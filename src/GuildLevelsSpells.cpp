/*
 * mod-guild-levels: the three perks you cast.
 *
 * - Mobile Banking (90452): summons a guild chest (gameobject 9503400, a guild bank) for 5
 *   minutes. The spell's own effect does the summoning; the script only checks for a guild.
 * - Have Group, Will Travel (90453): sends every party or raid member a summon to you.
 * - Mass Resurrection (90454): offers every dead party or raid member within 100 yards (them or
 *   their corpse) a resurrection, the same way Resurrection does. A player who accepts can't be
 *   raised by it again for GuildLevels.Perk.MassResurrectionMinutes.
 *
 * The spells are learned and unlearned with the guild level (RefreshPerks). Their rows are in
 * the client's Spell.dbc (tools/build_patch.py) and in spell_dbc.
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "Chat.h"
#include "Corpse.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "WorldPacket.h"
#include <mutex>
#include <unordered_map>

using namespace GuildLevels;

namespace
{
    constexpr float MASS_RES_RANGE = 100.0f;

    SpellCastResult NeedGuild(Unit* caster)
    {
        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player || !player->GetGuildId())
            return SPELL_FAILED_DONT_REPORT;
        if (player->InBattleground())
            return SPELL_FAILED_NOT_HERE;
        return SPELL_CAST_OK;
    }

    template <typename Fn>
    void ForEachOtherMember(Player* caster, Fn&& fn)
    {
        Group* group = caster->GetGroup();
        if (!group)
            return;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* member = itr->GetSource())
                if (member != caster && member->IsInWorld())
                    fn(member);
    }

    // Mass Resurrection: offers waiting for an answer (target -> caster), and when each player
    // last accepted one.
    std::mutex massResLock;
    std::unordered_map<ObjectGuid::LowType, ObjectGuid> massResOffers;
    std::unordered_map<ObjectGuid::LowType, Seconds> lastMassRes;

    bool MassResLockedOut(Player* target)
    {
        Seconds wait = Seconds(GetConfig().massResMinutes * MINUTE);
        std::lock_guard<std::mutex> guard(massResLock);
        auto itr = lastMassRes.find(target->GetGUID().GetCounter());
        return itr != lastMassRes.end() && GameTime::GetGameTime() - itr->second < wait;
    }

    void NoteOffer(Player* target, Player* caster)
    {
        std::lock_guard<std::mutex> guard(massResLock);
        massResOffers[target->GetGUID().GetCounter()] = caster->GetGUID();
    }

    // Same map (so the same map thread), dead, no other offer waiting, and the player or their
    // corpse within range.
    bool CanMassRes(Player* caster, Player* member)
    {
        if (member->IsAlive() || member->FindMap() != caster->GetMap() || member->isResurrectRequested())
            return false;
        if (caster->IsWithinDistInMap(member, MASS_RES_RANGE))
            return true;
        Corpse* corpse = member->GetCorpse();
        return corpse && corpse->FindMap() == caster->GetMap() && caster->IsWithinDistInMap(corpse, MASS_RES_RANGE);
    }
}

class spell_guild_levels_mobile_banking : public SpellScript
{
    PrepareSpellScript(spell_guild_levels_mobile_banking);

    SpellCastResult CheckCast()
    {
        SpellCastResult result = NeedGuild(GetCaster());
        if (result == SPELL_FAILED_DONT_REPORT)
            if (Player* player = GetCaster()->ToPlayer())
                ChatHandler(player->GetSession()).SendNotification("You are not in a guild.");
        return result;
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_guild_levels_mobile_banking::CheckCast);
    }
};

class spell_guild_levels_have_group : public SpellScript
{
    PrepareSpellScript(spell_guild_levels_have_group);

    SpellCastResult CheckCast()
    {
        Player* caster = GetCaster()->ToPlayer();
        if (!caster || !caster->GetGroup())
            return SPELL_FAILED_TARGET_NOT_IN_PARTY;
        if (caster->GetMap()->Instanceable())
            return SPELL_FAILED_NOT_HERE;
        return SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* caster = GetCaster()->ToPlayer();
        if (!caster)
            return;

        uint32 sent = 0;
        ForEachOtherMember(caster, [caster, &sent](Player* member)
        {
            // The member may be on another map, updated by another thread: plain field reads only.
            if (!member->IsAlive() || member->IsInCombat() || member->IsBeingTeleported() || member->IsInFlight())
                return;
            if (member->InBattleground())
                return;

            member->SetSummonPoint(caster->GetMapId(), caster->GetPositionX(), caster->GetPositionY(), caster->GetPositionZ());

            WorldPacket data(SMSG_SUMMON_REQUEST, 8 + 4 + 4);
            data << caster->GetGUID();
            data << uint32(caster->GetZoneId());
            data << uint32(MAX_PLAYER_SUMMON_DELAY * IN_MILLISECONDS);
            member->SendDirectMessage(&data);
            ++sent;
        });

        ChatHandler(caster->GetSession()).PSendSysMessage("Have Group, Will Travel: summoning {} group member{}.", sent, sent == 1 ? "" : "s");
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_guild_levels_have_group::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_guild_levels_have_group::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_guild_levels_mass_resurrection : public SpellScript
{
    PrepareSpellScript(spell_guild_levels_mass_resurrection);

    SpellCastResult CheckCast()
    {
        Player* caster = GetCaster()->ToPlayer();
        if (!caster || !caster->GetGroup())
            return SPELL_FAILED_TARGET_NOT_IN_PARTY;
        if (caster->InBattleground())
            return SPELL_FAILED_NOT_HERE;

        bool anyone = false;
        ForEachOtherMember(caster, [caster, &anyone](Player* member)
        {
            anyone |= CanMassRes(caster, member) && !MassResLockedOut(member);
        });
        return anyone ? SPELL_CAST_OK : SPELL_FAILED_NO_VALID_TARGETS;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* caster = GetCaster()->ToPlayer();
        if (!caster)
            return;

        uint32 pct = GetConfig().massResHealth;
        ForEachOtherMember(caster, [caster, pct](Player* member)
        {
            if (!CanMassRes(caster, member) || MassResLockedOut(member))
                return;

            uint32 health = member->CountPctFromMaxHealth(int32(pct));
            uint32 mana = CalculatePct(member->GetMaxPower(POWER_MANA), pct);
            member->setResurrectRequestData(caster->GetGUID(), caster->GetMapId(), caster->GetPositionX(),
                caster->GetPositionY(), caster->GetPositionZ(), health, mana);

            // As Spell::SendResurrectRequest does for a player caster: the client looks up the name.
            WorldPacket data(SMSG_RESURRECT_REQUEST, 8 + 4 + 1 + 1 + 1);
            data << caster->GetGUID();
            data << uint32(1);
            data << std::string_view();
            data << uint8(0);
            data << uint8(0);
            member->SendDirectMessage(&data);
            NoteOffer(member, caster);
        });
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_guild_levels_mass_resurrection::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_guild_levels_mass_resurrection::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

namespace GuildLevels
{
    void MassResurrectionAccepted(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::lock_guard<std::mutex> guard(massResLock);
        auto itr = massResOffers.find(guid);
        if (itr == massResOffers.end())
            return;
        if (player->isResurrectRequestedBy(itr->second))
            lastMassRes[guid] = GameTime::GetGameTime();
        massResOffers.erase(itr);
    }
}

void AddGuildLevelsSpellScripts()
{
    RegisterSpellScript(spell_guild_levels_mobile_banking);
    RegisterSpellScript(spell_guild_levels_have_group);
    RegisterSpellScript(spell_guild_levels_mass_resurrection);
}
