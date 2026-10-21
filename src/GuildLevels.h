/*
 * mod-guild-levels: Cataclysm-style guild levels for WotLK 3.3.5a.
 *
 * A guild earns guild XP from its members' quests and from dungeon and raid bosses killed in a
 * guild group, and climbs from level 1 to 25. Every level from 2 to 25 unlocks one perk, the same
 * order as Cataclysm. The perks live in GuildLevelsPerks.cpp, the three castable ones in
 * GuildLevelsSpells.cpp, guild XP and storage in GuildLevelsMgr.cpp. Members also earn guild
 * reputation (GuildLevelsRep.cpp) and spend it at the guild vendors (GuildLevelsRewards.cpp).
 *
 * Released under the MIT License.
 */

#ifndef MOD_GUILD_LEVELS_H
#define MOD_GUILD_LEVELS_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <array>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class Player;
class WorldSession;

namespace GuildLevels
{
    constexpr uint8 MAX_LEVEL = 25;
    constexpr char const* PREFIX = "GuildLevels";

    // The guild level each perk unlocks at (Cataclysm 4.3.4's order).
    enum PerkLevel : uint8
    {
        PERK_FAST_TRACK_1       = 2,
        PERK_MOUNT_UP           = 3,
        PERK_MR_POPULARITY_1    = 4,
        PERK_CASH_FLOW_1        = 5,
        PERK_FAST_TRACK_2       = 6,
        PERK_REINFORCE_1        = 7,
        PERK_RIDE_LIKE_THE_WIND = 8,    // Cataclysm: Hasty Hearth
        PERK_REINFORCE_2        = 9,
        PERK_CHUG_A_LUG_1       = 10,
        PERK_MOBILE_BANKING     = 11,
        PERK_MR_POPULARITY_2    = 12,
        PERK_HONORABLE_MENTION_1 = 13,
        PERK_WORKING_OVERTIME   = 14,
        PERK_QUICK_AND_DEAD     = 15,
        PERK_CASH_FLOW_2        = 16,
        PERK_DOCTOR_IS_IN       = 17,   // Cataclysm: Guild Mail
        PERK_FOR_GREAT_JUSTICE  = 18,
        PERK_HONORABLE_MENTION_2 = 19,
        PERK_HAPPY_HOUR         = 20,
        PERK_HAVE_GROUP_WILL_TRAVEL = 21,
        PERK_CHUG_A_LUG_2       = 22,
        PERK_BOUNTIFUL_BAGS     = 23,
        PERK_BARTERING          = 24,
        PERK_MASS_RESURRECTION  = 25,
    };

    // Spells. 90450/90451 exist only on the server (spell_dbc); the castable three are in the
    // client's Spell.dbc too (tools/build_patch.py).
    enum Spells : uint32
    {
        SPELL_MOUNT_UP          = 90450,    // hidden passive: mounted and flying speed
        SPELL_QUICK_AND_DEAD    = 90451,    // hidden: ghost run speed
        SPELL_MOBILE_BANKING    = 90452,
        SPELL_HAVE_GROUP        = 90453,
        SPELL_MASS_RESURRECTION = 90454,
        SPELL_FOR_GREAT_JUSTICE = 90455,    // hidden passive: mod-boss-coins' bonus coin chance

        // Rewards (client + spell_dbc rows from tools/build_patch.py)
        SPELL_GUILD_TELEPORT_8H = 90456,    // Renowned Guild Tabard
        SPELL_GUILD_TELEPORT_2H = 90457,    // Illustrious Guild Tabard
        SPELL_STANDARD_AURA_1   = 90461,    // the battle standards' area auras, ranks 1-3
        SPELL_STANDARD_AURA_2   = 90462,
        SPELL_STANDARD_AURA_3   = 90463,
        SPELL_FLASK_OF_BATTLE   = 90466,
    };

    constexpr uint32 GO_GUILD_CHEST = 9503400;  // Mobile Banking's guild bank, gameobject type 34

    // The battle standards the standard items plant, ranks 1-3 (creature_template).
    constexpr uint32 NPC_BATTLE_STANDARD_1 = 9503410;
    constexpr uint32 NPC_BATTLE_STANDARD_3 = 9503412;

    struct Config
    {
        bool enabled = true;
        bool announce = true;

        // Guild XP
        float curveScale = 0.0125f;     // Cataclysm's XP per level times this
        float questShare = 0.25f;       // share of a quest's XP that goes to the guild
        uint32 bossXPDungeon = 2000;
        uint32 bossXPRaid = 6000;
        float heroicBonus = 1.5f;
        uint32 dailyCap = 150000;       // 0 = no cap
        uint8 dailyCapUntilLevel = 20;  // the cap stops applying once the guild reaches this level
        uint32 raidGuildMembers = 10;   // raid kills: guild members needed (at most 80% of the raid)
        bool botGuildsEarn = false;     // guilds without a real player never earn XP when false

        // Perks (percent unless noted)
        uint32 fastTrack1 = 5, fastTrack2 = 10;
        uint32 mountUp = 10;
        uint32 mrPopularity1 = 5, mrPopularity2 = 10;
        uint32 cashFlow1 = 5, cashFlow2 = 10;
        uint32 reinforce1 = 10, reinforce2 = 20;
        uint32 rideLikeTheWind = 25;
        uint32 chugALug1 = 50, chugALug2 = 100;
        uint32 honorableMention1 = 5, honorableMention2 = 10;
        uint32 workingOvertime = 10;
        uint32 quickAndDeadSpeed = 100, quickAndDeadRes = 50;
        uint32 doctorIsIn = 50;
        uint32 forGreatJusticeChance = 10;
        uint32 happyHourChance = 50;
        uint32 bountifulBagsChance = 15;
        uint32 bartering = 10;
        uint32 massResHealth = 35;
        uint32 massResMinutes = 10;     // a player can be raised by Mass Resurrection once per this

        // Guild reputation
        bool repEnabled = true;
        uint32 repFaction = 1168;       // Faction.dbc row the rep goes to
        uint32 repXPPerRep = 36;        // guild XP a member earns per point of rep
        uint32 repWeeklyCap = 4375;     // 0 = no cap
        bool repRealmRate = true;       // scale rep and weekly cap by Rate.Reputation.Gain

        // Guild vendors
        bool rewardsEnabled = true;
    };

    Config const& GetConfig();
    void LoadConfig();

    // --- guild state (GuildLevelsMgr.cpp). Safe from any thread. ---

    struct GuildState
    {
        uint8 level = 1;
        uint64 xp = 0;          // into the current level
        uint64 totalXp = 0;
        uint32 day = 0;
        uint64 dayXp = 0;
        uint32 cashWeek = 0;    // the week cashWeekMoney is for
        uint64 cashWeekMoney = 0;   // Cash Flow into the guild bank, copper
        uint64 cashTotal = 0;
    };

    // XP from level to level + 1 (0 at the cap).
    uint64 XPForLevel(uint8 level);
    uint64 DailyCap(uint8 level);

    uint8 GetLevel(uint32 guildId);
    GuildState GetState(uint32 guildId);
    bool HasPerk(Player const* player, uint8 perkLevel);

    // Adds guild XP, capped by the daily cap. Contributors are characters who earned it; the XP
    // is split between them for the roster. Returns what the guild got.
    uint64 AddXP(uint32 guildId, uint64 xp, std::vector<ObjectGuid::LowType> const& contributors);

    // GM commands: set the level outright, or add XP without the cap.
    void SetLevel(uint32 guildId, uint8 level);
    void AddXPUncapped(uint32 guildId, uint64 xp);

    // Cash Flow: money paid into the guild bank from a member's loot, for the totals.
    void AddCashFlow(uint32 guildId, ObjectGuid::LowType guid, uint64 money);

    void ForgetMember(ObjectGuid::LowType guid);
    void ForgetGuild(uint32 guildId);

    struct MemberStats
    {
        uint64 weekXp = 0;
        uint64 totalXp = 0;
        uint64 weekCash = 0;    // Cash Flow this member's loot paid into the bank, copper
    };
    MemberStats GetMemberStats(ObjectGuid::LowType guid, uint32 guildId);
    std::vector<std::pair<ObjectGuid::LowType, MemberStats>> GetGuildMembers(uint32 guildId);

    // Guild reputation bookkeeping (GuildLevelsMgr.cpp; the standing itself is the core's).
    // A member's rep belongs to the guild they earned it in; this is that guild (0: none yet).
    uint32 GetRepGuild(ObjectGuid::LowType guid);
    void SetRepGuild(ObjectGuid::LowType guid, uint32 guildId);
    // Turns guild XP into rep: keeps the remainder, applies the realm rate and the weekly cap,
    // returns the rep.
    uint32 TakeRep(ObjectGuid::LowType guid, uint64 guildXp, uint32 roomToExalted);
    // GuildLevels.Rep.WeeklyCap times the realm rate (0 = no cap).
    uint32 WeeklyRepCap();
    uint32 GetWeekRep(ObjectGuid::LowType guid);
    void ForgetRep(ObjectGuid::LowType guid);

    void Load();
    void Save(bool now);            // writes what changed; now = synchronous (shutdown)
    void SaveNews(CharacterDatabaseTransaction trans, bool& any);   // GuildLevelsNews.cpp, from Save
    void ProcessEvents();           // world thread: level-up announcements

    // --- perks (GuildLevelsPerks.cpp) ---

    // Brings a player's perk auras and spells in line with their guild's level. Map thread.
    void RefreshPerks(Player* player, bool force);

    // The Doctor Is In: the heal spells of healing potions and healthstones, from the item
    // templates. Built once at startup, read-only after.
    void LoadHealingConsumables();

    // GuildLevelsSpells.cpp: a resurrection from a request was accepted; if it was Mass
    // Resurrection's, the player can't be raised by it again for a while.
    void MassResurrectionAccepted(Player* player);

    struct PerkInfo
    {
        uint8 level;
        char const* name;
        std::string description;
    };
    std::vector<PerkInfo> PerkList();
    std::string PerkName(uint8 level);

    // --- news feed (GuildLevelsNews.cpp). Safe from any thread. ---

    enum NewsType : uint8
    {
        NEWS_GUILD_LEVEL    = 0,    // value = level
        NEWS_ENCOUNTER      = 1,    // value = DungeonEncounter.dbc id, extra = map difficulty
        NEWS_ITEM_LOOTED    = 2,    // guid = member, value = item entry, extra = quality
        NEWS_ITEM_CRAFTED   = 3,    // the same
        NEWS_ACHIEVEMENT    = 4,    // guid = member, value = achievement id, extra = points
    };

    struct NewsItem
    {
        uint32 id;
        uint8 type;
        uint32 time;                // unix seconds
        ObjectGuid::LowType guid;   // the member it's about, 0 for the whole guild
        uint32 value;
        uint32 extra;
    };

    void LoadNewsConfig();
    void AddNews(uint32 guildId, NewsType type, ObjectGuid::LowType guid, uint32 value, uint32 extra = 0);
    std::vector<NewsItem> GetNews(uint32 guildId);     // newest first
    void ForgetGuildNews(uint32 guildId);
    void LoadNews();

    // --- guild reputation (GuildLevelsRep.cpp). Map thread of the player. ---

    // Rep for guild XP the player earned themself (quests, a boss killed in a guild group).
    void GainGuildRep(Player* player, uint64 guildXp);
    // Resets the rep when the player is now in a different guild than the one it was earned in,
    // and moves it to Inactive while they're in none.
    void RefreshGuildRep(Player* player);

    struct RepInfo
    {
        int32 standing = 0;     // the reputation value (0 = Neutral 0)
        uint8 rank = 3;         // ReputationRank: 3 Neutral .. 7 Exalted
        uint32 weekRep = 0;
    };
    RepInfo GetRepInfo(Player* player);
    uint8 GetRepRank(Player const* player);

    // --- guild vendors (GuildLevelsRewards.cpp) ---

    enum RewardCategory : uint8
    {
        REWARD_TABARD = 1, REWARD_HEIRLOOM, REWARD_PET, REWARD_MOUNT, REWARD_CONSUMABLE,
    };

    struct Reward
    {
        uint32 item = 0;
        uint8 team = 0;         // 0 both, 1 Alliance, 2 Horde
        uint8 guildLevel = 1;
        uint8 repRank = 3;      // ReputationRank
        uint32 price = 0;       // copper, before the vendor's discount
        uint8 category = 0;
    };

    void LoadRewards();                         // world thread, startup
    std::vector<Reward> const& GetRewards();    // read-only after startup
    std::vector<uint32> const& GetVendorEntries();

    // --- addon (GuildLevelsAddon.cpp) ---

    void SendAddon(Player* player, std::string const& payload);
    void SendState(Player* player);

    // Bot sessions: IsHeadless() on current playerbots cores, IsBot() on older ones.
    template <typename Session, typename = void>
    struct HasIsHeadless : std::false_type { };

    template <typename Session>
    struct HasIsHeadless<Session, std::void_t<decltype(std::declval<Session&>().IsHeadless())>> : std::true_type { };

    template <typename Session, typename = void>
    struct HasIsBot : std::false_type { };

    template <typename Session>
    struct HasIsBot<Session, std::void_t<decltype(std::declval<Session&>().IsBot())>> : std::true_type { };

    template <typename Session>
    bool IsBotSession(Session* session)
    {
        if constexpr (HasIsHeadless<Session>::value)
            return session->IsHeadless();
        else if constexpr (HasIsBot<Session>::value)
            return session->IsBot();
        else
            return false;
    }

    bool IsRealPlayer(Player const* player);
}

#endif
