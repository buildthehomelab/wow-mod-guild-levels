/*
 * mod-guild-levels: guild reputation.
 *
 * As in Cataclysm, a member earns reputation with their guild from the guild XP they bring in
 * themself: GuildLevels.Rep.XPPerRep guild XP is one point (Cataclysm: 360, but this module's
 * guild XP is far smaller). It counts what the member earned, not what the guild kept, so rep
 * still comes once the guild is level 25 or has hit its daily cap. Each member can earn
 * GuildLevels.Rep.WeeklyCap a week (Cataclysm 4.2: 4,375), weeks starting on Tuesday.
 *
 * The rep is a real faction (Faction.dbc 1168 "Your Guild", under the "Guild" header 1169, both
 * in the client patch and faction_dbc), so the core keeps the standing, saves it with the
 * character and shows it in the reputation pane. The module only remembers which guild it was
 * earned in: in another guild it starts over at Neutral, and while the member is in no guild it
 * waits under Inactive, back where it was if they rejoin the same guild.
 *
 * The rep is set outright, not added through the core's reputation gain, so the realm's
 * Rate.Reputation.Gain leaves it alone: the weekly cap is what a member really gets.
 *
 * Everything here runs on the player's own map thread (their update, a quest, a boss kill, a
 * purchase) or the world thread at login.
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "DataMap.h"
#include "DBCStores.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include <algorithm>

using namespace GuildLevels;

namespace
{
    // The guild this player was in at the last check. Only a change touches the rep pane, so a
    // member can still mark the faction inactive themself.
    constexpr uint32 NOT_CHECKED = 0xFFFFFFFF;
    class RepSeen : public DataMap::Base
    {
    public:
        uint32 lastGuild = NOT_CHECKED;
    };
    constexpr char const* REP_SEEN_KEY = "mod-guild-levels-rep";

    FactionEntry const* GuildFaction()
    {
        FactionEntry const* faction = sFactionStore.LookupEntry(GetConfig().repFaction);
        return faction && faction->CanHaveReputation() ? faction : nullptr;
    }

    void SetInactive(Player* player, FactionEntry const* faction, bool inactive)
    {
        ReputationMgr& reps = player->GetReputationMgr();
        FactionState const* state = reps.GetState(faction);
        if (!state || !(state->Flags & FACTION_FLAG_VISIBLE))
            return;
        if (bool(state->Flags & FACTION_FLAG_INACTIVE) != inactive)
            reps.SetInactive(faction->reputationListID, inactive);
    }
}

namespace GuildLevels
{
    void RefreshGuildRep(Player* player)
    {
        if (!GetConfig().repEnabled || !IsRealPlayer(player))
            return;
        FactionEntry const* faction = GuildFaction();
        if (!faction)
            return;

        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        uint32 guildId = player->GetGuildId();
        RepSeen* seen = player->CustomData.GetDefault<RepSeen>(REP_SEEN_KEY);
        if (seen->lastGuild == guildId)
            return;
        bool wasGuildless = seen->lastGuild == 0;
        seen->lastGuild = guildId;

        if (!guildId)
        {
            SetInactive(player, faction, true);
            return;
        }

        uint32 repGuild = GetRepGuild(guid);
        if (repGuild != guildId)
        {
            // A different guild than the one the rep was earned in: start over. No record yet
            // (first login with the module) keeps whatever the character has.
            if (repGuild)
                player->GetReputationMgr().SetReputation(faction, 0.0f);
            SetRepGuild(guid, guildId);
            SetInactive(player, faction, false);
        }
        else if (wasGuildless)
            SetInactive(player, faction, false);    // back in the guild the rep is from
    }

    void GainGuildRep(Player* player, uint64 guildXp)
    {
        if (!GetConfig().enabled || !GetConfig().repEnabled || !guildXp || !player->GetGuildId() || !IsRealPlayer(player))
            return;
        FactionEntry const* faction = GuildFaction();
        if (!faction)
            return;

        RefreshGuildRep(player);

        // Rep past Exalted would only use up the week.
        ReputationMgr& reps = player->GetReputationMgr();
        int32 standing = reps.GetReputation(faction);
        uint32 room = standing < ReputationMgr::Reputation_Cap ? uint32(ReputationMgr::Reputation_Cap - standing) : 0;

        if (uint32 rep = TakeRep(player->GetGUID().GetCounter(), guildXp, room))
            reps.SetReputation(faction, float(standing + int32(rep)));
    }

    RepInfo GetRepInfo(Player* player)
    {
        RepInfo info;
        if (FactionEntry const* faction = GuildFaction())
        {
            info.standing = player->GetReputationMgr().GetReputation(faction);
            info.rank = uint8(player->GetReputationMgr().GetRank(faction));
        }
        info.weekRep = GetWeekRep(player->GetGUID().GetCounter());
        return info;
    }

    uint8 GetRepRank(Player const* player)
    {
        FactionEntry const* faction = GuildFaction();
        return faction ? uint8(player->GetReputationMgr().GetRank(faction)) : uint8(REP_NEUTRAL);
    }
}

class GuildLevelsRepPlayerScript : public PlayerScript
{
public:
    GuildLevelsRepPlayerScript() : PlayerScript("GuildLevelsRepPlayerScript", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_DELETE }) { }

    void OnPlayerLogin(Player* player) override
    {
        RefreshGuildRep(player);
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        ForgetRep(guid.GetCounter());
    }
};

void AddGuildLevelsRepScripts()
{
    new GuildLevelsRepPlayerScript();
}
