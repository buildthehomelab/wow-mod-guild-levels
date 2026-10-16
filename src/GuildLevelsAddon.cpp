/*
 * mod-guild-levels: the GuildLevels addon's messages, the .guildlevel command, and the world and
 * guild hooks that load, save and clean up.
 *
 * Addon wire format, both directions: an addon whisper to yourself, "GuildLevels\t<COMMAND>:...".
 *   client -> server  HELLO (perk list + state), STATE, ROSTER
 *   server -> client  S:<level>:<xp>:<xp needed>:<today's xp>:<daily cap>:<total xp>:<my week>:<my total>
 *                       :<guild Cash Flow this week>:<guild Cash Flow total>:<my Cash Flow this week> (copper)
 *                     UP:<level>:<perk name>    the guild just reached that level
 *                     K:<level>:<name>:<description> ... KEND
 *                     R:<name>,<week xp>,<total xp>;... ... REND
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "GameTime.h"
#include <algorithm>
#include <unordered_map>

using namespace Acore::ChatCommands;
using namespace GuildLevels;

namespace
{
    constexpr std::size_t MAX_PAYLOAD = 240;
    constexpr uint32 SAVE_INTERVAL_MS = 60 * IN_MILLISECONDS;
    constexpr Milliseconds REQUEST_COOLDOWN = 1s;

    enum Request : uint8 { REQUEST_HELLO, REQUEST_STATE, REQUEST_ROSTER, REQUEST_COUNT };

    // Addon requests per player, each kind at most once a second (the addon sends two kinds
    // back to back when its window opens). World thread only.
    std::unordered_map<ObjectGuid::LowType, std::array<Milliseconds, REQUEST_COUNT>> lastRequest;

    bool Throttled(Player* player, Request request)
    {
        Milliseconds now = GameTime::GetGameTimeMS();
        Milliseconds& last = lastRequest[player->GetGUID().GetCounter()][request];
        if (last.count() && now - last < REQUEST_COOLDOWN)
            return true;
        last = now;
        return false;
    }

    void SendRows(Player* player, std::string const& header, std::vector<std::string> const& rows, std::string const& end)
    {
        std::string line;
        std::size_t const budget = MAX_PAYLOAD - header.size() - 1;
        for (std::string const& row : rows)
        {
            if (!line.empty() && line.size() + 1 + row.size() > budget)
            {
                SendAddon(player, header + ":" + line);
                line.clear();
            }
            if (!line.empty())
                line += ';';
            line += row;
        }
        if (!line.empty())
            SendAddon(player, header + ":" + line);
        SendAddon(player, end);
    }

    void SendPerks(Player* player)
    {
        for (PerkInfo const& perk : PerkList())
        {
            std::string line = Acore::StringFormat("K:{}:{}:{}", perk.level, perk.name, perk.description);
            if (line.size() > MAX_PAYLOAD)
                line.resize(MAX_PAYLOAD);
            SendAddon(player, line);
        }
        SendAddon(player, "KEND");
    }

    void SendRoster(Player* player)
    {
        Guild* guild = player->GetGuild();
        if (!guild)
            return;

        // Only current members: a GM removal, a faction change or a deleted character leaves
        // the guild without the leave event that clears a member's row.
        auto members = GetGuildMembers(guild->GetId());
        uint32 guildId = guild->GetId();
        std::erase_if(members, [guildId](auto const& m)
        {
            return sCharacterCache->GetCharacterGuildIdByGuid(ObjectGuid::Create<HighGuid::Player>(m.first)) != guildId;
        });
        std::sort(members.begin(), members.end(), [](auto const& a, auto const& b)
        {
            return a.second.weekXp != b.second.weekXp ? a.second.weekXp > b.second.weekXp : a.second.totalXp > b.second.totalXp;
        });

        std::vector<std::string> rows;
        for (auto const& [guid, stats] : members)
        {
            std::string name;
            if (!sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(guid), name))
                continue;
            rows.push_back(Acore::StringFormat("{},{},{}", name, stats.weekXp, stats.totalXp));
        }
        SendRows(player, "R", rows, "REND");
    }

    std::string Progress(uint32 guildId)
    {
        GuildState s = GetState(guildId);
        if (s.level >= MAX_LEVEL)
            return Acore::StringFormat("level {} (max)", s.level);
        std::string text = Acore::StringFormat("level {}, {} / {} XP to level {}", s.level, s.xp, XPForLevel(s.level), s.level + 1);
        if (uint64 cap = DailyCap(s.level))
            text += Acore::StringFormat(", {} / {} today", s.dayXp, cap);
        return text;
    }

    Guild* FindGuild(ChatHandler* handler, Optional<Tail> name)
    {
        if (name && !name->empty())
        {
            Guild* guild = sGuildMgr->GetGuildByName(std::string(*name));
            if (!guild)
                handler->SendErrorMessage("No guild called {}.", std::string(*name));
            return guild;
        }
        Player* player = handler->getSelectedPlayerOrSelf();
        Guild* guild = player ? player->GetGuild() : nullptr;
        if (!guild)
            handler->SendErrorMessage("Name a guild, or select someone in one.");
        return guild;
    }
}

namespace GuildLevels
{
    void SendAddon(Player* player, std::string const& payload)
    {
        std::string full = std::string(PREFIX) + "\t" + payload;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, full);
        player->GetSession()->SendPacket(&data);
    }

    void SendState(Player* player)
    {
        uint32 guildId = player->GetGuildId();
        if (!guildId || !GetConfig().enabled)
        {
            SendAddon(player, "S:0:0:0:0:0:0:0:0:0:0:0");
            return;
        }
        GuildState s = GetState(guildId);
        MemberStats me = GetMemberStats(player->GetGUID().GetCounter(), guildId);
        SendAddon(player, Acore::StringFormat("S:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}", s.level, s.xp, XPForLevel(s.level),
            s.dayXp, DailyCap(s.level), s.totalXp, me.weekXp, me.totalXp, s.cashWeekMoney, s.cashTotal, me.weekCash));
    }
}

class GuildLevelsAddonPlayerScript : public PlayerScript
{
public:
    GuildLevelsAddonPlayerScript() : PlayerScript("GuildLevelsAddonPlayerScript", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogout(Player* player) override
    {
        lastRequest.erase(player->GetGUID().GetCounter());
    }

    // CMSG_MESSAGECHAT runs on the world thread, so the guild and the character cache are safe here.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || receiver != player)
            return true;

        std::string const prefixTab = std::string(PREFIX) + "\t";
        if (msg.compare(0, prefixTab.size(), prefixTab) != 0)
            return true;

        std::string_view command = std::string_view(msg).substr(prefixTab.size());
        if (command == "HELLO")
        {
            if (!Throttled(player, REQUEST_HELLO))
            {
                SendPerks(player);
                SendState(player);
            }
        }
        else if (command == "STATE")
        {
            if (!Throttled(player, REQUEST_STATE))
                SendState(player);
        }
        else if (command == "ROSTER")
        {
            if (!Throttled(player, REQUEST_ROSTER))
                SendRoster(player);
        }
        return false;
    }
};

class GuildLevelsWorldScript : public WorldScript
{
public:
    GuildLevelsWorldScript() : WorldScript("GuildLevelsWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnStartup() override
    {
        Load();
        LoadHealingConsumables();
    }

    void OnUpdate(uint32 diff) override
    {
        ProcessEvents();
        if (saveTimer > diff)
        {
            saveTimer -= diff;
            return;
        }
        saveTimer = SAVE_INTERVAL_MS;
        Save(false);
    }

    void OnShutdown() override
    {
        Save(true);
    }

private:
    uint32 saveTimer = SAVE_INTERVAL_MS;
};

class GuildLevelsGuildScript : public GuildScript
{
public:
    GuildLevelsGuildScript() : GuildScript("GuildLevelsGuildScript", { GUILDHOOK_ON_EVENT, GUILDHOOK_ON_DISBAND }) { }

    // Leaving or being removed resets a member's contribution, as in Cataclysm.
    void OnEvent(Guild* /*guild*/, uint8 eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2, uint8 /*newRank*/) override
    {
        if (eventType == GUILD_EVENT_LOG_LEAVE_GUILD)
            ForgetMember(playerGuid1);
        else if (eventType == GUILD_EVENT_LOG_UNINVITE_PLAYER)
            ForgetMember(playerGuid2);
    }

    void OnDisband(Guild* guild) override
    {
        ForgetGuild(guild->GetId());
    }
};

class GuildLevelsCommandScript : public CommandScript
{
public:
    GuildLevelsCommandScript() : CommandScript("GuildLevelsCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable subCommands =
        {
            { "",      HandleSelf,  SEC_PLAYER,     Console::No },
            { "info",  HandleInfo,  SEC_GAMEMASTER, Console::Yes },
            { "set",   HandleSet,   SEC_ADMINISTRATOR, Console::Yes },
            { "addxp", HandleAddXP, SEC_ADMINISTRATOR, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "guildlevel", subCommands },
        };
        return commandTable;
    }

    // .guildlevel: your own guild.
    static bool HandleSelf(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        Guild* guild = player ? player->GetGuild() : nullptr;
        if (!GetConfig().enabled || !guild)
        {
            handler->SendErrorMessage(guild ? "Guild levels are turned off." : "You are not in a guild.");
            return false;
        }
        handler->PSendSysMessage("{}: {}.", guild->GetName(), Progress(guild->GetId()));
        return true;
    }

    // .guildlevel info [guild]: any guild, or the selected player's.
    static bool HandleInfo(ChatHandler* handler, Optional<Tail> name)
    {
        if (!GetConfig().enabled)
        {
            handler->SendErrorMessage("Guild levels are turned off.");
            return false;
        }
        Guild* guild = FindGuild(handler, name);
        if (!guild)
            return false;
        handler->PSendSysMessage("{}: {}.", guild->GetName(), Progress(guild->GetId()));
        return true;
    }

    // .guildlevel set <level> [guild]
    static bool HandleSet(ChatHandler* handler, uint8 level, Optional<Tail> name)
    {
        Guild* guild = FindGuild(handler, name);
        if (!guild)
            return false;
        SetLevel(guild->GetId(), level);
        handler->PSendSysMessage("{}: {}.", guild->GetName(), Progress(guild->GetId()));
        return true;
    }

    // .guildlevel addxp <xp> [guild], ignoring the daily cap
    static bool HandleAddXP(ChatHandler* handler, uint32 xp, Optional<Tail> name)
    {
        Guild* guild = FindGuild(handler, name);
        if (!guild)
            return false;
        AddXPUncapped(guild->GetId(), xp);
        handler->PSendSysMessage("{}: {}.", guild->GetName(), Progress(guild->GetId()));
        return true;
    }
};

void AddGuildLevelsAddonScripts()
{
    new GuildLevelsAddonPlayerScript();
    new GuildLevelsWorldScript();
    new GuildLevelsGuildScript();
    new GuildLevelsCommandScript();
}
