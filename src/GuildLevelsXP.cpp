/*
 * mod-guild-levels: where guild XP comes from.
 *
 * - Quests: a share (GuildLevels.XP.QuestShare, Cataclysm: 25%) of the quest's XP, also at the
 *   level cap, where the player gets money instead. Repeat completions that give no XP give none.
 *   The core also asks for a quest's XP to show it in the quest giver's windows, so the XP is only
 *   noted there and paid when the reward goes through (OnPlayerCompleteQuest, end of RewardQuest).
 * - Bosses: a dungeon or raid boss killed by a guild group. In a 5-player dungeon 3, 4 or 5 guild
 *   members give 50%, 100% or 125%; a raid needs GuildLevels.XP.RaidGuildMembers of them, at most
 *   80% of the raid. Bosses that are gray to the highest-level member give nothing. A boss is
 *   an encounter of the instance (DungeonEncounter.dbc), paid when the core credits it, once
 *   per instance: a council of several creatures pays once.
 *
 * Only real players earn guild XP. Bots that are members of the guild count toward the size of
 * a guild group, so one player with four guild bots is a full guild group, but a guild with no
 * real player present never earns anything (unless GuildLevels.XP.BotGuildsEarn).
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "Creature.h"
#include "Formulas.h"
#include "GameTime.h"
#include "ObjectMgr.h"
#include "Map.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using namespace GuildLevels;

namespace
{
    // Quest XP seen since the last reward, by (player, quest). RewardQuest computes its quest's
    // XP and fires OnPlayerCompleteQuest in the same call on the same thread; in between, a
    // reward spell can open another quest's window, so this keeps one entry per quest.
    thread_local std::map<std::pair<ObjectGuid::LowType, uint32>, uint32> pendingQuests;

    // Encounters already paid, per instance id; forgotten when the instance is.
    std::mutex bossLock;
    std::unordered_map<uint32, std::unordered_set<uint32>> paidEncounters;

    bool FirstCredit(Map* map, uint32 encounterId)
    {
        std::lock_guard<std::mutex> guard(bossLock);
        return paidEncounters[map->GetInstanceId()].insert(encounterId).second;
    }

    struct GuildPresence
    {
        uint32 members = 0;                             // guild members on the boss's map, bots too
        uint8 highestLevel = 0;
        std::vector<ObjectGuid::LowType> realPlayers;   // who gets the credit
    };

    // Guild XP percentage for a guild group of `members` guild members out of `groupSize`.
    uint32 GuildGroupPercent(Map* map, uint32 members, uint32 groupSize)
    {
        if (map->IsRaid())
        {
            InstanceMap* instance = map->ToInstanceMap();
            uint32 raidSize = std::max<uint32>(groupSize, instance ? instance->GetMaxPlayers() : groupSize);
            uint32 needed = std::min<uint32>(GetConfig().raidGuildMembers, uint32(std::ceil(raidSize * 0.8)));
            return members >= std::max<uint32>(needed, 1) ? 100 : 0;
        }

        if (members >= 5)
            return 125;
        if (members == 4)
            return 100;
        if (members == 3)
            return 50;
        return 0;
    }

    // The instance's players are the group: the guild with the most members among them is the
    // guild group. `boss` is the credited creature, if the credit came from one.
    void RewardEncounter(Map* map, uint32 encounterId, Creature const* boss)
    {
        std::map<uint32, GuildPresence> presence;
        uint32 groupSize = 0;
        for (MapReference const& ref : map->GetPlayers())
        {
            Player* member = ref.GetSource();
            if (!member || member->IsGameMaster())
                continue;

            ++groupSize;
            uint32 guildId = member->GetGuildId();
            if (!guildId)
                continue;

            GuildPresence& p = presence[guildId];
            ++p.members;
            p.highestLevel = std::max(p.highestLevel, member->GetLevel());
            if (IsRealPlayer(member))
                p.realPlayers.push_back(member->GetGUID().GetCounter());
        }

        auto best = std::max_element(presence.begin(), presence.end(),
            [](auto const& a, auto const& b) { return a.second.members < b.second.members; });
        if (best == presence.end())
            return;

        GuildPresence const& p = best->second;
        if (p.realPlayers.empty() && !GetConfig().botGuildsEarn)
            return;
        if (boss && boss->GetLevel() <= Acore::XP::GetGrayLevel(p.highestLevel))
            return;

        uint32 percent = GuildGroupPercent(map, p.members, groupSize);
        if (!percent || !FirstCredit(map, encounterId))
            return;

        AddNews(best->first, NEWS_ENCOUNTER, 0, encounterId, map->GetDifficulty());

        double xp = map->IsRaid() ? GetConfig().bossXPRaid : GetConfig().bossXPDungeon;
        if (map->IsHeroic())
            xp *= GetConfig().heroicBonus;
        xp = xp * percent / 100.0;

        AddXP(best->first, uint64(xp), p.realPlayers);
    }
}

class GuildLevelsXPPlayerScript : public PlayerScript
{
public:
    GuildLevelsXPPlayerScript() : PlayerScript("GuildLevelsXPPlayerScript", {
        PLAYERHOOK_ON_QUEST_COMPUTE_EXP, PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST }) { }

    // Also called to fill in the quest giver's windows: only note the XP here.
    void OnPlayerQuestComputeXP(Player* player, Quest const* quest, uint32& xpValue) override
    {
        if (!player || !quest)
            return;
        if (pendingQuests.size() >= 64)     // windows that never led to a reward
            pendingQuests.clear();
        pendingQuests[{ player->GetGUID().GetCounter(), quest->GetQuestId() }] = xpValue;
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        auto itr = pendingQuests.find({ player->GetGUID().GetCounter(), quest->GetQuestId() });
        uint32 xpValue = itr != pendingQuests.end() ? itr->second : 0;
        pendingQuests.clear();
        if (!xpValue)
            return;
        if (!GetConfig().enabled || !player->GetGuildId())
            return;
        if (!IsRealPlayer(player) && !GetConfig().botGuildsEarn)
            return;

        uint64 xp = uint64(double(xpValue) * GetConfig().questShare);
        AddXP(player->GetGuildId(), xp, { player->GetGUID().GetCounter() });
    }
};

class GuildLevelsXPGlobalScript : public GlobalScript
{
public:
    GuildLevelsXPGlobalScript() : GlobalScript("GuildLevelsXPGlobalScript",
        { GLOBALHOOK_ON_AFTER_UPDATE_ENCOUNTER_STATE, GLOBALHOOK_ON_INSTANCEID_REMOVED }) { }

    // Fires for every creature (or spell) the core credits in an instance; `encounters` is the
    // instance's boss list for its difficulty, so the credit is a boss when it's on it.
    void OnAfterUpdateEncounterState(Map* map, EncounterCreditType type, uint32 creditEntry, Unit* source,
        Difficulty /*difficulty_fixed*/, DungeonEncounterList const* encounters, uint32 /*dungeonCompleted*/, bool /*updated*/) override
    {
        if (!GetConfig().enabled || !map || !map->IsDungeon() || !encounters)
            return;

        for (DungeonEncounter const* encounter : *encounters)
        {
            if (encounter->creditType != type || encounter->creditEntry != creditEntry)
                continue;
            RewardEncounter(map, encounter->dbcEntry->id, source ? source->ToCreature() : nullptr);
            return;
        }
    }

    void OnInstanceIdRemoved(uint32 instanceId) override
    {
        std::lock_guard<std::mutex> guard(bossLock);
        paidEncounters.erase(instanceId);
    }
};

void AddGuildLevelsXPScripts()
{
    new GuildLevelsXPPlayerScript();
    new GuildLevelsXPGlobalScript();
}
