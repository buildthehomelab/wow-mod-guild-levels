/*
 * mod-guild-levels: guild XP, levels and their storage.
 *
 * Guild XP arrives from map threads (quests, boss kills), so all of the state below sits behind
 * one mutex and the hooks only ever hold it briefly. Nothing is written to the database from a
 * hook: changed guilds and members are marked dirty and saved together every SaveInterval
 * seconds (and on shutdown). Level-ups are queued and announced from the world thread, where
 * the guild's member list may be read.
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace GuildLevels
{
    namespace
    {
        Config config;

        // Cataclysm's guild XP from level N to N + 1 (N = 1..24), in thousands.
        constexpr std::array<uint32, MAX_LEVEL - 1> CATACLYSM_XP =
        {
            16580, 18240, 19900, 21550, 23220, 24880, 26530, 28190, 29850, 31510, 33170, 34820,
            36490, 38140, 39800, 41450, 43120, 44780, 46430, 48090, 49750, 51410, 53060, 54730
        };

        struct MemberRow
        {
            uint32 guildId = 0;
            uint32 week = 0;
            uint64 weekXp = 0;
            uint64 totalXp = 0;
            uint64 weekCash = 0;
        };

        std::mutex lock;
        std::unordered_map<uint32, GuildState> guilds;
        std::unordered_map<ObjectGuid::LowType, MemberRow> members;
        std::unordered_set<uint32> dirtyGuilds;
        std::unordered_set<ObjectGuid::LowType> dirtyMembers;
        std::unordered_set<uint32> deletedGuilds;
        std::unordered_set<ObjectGuid::LowType> deletedMembers;
        std::vector<std::pair<uint32, uint8>> levelUps;

        uint32 Today()
        {
            return uint32(GameTime::GetGameTime().count() / DAY);
        }

        // Weeks start on Tuesday (1970-01-01 was a Thursday).
        uint32 ThisWeek()
        {
            return (Today() + 2) / 7;
        }

        // Caller holds the lock.
        void RollDay(GuildState& state)
        {
            uint32 today = Today();
            if (state.day != today)
            {
                state.day = today;
                state.dayXp = 0;
            }
        }

        // Caller holds the lock. The member's row for this guild and week.
        MemberRow& Row(uint32 guildId, ObjectGuid::LowType guid)
        {
            MemberRow& row = members[guid];
            uint32 week = ThisWeek();
            if (row.guildId != guildId)
                row = MemberRow{ guildId, week, 0, 0, 0 };
            if (row.week != week)
            {
                row.week = week;
                row.weekXp = 0;
                row.weekCash = 0;
            }
            dirtyMembers.insert(guid);
            deletedMembers.erase(guid);
            return row;
        }

        // Caller holds the lock.
        void Credit(uint32 guildId, ObjectGuid::LowType guid, uint64 xp)
        {
            MemberRow& row = Row(guildId, guid);
            row.weekXp += xp;
            row.totalXp += xp;
        }

        // Caller holds the lock. Adds xp to the guild's level progress and queues level-ups.
        void Grow(uint32 guildId, GuildState& state, uint64 xp)
        {
            state.totalXp += xp;
            state.xp += xp;
            while (state.level < MAX_LEVEL && state.xp >= XPForLevel(state.level))
            {
                state.xp -= XPForLevel(state.level);
                ++state.level;
                levelUps.emplace_back(guildId, state.level);
            }
            if (state.level >= MAX_LEVEL)
                state.xp = 0;
            dirtyGuilds.insert(guildId);
            deletedGuilds.erase(guildId);
        }
    }

    Config const& GetConfig()
    {
        return config;
    }

    void LoadConfig()
    {
        Config c;
        c.enabled = sConfigMgr->GetOption<bool>("GuildLevels.Enable", true);
        c.announce = sConfigMgr->GetOption<bool>("GuildLevels.Announce", true);
        c.curveScale = std::max(0.0001f, sConfigMgr->GetOption<float>("GuildLevels.XP.CurveScale", 0.0125f));
        c.questShare = std::max(0.0f, sConfigMgr->GetOption<float>("GuildLevels.XP.QuestShare", 0.25f));
        c.bossXPDungeon = sConfigMgr->GetOption<uint32>("GuildLevels.XP.BossDungeon", 2000);
        c.bossXPRaid = sConfigMgr->GetOption<uint32>("GuildLevels.XP.BossRaid", 6000);
        c.heroicBonus = std::max(1.0f, sConfigMgr->GetOption<float>("GuildLevels.XP.HeroicBonus", 1.5f));
        c.dailyCap = sConfigMgr->GetOption<uint32>("GuildLevels.XP.DailyCap", 150000);
        c.dailyCapUntilLevel = uint8(std::min<uint32>(MAX_LEVEL, sConfigMgr->GetOption<uint32>("GuildLevels.XP.DailyCapUntilLevel", 20)));
        c.raidGuildMembers = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("GuildLevels.XP.RaidGuildMembers", 10));
        c.botGuildsEarn = sConfigMgr->GetOption<bool>("GuildLevels.XP.BotGuildsEarn", false);

        auto pct = [](char const* key, uint32 def) { return std::min<uint32>(1000, sConfigMgr->GetOption<uint32>(key, def)); };
        c.fastTrack1 = pct("GuildLevels.Perk.FastTrack1", 5);
        c.fastTrack2 = pct("GuildLevels.Perk.FastTrack2", 10);
        c.mountUp = pct("GuildLevels.Perk.MountUp", 10);
        c.mrPopularity1 = pct("GuildLevels.Perk.MrPopularity1", 5);
        c.mrPopularity2 = pct("GuildLevels.Perk.MrPopularity2", 10);
        c.cashFlow1 = pct("GuildLevels.Perk.CashFlow1", 5);
        c.cashFlow2 = pct("GuildLevels.Perk.CashFlow2", 10);
        c.reinforce1 = std::min<uint32>(100, pct("GuildLevels.Perk.Reinforce1", 10));
        c.reinforce2 = std::min<uint32>(100, pct("GuildLevels.Perk.Reinforce2", 20));
        c.rideLikeTheWind = pct("GuildLevels.Perk.RideLikeTheWind", 25);
        c.chugALug1 = pct("GuildLevels.Perk.ChugALug1", 50);
        c.chugALug2 = pct("GuildLevels.Perk.ChugALug2", 100);
        c.honorableMention1 = pct("GuildLevels.Perk.HonorableMention1", 5);
        c.honorableMention2 = pct("GuildLevels.Perk.HonorableMention2", 10);
        c.workingOvertime = std::min<uint32>(100, pct("GuildLevels.Perk.WorkingOvertime", 10));
        c.quickAndDeadSpeed = pct("GuildLevels.Perk.QuickAndDeadSpeed", 100);
        c.quickAndDeadRes = pct("GuildLevels.Perk.QuickAndDeadRes", 50);
        c.doctorIsIn = pct("GuildLevels.Perk.DoctorIsIn", 50);
        c.forGreatJusticeChance = std::min<uint32>(100, pct("GuildLevels.Perk.ForGreatJusticeChance", 10));
        c.happyHourChance = std::min<uint32>(100, pct("GuildLevels.Perk.HappyHourChance", 50));
        c.bountifulBagsChance = std::min<uint32>(100, pct("GuildLevels.Perk.BountifulBagsChance", 15));
        c.bartering = std::min<uint32>(100, pct("GuildLevels.Perk.Bartering", 10));
        c.massResHealth = std::min<uint32>(100, pct("GuildLevels.Perk.MassResurrectionHealth", 35));
        c.massResMinutes = sConfigMgr->GetOption<uint32>("GuildLevels.Perk.MassResurrectionMinutes", 10);
        config = c;
    }

    uint64 XPForLevel(uint8 level)
    {
        if (level < 1 || level >= MAX_LEVEL)
            return 0;
        return std::max<uint64>(1, uint64(double(CATACLYSM_XP[level - 1]) * 1000.0 * config.curveScale));
    }

    uint64 DailyCap(uint8 level)
    {
        if (!config.dailyCap || level >= config.dailyCapUntilLevel)
            return 0;
        return config.dailyCap;
    }

    uint8 GetLevel(uint32 guildId)
    {
        if (!guildId || !config.enabled)
            return 0;
        std::lock_guard<std::mutex> guard(lock);
        auto itr = guilds.find(guildId);
        return itr != guilds.end() ? itr->second.level : 1;
    }

    GuildState GetState(uint32 guildId)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto itr = guilds.find(guildId);
        if (itr == guilds.end())
            return GuildState{};
        RollDay(itr->second);
        GuildState state = itr->second;
        if (state.cashWeek != ThisWeek())
            state.cashWeekMoney = 0;
        return state;
    }

    bool HasPerk(Player const* player, uint8 perkLevel)
    {
        return player && GetLevel(player->GetGuildId()) >= perkLevel;
    }

    uint64 AddXP(uint32 guildId, uint64 xp, std::vector<ObjectGuid::LowType> const& contributors)
    {
        if (!config.enabled || !guildId || !xp)
            return 0;

        std::lock_guard<std::mutex> guard(lock);
        GuildState& state = guilds[guildId];
        if (state.level >= MAX_LEVEL)
            return 0;

        RollDay(state);
        if (uint64 cap = DailyCap(state.level))
            xp = std::min(xp, cap > state.dayXp ? cap - state.dayXp : 0);
        if (!xp)
            return 0;

        state.dayXp += xp;
        Grow(guildId, state, xp);

        if (!contributors.empty())
        {
            uint64 share = std::max<uint64>(1, xp / contributors.size());
            for (ObjectGuid::LowType guid : contributors)
                Credit(guildId, guid, share);
        }
        return xp;
    }

    void SetLevel(uint32 guildId, uint8 level)
    {
        level = std::clamp<uint8>(level, 1, MAX_LEVEL);
        std::lock_guard<std::mutex> guard(lock);
        GuildState& state = guilds[guildId];
        if (level > state.level)
            levelUps.emplace_back(guildId, level);
        state.level = level;
        state.xp = 0;
        dirtyGuilds.insert(guildId);
        deletedGuilds.erase(guildId);
    }

    void AddXPUncapped(uint32 guildId, uint64 xp)
    {
        std::lock_guard<std::mutex> guard(lock);
        GuildState& state = guilds[guildId];
        if (state.level < MAX_LEVEL)
            Grow(guildId, state, xp);
    }

    void AddCashFlow(uint32 guildId, ObjectGuid::LowType guid, uint64 money)
    {
        if (!guildId || !money)
            return;
        std::lock_guard<std::mutex> guard(lock);
        GuildState& state = guilds[guildId];
        uint32 week = ThisWeek();
        if (state.cashWeek != week)
        {
            state.cashWeek = week;
            state.cashWeekMoney = 0;
        }
        state.cashWeekMoney += money;
        state.cashTotal += money;
        dirtyGuilds.insert(guildId);
        deletedGuilds.erase(guildId);
        Row(guildId, guid).weekCash += money;
    }

    void ForgetMember(ObjectGuid::LowType guid)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (members.erase(guid))
        {
            dirtyMembers.erase(guid);
            deletedMembers.insert(guid);
        }
    }

    void ForgetGuild(uint32 guildId)
    {
        std::lock_guard<std::mutex> guard(lock);
        guilds.erase(guildId);
        dirtyGuilds.erase(guildId);
        deletedGuilds.insert(guildId);
        std::erase_if(levelUps, [guildId](auto const& up) { return up.first == guildId; });
        for (auto itr = members.begin(); itr != members.end();)
        {
            if (itr->second.guildId == guildId)
            {
                dirtyMembers.erase(itr->first);
                itr = members.erase(itr);
            }
            else
                ++itr;
        }
    }

    MemberStats GetMemberStats(ObjectGuid::LowType guid, uint32 guildId)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto itr = members.find(guid);
        if (itr == members.end() || itr->second.guildId != guildId)
            return MemberStats{};
        bool thisWeek = itr->second.week == ThisWeek();
        return MemberStats{ thisWeek ? itr->second.weekXp : 0, itr->second.totalXp, thisWeek ? itr->second.weekCash : 0 };
    }

    std::vector<std::pair<ObjectGuid::LowType, MemberStats>> GetGuildMembers(uint32 guildId)
    {
        std::vector<std::pair<ObjectGuid::LowType, MemberStats>> out;
        uint32 week = ThisWeek();
        std::lock_guard<std::mutex> guard(lock);
        for (auto const& [guid, row] : members)
            if (row.guildId == guildId)
                out.emplace_back(guid, MemberStats{ row.week == week ? row.weekXp : 0, row.totalXp, row.week == week ? row.weekCash : 0 });
        return out;
    }

    void Load()
    {
        std::lock_guard<std::mutex> guard(lock);
        guilds.clear();
        members.clear();

        if (QueryResult result = CharacterDatabase.Query("SELECT guildid, level, xp, total_xp, day, day_xp, cash_week, cash_week_money, cash_total FROM mod_guild_levels"))
        {
            do
            {
                Field* f = result->Fetch();
                uint32 guildId = f[0].Get<uint32>();
                if (!sGuildMgr->GetGuildById(guildId))
                {
                    deletedGuilds.insert(guildId);
                    continue;
                }
                GuildState& state = guilds[guildId];
                state.level = std::clamp<uint8>(f[1].Get<uint8>(), 1, MAX_LEVEL);
                state.xp = f[2].Get<uint64>();
                state.totalXp = f[3].Get<uint64>();
                state.day = f[4].Get<uint32>();
                state.dayXp = f[5].Get<uint64>();
                state.cashWeek = f[6].Get<uint32>();
                state.cashWeekMoney = f[7].Get<uint64>();
                state.cashTotal = f[8].Get<uint64>();
            } while (result->NextRow());
        }

        if (QueryResult result = CharacterDatabase.Query("SELECT guid, guildid, week, week_xp, total_xp, week_cash FROM mod_guild_levels_member"))
        {
            do
            {
                Field* f = result->Fetch();
                uint32 guildId = f[1].Get<uint32>();
                if (!sGuildMgr->GetGuildById(guildId))
                    continue;   // its guild is gone; Save deletes the guild's rows
                MemberRow& row = members[f[0].Get<uint32>()];
                row.guildId = guildId;
                row.week = f[2].Get<uint32>();
                row.weekXp = f[3].Get<uint64>();
                row.totalXp = f[4].Get<uint64>();
                row.weekCash = f[5].Get<uint64>();
            } while (result->NextRow());
        }

        LOG_INFO("module", "mod-guild-levels: {} guilds, {} members loaded", guilds.size(), members.size());
    }

    void Save(bool now)
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        bool any = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            for (uint32 guildId : deletedGuilds)
            {
                trans->Append("DELETE FROM mod_guild_levels WHERE guildid = {}", guildId);
                trans->Append("DELETE FROM mod_guild_levels_member WHERE guildid = {}", guildId);
                any = true;
            }
            for (ObjectGuid::LowType guid : deletedMembers)
            {
                trans->Append("DELETE FROM mod_guild_levels_member WHERE guid = {}", guid);
                any = true;
            }
            for (uint32 guildId : dirtyGuilds)
            {
                auto itr = guilds.find(guildId);
                if (itr == guilds.end())
                    continue;
                GuildState const& s = itr->second;
                trans->Append("REPLACE INTO mod_guild_levels (guildid, level, xp, total_xp, day, day_xp, cash_week, cash_week_money, cash_total) "
                    "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {})",
                    guildId, s.level, s.xp, s.totalXp, s.day, s.dayXp, s.cashWeek, s.cashWeekMoney, s.cashTotal);
                any = true;
            }
            for (ObjectGuid::LowType guid : dirtyMembers)
            {
                auto itr = members.find(guid);
                if (itr == members.end())
                    continue;
                MemberRow const& r = itr->second;
                trans->Append("REPLACE INTO mod_guild_levels_member (guid, guildid, week, week_xp, total_xp, week_cash) VALUES ({}, {}, {}, {}, {}, {})",
                    guid, r.guildId, r.week, r.weekXp, r.totalXp, r.weekCash);
                any = true;
            }
            deletedGuilds.clear();
            deletedMembers.clear();
            dirtyGuilds.clear();
            dirtyMembers.clear();
        }
        SaveNews(trans, any);

        if (!any)
            return;
        if (now)
            CharacterDatabase.DirectCommitTransaction(trans);
        else
            CharacterDatabase.CommitTransaction(trans);
    }

    void ProcessEvents()
    {
        std::vector<std::pair<uint32, uint8>> ups;
        {
            std::lock_guard<std::mutex> guard(lock);
            ups.swap(levelUps);
        }

        for (auto const& [guildId, level] : ups)
        {
            Guild* guild = sGuildMgr->GetGuildById(guildId);
            if (!guild)
                continue;

            LOG_INFO("module", "mod-guild-levels: guild {} ({}) reached level {}", guild->GetName(), guildId, level);
            AddNews(guildId, NEWS_GUILD_LEVEL, 0, level);
            if (!config.announce)
                continue;

            std::string text = Acore::StringFormat("|cff40c040Your guild has reached level {}!|r", level);
            std::string perk = PerkName(level);
            if (!perk.empty())
                text += Acore::StringFormat(" New guild perk: |cffffd200{}|r.", perk);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, nullptr, nullptr, text);
            guild->BroadcastPacket(&data);
        }
    }

    bool IsRealPlayer(Player const* player)
    {
        return player && player->GetSession() && !IsBotSession(player->GetSession());
    }
}
