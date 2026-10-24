/*
 * mod-guild-levels: the passive guild perks.
 *
 * Most perks are a hook that checks the player's guild level when it fires. Two are hidden auras
 * (Mount Up, The Quick and the Dead's ghost speed) and three are spells the player learns
 * (GuildLevelsSpells.cpp). A player's auras and spells are brought in line with their guild's
 * level from their own map update every two seconds, which also covers joining, leaving and the
 * guild levelling up, without touching a player from another thread.
 *
 * Changes from Cataclysm, for spells and systems 3.3.5 doesn't have:
 * - Ride Like the Wind (Mists of Pandaria) replaces Hasty Hearth: flight paths are faster. Needs
 *   the core's OnPlayerTaxiFlightSpeed hook; without it the perk does nothing.
 * - The Doctor Is In (Mists of Pandaria) replaces Guild Mail: healing potions and healthstones
 *   heal more.
 * - For Great Justice: a hidden aura (90455) whose amount is a chance of one more boss coin per
 *   boss award, instead of Justice Points. mod-boss-coins pays it when its
 *   BossCoins.BossKills.BonusAura is set to 90455; this module doesn't touch coins itself.
 * - Chug-A-Lug: Well Fed, flask and elixir buffs last longer (3.3.5 has no guild cauldrons).
 * - Happy Hour: a chance of one extra flask or elixir when you make one.
 * - Honorable Mention adds to honor from honorable kills; battleground bonus honor has no hook.
 * - Working Overtime adds a second skill-up roll worth a tenth of the recipe's own chance.
 * - Bartering lowers prices at vendors (items and repairs), not at trainers or flight masters.
 * - Bountiful Bags: herb and ore nodes, skinning and disenchanting, trade goods and gems only.
 * - Reinforce gives back part of the durability lost between a killing blow and resurrection
 *   (death and the spirit healer), since the core has no hook on durability loss.
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "Bag.h"
#include "Creature.h"
#include "DataMap.h"
#include "GameObject.h"
#include "Group.h"
#include "Guild.h"
#include "Item.h"
#include "LootMgr.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GuildPackets.h"
#include "Log.h"
#include "Util.h"
#include "ObjectMgr.h"
#include <functional>
#include <unordered_set>

using namespace GuildLevels;

namespace
{
    constexpr uint32 REFRESH_MS = 2000;

    // Castable perks: the level they come with and the spell.
    constexpr std::array<std::pair<uint8, uint32>, 3> PERK_SPELLS =
    {{
        { PERK_MOBILE_BANKING, SPELL_MOBILE_BANKING },
        { PERK_HAVE_GROUP_WILL_TRAVEL, SPELL_HAVE_GROUP },
        { PERK_MASS_RESURRECTION, SPELL_MASS_RESURRECTION },
    }};

    constexpr Seconds SNAPSHOT_STALE = 3s;          // Reinforce: a killing blow that didn't kill

    // Per player. Touched from that player's map update, or from the world thread, which never
    // runs alongside the map updates.
    struct PerkData : public DataMap::Base
    {
        uint32 timer = 0;
        uint32 guildId = 0;
        uint8 level = 0;
        bool known = false;

        // Reinforce: durability of every item at the killing blow, given back at resurrection.
        std::vector<std::pair<ObjectGuid, uint32>> snapshot;
        Seconds snapshotAt = 0s;

        // The Quick and the Dead: raised by a guild member, top up once the health is set.
        bool raised = false;
    };

    PerkData* DataOf(Player* player)
    {
        return player->CustomData.GetDefault<PerkData>("GuildLevels");
    }

    uint32 Pick(Player const* player, uint8 level2, uint32 value2, uint8 level1, uint32 value1)
    {
        uint8 level = GetLevel(player->GetGuildId());
        if (level >= level2)
            return value2;
        if (level >= level1)
            return value1;
        return 0;
    }

    // Guard so items the perks hand out don't trigger the perks again.
    thread_local bool granting = false;

    void GrantExtra(Player* player, uint32 entry, char const* perk)
    {
        if (granting)
            return;
        granting = true;
        if (player->AddItem(entry, 1))
            ChatHandler(player->GetSession()).PSendSysMessage("|cff40c040{}:|r you get one more.", perk);
        granting = false;
    }

    void ForEachItem(Player* player, std::function<void(Item*)> const& fn)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                fn(item);
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = player->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        fn(item);
    }

    void SetAuraAmounts(Aura* aura, int32 amount)
    {
        if (!aura)
            return;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (AuraEffect* effect = aura->GetEffect(i))
                if (effect->GetAmount() != amount)
                    effect->ChangeAmount(amount);
    }

    // Adds or removes a hidden aura so the player has it exactly when `want`.
    void Keep(Player* player, uint32 spellId, bool want, int32 amount)
    {
        Aura* aura = player->GetAura(spellId);
        if (want && !aura)
            SetAuraAmounts(player->AddAura(spellId, player), amount);
        else if (want)
            SetAuraAmounts(aura, amount);
        else if (aura)
            player->RemoveAurasDueToSpell(spellId);
    }

    bool IsWellFed(SpellInfo const* spell)
    {
        char const* name = spell->SpellName[0];
        return name && std::string_view(name) == "Well Fed";
    }

    bool IsFlaskOrElixir(SpellInfo const* spell)
    {
        return sSpellMgr->IsSpellMemberOfSpellGroup(spell->Id, SPELL_GROUP_ELIXIR_BATTLE)
            || sSpellMgr->IsSpellMemberOfSpellGroup(spell->Id, SPELL_GROUP_ELIXIR_GUARDIAN);
    }

    // The Doctor Is In: on-use heal spells of potions and healthstones.
    std::unordered_set<uint32> healingConsumables;

    // A herb or ore node: its lock needs Herbalism or Mining. (Every lock-opened gameobject,
    // treasure chests too, loots as LOOT_SKINNING.)
    bool IsGatheringNode(GameObject const* go)
    {
        LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
        if (!lock)
            return false;
        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
            if (lock->Type[i] == LOCK_KEY_SKILL && (lock->Index[i] == LOCKTYPE_HERBALISM || lock->Index[i] == LOCKTYPE_MINING))
                return true;
        return false;
    }

    // Bountiful Bags: was this loot gathered (node, skinning) or disenchanted?
    bool IsGathered(Player* player, ObjectGuid guid)
    {
        if (guid.IsGameObject())
        {
            GameObject* go = player->GetMap()->GetGameObject(guid);
            return go && go->loot.loot_type == LOOT_SKINNING && IsGatheringNode(go);
        }
        if (guid.IsCreatureOrVehicle())
        {
            Creature* creature = player->GetMap()->GetCreature(guid);
            return creature && creature->loot.loot_type == LOOT_SKINNING;
        }
        if (guid.IsItem())
        {
            Item* item = player->GetItemByGuid(guid);
            return item && item->loot.loot_type == LOOT_DISENCHANTING;
        }
        return false;
    }

    // Whether a resurrection came from a request (a spell, Mass Resurrection) and not the
    // corpse or the spirit healer: ResurectUsingRequestData calls ResurrectPlayer(0, false).
    // A soulstone looks the same, so the request also has to be from the right player.
    bool FromRequest(float restorePercent, bool applySickness)
    {
        return restorePercent == 0.0f && !applySickness;
    }
}

namespace GuildLevels
{
    void RefreshPerks(Player* player, bool force)
    {
        PerkData* data = DataOf(player);
        uint32 guildId = player->GetGuildId();
        uint8 level = GetLevel(guildId);
        bool changed = !data->known || data->guildId != guildId || data->level != level;
        if (!changed && !force)
            return;

        Config const& cfg = GetConfig();
        bool pvpMap = player->FindMap() && player->FindMap()->IsBattlegroundOrArena();

        Keep(player, SPELL_MOUNT_UP, level >= PERK_MOUNT_UP && !pvpMap, int32(cfg.mountUp));
        Keep(player, SPELL_QUICK_AND_DEAD, level >= PERK_QUICK_AND_DEAD && !pvpMap
            && player->HasPlayerFlag(PLAYER_FLAGS_GHOST), int32(cfg.quickAndDeadSpeed));
        Keep(player, SPELL_FOR_GREAT_JUSTICE, level >= PERK_FOR_GREAT_JUSTICE && cfg.forGreatJusticeChance,
            int32(cfg.forGreatJusticeChance));

        for (auto const& [perkLevel, spellId] : PERK_SPELLS)
        {
            bool has = player->HasSpell(spellId);
            if (level >= perkLevel && !has)
                player->learnSpell(spellId);
            else if (level < perkLevel && has)
                player->removeSpell(spellId, SPEC_MASK_ALL, false);
        }

        // Not from 0: that is the module being switched back on, not the guild levelling up.
        bool levelUp = data->known && data->guildId == guildId && data->level && level > data->level;
        data->known = true;
        data->guildId = guildId;
        data->level = level;

        if (changed && IsRealPlayer(player))
        {
            SendState(player);
            if (levelUp)
                SendAddon(player, Acore::StringFormat("UP:{}:{}", level, PerkName(level)));
        }
    }

    std::vector<PerkInfo> PerkList()
    {
        Config const& c = GetConfig();
        auto f = [](char const* text, auto... args) { return Acore::StringFormat(text, args...); };
        return
        {
            { PERK_FAST_TRACK_1, "Fast Track", f("Experience from killing monsters and completing quests increased by {}%.", c.fastTrack1) },
            { PERK_MOUNT_UP, "Mount Up", f("Mounted and flying speed increased by {}%, also in Travel Form, the flight forms and Ghost Wolf where a mount could be used. Not active in battlegrounds and arenas.", c.mountUp) },
            { PERK_MR_POPULARITY_1, "Mr. Popularity", f("Reputation from killing monsters and completing quests increased by {}%.", c.mrPopularity1) },
            { PERK_CASH_FLOW_1, "Cash Flow", f("Each time you loot money from an enemy, an extra {}% is deposited into your guild bank.", c.cashFlow1) },
            { PERK_FAST_TRACK_2, "Fast Track (Rank 2)", f("Experience from killing monsters and completing quests increased by {}%.", c.fastTrack2) },
            { PERK_REINFORCE_1, "Reinforce", f("Items lose {}% less durability when you die.", c.reinforce1) },
            { PERK_RIDE_LIKE_THE_WIND, "Ride Like the Wind", f("Flight paths are {}% faster.", c.rideLikeTheWind) },
            { PERK_REINFORCE_2, "Reinforce (Rank 2)", f("Items lose {}% less durability when you die.", c.reinforce2) },
            { PERK_CHUG_A_LUG_1, "Chug-A-Lug", f("Well Fed, flask and elixir buffs last {}% longer.", c.chugALug1) },
            { PERK_MOBILE_BANKING, "Mobile Banking", f("Teaches Mobile Banking: summons a guild chest for 5 min that opens your guild bank. 1 hour cooldown.") },
            { PERK_MR_POPULARITY_2, "Mr. Popularity (Rank 2)", f("Reputation from killing monsters and completing quests increased by {}%.", c.mrPopularity2) },
            { PERK_HONORABLE_MENTION_1, "Honorable Mention", f("Honor from honorable kills increased by {}%.", c.honorableMention1) },
            { PERK_WORKING_OVERTIME, "Working Overtime", f("Chance of a skill-up when crafting increased by {}%.", c.workingOvertime) },
            { PERK_QUICK_AND_DEAD, "The Quick and the Dead", f("Health and mana when a guild member resurrects you increased by {}%, and you move {}% faster as a ghost. Not active in battlegrounds and arenas.", c.quickAndDeadRes, c.quickAndDeadSpeed) },
            { PERK_CASH_FLOW_2, "Cash Flow (Rank 2)", f("Each time you loot money from an enemy, an extra {}% is deposited into your guild bank.", c.cashFlow2) },
            { PERK_DOCTOR_IS_IN, "The Doctor Is In", f("Healing potions and healthstones heal {}% more.", c.doctorIsIn) },
            { PERK_FOR_GREAT_JUSTICE, "For Great Justice", f("When a boss pays you boss coins, a {}% chance of one more.", c.forGreatJusticeChance) },
            { PERK_HONORABLE_MENTION_2, "Honorable Mention (Rank 2)", f("Honor from honorable kills increased by {}%.", c.honorableMention2) },
            { PERK_HAPPY_HOUR, "Happy Hour", f("A {}% chance of an extra flask or elixir when you make one.", c.happyHourChance) },
            { PERK_HAVE_GROUP_WILL_TRAVEL, "Have Group, Will Travel", f("Teaches Have Group, Will Travel: summons your party or raid to you. Outdoors only, 2 hour cooldown.") },
            { PERK_CHUG_A_LUG_2, "Chug-A-Lug (Rank 2)", f("Well Fed, flask and elixir buffs last {}% longer.", c.chugALug2) },
            { PERK_BOUNTIFUL_BAGS, "Bountiful Bags", f("A {}% chance of an extra trade good or gem from Mining, Herbalism, Skinning and Disenchanting.", c.bountifulBagsChance) },
            { PERK_BARTERING, "Bartering", f("Prices and repairs at vendors reduced by {}%.", c.bartering) },
            { PERK_MASS_RESURRECTION, "Mass Resurrection", f("Teaches Mass Resurrection: brings all dead party and raid members back to life with {}% health and mana.", c.massResHealth) },
        };
    }

    void LoadHealingConsumables()
    {
        healingConsumables.clear();
        for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
        {
            if (proto.Class != ITEM_CLASS_CONSUMABLE)
                continue;
            bool healthstone = proto.Name1.find("Healthstone") != std::string::npos;
            if (proto.SubClass != ITEM_SUBCLASS_POTION && !healthstone)
                continue;
            for (auto const& itemSpell : proto.Spells)
            {
                if (itemSpell.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE || itemSpell.SpellId <= 0)
                    continue;
                SpellInfo const* spell = sSpellMgr->GetSpellInfo(uint32(itemSpell.SpellId));
                if (spell && spell->HasEffect(SPELL_EFFECT_HEAL))
                    healingConsumables.insert(spell->Id);
            }
        }
        LOG_INFO("module", "mod-guild-levels: {} healing potion and healthstone spells", healingConsumables.size());
    }

    std::string PerkName(uint8 level)
    {
        for (PerkInfo const& perk : PerkList())
            if (perk.level == level)
                return perk.name;
        return {};
    }
}

class GuildLevelsPerksPlayerScript : public PlayerScript
{
public:
    GuildLevelsPerksPlayerScript() : PlayerScript("GuildLevelsPerksPlayerScript", {
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_GIVE_REPUTATION,
        PLAYERHOOK_ON_BEFORE_LOOT_MONEY,
        PLAYERHOOK_ON_PLAYER_RELEASED_GHOST,
        PLAYERHOOK_ON_PLAYER_RESURRECT,
        PLAYERHOOK_ON_VICTIM_REWARD_AFTER,
        PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL,
        PLAYERHOOK_ON_CREATE_ITEM,
        PLAYERHOOK_ON_LOOT_ITEM,
        PLAYERHOOK_ON_GET_REPUTATION_PRICE_DISCOUNT,
#ifdef GL_CORE_HAS_TAXI_SPEED
        PLAYERHOOK_ON_TAXI_FLIGHT_SPEED,
#endif
    }) { }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!GetConfig().enabled)
            return;

        PerkData* data = DataOf(player);

        // The Quick and the Dead: the health ResurectUsingRequestData set, half again.
        if (data->raised && player->IsAlive())
        {
            data->raised = false;
            uint32 pct = 100 + GetConfig().quickAndDeadRes;
            player->SetHealth(std::min<uint32>(player->GetMaxHealth(), uint32(uint64(player->GetHealth()) * pct / 100)));
            if (player->GetMaxPower(POWER_MANA))
                player->SetPower(POWER_MANA, std::min<uint32>(player->GetMaxPower(POWER_MANA), uint32(uint64(player->GetPower(POWER_MANA)) * pct / 100)));
        }

        // Reinforce: a killing blow the player survived (a duel) leaves a snapshot behind.
        if (!data->snapshot.empty() && player->IsAlive() && GameTime::GetGameTime() - data->snapshotAt > SNAPSHOT_STALE)
            data->snapshot.clear();

        if (data->timer > diff)
        {
            data->timer -= diff;
            return;
        }
        data->timer = REFRESH_MS;
        // Every tick also re-checks the auras, which some core paths strip (arena entry, etc.).
        RefreshPerks(player, true);
        // and the guild rep: a new guild starts it over, no guild puts it away.
        RefreshGuildRep(player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (GetConfig().enabled)
            RefreshPerks(player, true);
    }

    // Fast Track
    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 xpSource) override
    {
        if (xpSource != XPSOURCE_KILL && xpSource != XPSOURCE_QUEST && xpSource != XPSOURCE_QUEST_DF)
            return;
        if (uint32 pct = Pick(player, PERK_FAST_TRACK_2, GetConfig().fastTrack2, PERK_FAST_TRACK_1, GetConfig().fastTrack1))
            amount += amount * pct / 100;
    }

    // Mr. Popularity
    void OnPlayerGiveReputation(Player* player, int32 /*factionID*/, float& amount, ReputationSource repSource) override
    {
        if (amount <= 0.0f || repSource == REPUTATION_SOURCE_SPELL)
            return;
        if (uint32 pct = Pick(player, PERK_MR_POPULARITY_2, GetConfig().mrPopularity2, PERK_MR_POPULARITY_1, GetConfig().mrPopularity1))
            amount += amount * pct / 100.0f;
    }

    // Cash Flow. CMSG_LOOT_MONEY is handled on the world thread, so the guild bank is safe here.
    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!loot || !loot->gold || !loot->sourceWorldObjectGUID.IsCreatureOrVehicle())
            return;
        uint32 pct = Pick(player, PERK_CASH_FLOW_2, GetConfig().cashFlow2, PERK_CASH_FLOW_1, GetConfig().cashFlow1);
        if (!pct)
            return;
        Guild* guild = player->GetGuild();
        if (!guild)
            return;

        uint64 extra = uint64(loot->gold) * pct / 100;
        if (!extra || guild->GetTotalBankMoney() + extra > GUILD_BANK_MONEY_LIMIT)
            return;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (!guild->ModifyBankMoney(trans, extra, true))
            return;
        CharacterDatabase.CommitTransaction(trans);
        AddCashFlow(guild->GetId(), player->GetGUID().GetCounter(), extra);

        // ModifyBankMoney neither logs nor tells anyone; this refreshes open guild bank windows
        // the way a deposit does.
        uint64 total = guild->GetTotalBankMoney();
        WorldPackets::Guild::GuildEvent event;
        event.Type = GE_BANK_MONEY_SET;
        event.Params.push_back(Acore::Impl::ByteArrayToHexStr(reinterpret_cast<uint8 const*>(&total), 8, true));
        guild->BroadcastPacket(event.Write());
    }

    // The Quick and the Dead: ghost speed.
    void OnPlayerReleasedGhost(Player* player) override
    {
        if (GetConfig().enabled)
            RefreshPerks(player, true);
    }

    void OnPlayerResurrect(Player* player, float restorePercent, bool& applySickness) override
    {
        if (!GetConfig().enabled)
            return;

        player->RemoveAurasDueToSpell(SPELL_QUICK_AND_DEAD);
        PerkData* data = DataOf(player);
        bool fromRequest = FromRequest(restorePercent, applySickness);
        if (fromRequest)
            MassResurrectionAccepted(player);

        // Reinforce
        std::vector<std::pair<ObjectGuid, uint32>> before;
        before.swap(data->snapshot);
        if (uint32 pct = Pick(player, PERK_REINFORCE_2, GetConfig().reinforce2, PERK_REINFORCE_1, GetConfig().reinforce1))
        {
            for (auto const& [itemGuid, durability] : before)
            {
                Item* item = player->GetItemByGuid(itemGuid);
                if (!item)
                    continue;
                uint32 now = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
                if (now < durability)
                    if (int32 back = int32((durability - now) * pct / 100))
                        player->DurabilityPointsLoss(item, -back);
            }
        }

        // The Quick and the Dead: raised by a spell from a guild member of the group.
        if (!fromRequest || !player->isResurrectRequested() || !HasPerk(player, PERK_QUICK_AND_DEAD))
            return;
        if (player->InBattleground())
            return;
        Group* group = player->GetGroup();
        if (!group)
            return;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (member && member != player && member->GetGuildId() == player->GetGuildId()
                && player->isResurrectRequestedBy(member->GetGUID()))
            {
                data->raised = true;
                break;
            }
        }
    }

    // Honorable Mention
    void OnPlayerVictimRewardAfter(Player* player, Player* /*victim*/, uint32& /*killer_title*/, int32& /*victim_rank*/, float& honor_f) override
    {
        if (uint32 pct = Pick(player, PERK_HONORABLE_MENTION_2, GetConfig().honorableMention2, PERK_HONORABLE_MENTION_1, GetConfig().honorableMention1))
            honor_f += honor_f * pct / 100.0f;
    }

    // Working Overtime: a second roll, a tenth as likely as the recipe's own (100% at orange,
    // falling to 0 at gray). The core's roll comes after this one, so both can land.
    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* skill, uint32 currentLevel, uint32& gain) override
    {
        if (!gain || !HasPerk(player, PERK_WORKING_OVERTIME))
            return;
        // TrivialSkillLineRankHigh is where the recipe turns gray, RankLow where it turns yellow
        // (Player::UpdateCraftSkill).
        uint32 gray = skill->TrivialSkillLineRankHigh;
        uint32 yellow = skill->TrivialSkillLineRankLow;
        if (currentLevel >= gray || gray <= yellow)
            return;
        uint32 base = currentLevel < yellow ? 100 : 100 * (gray - currentLevel) / (gray - yellow);
        uint32 chance = base * GetConfig().workingOvertime / 100;
        if (chance && roll_chance_i(int32(chance)))
            player->UpdateSkillPro(skill->SkillLine, 1000, gain);
    }

    // Happy Hour
    void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (granting || proto->Class != ITEM_CLASS_CONSUMABLE)
            return;
        if (proto->SubClass != ITEM_SUBCLASS_FLASK && proto->SubClass != ITEM_SUBCLASS_ELIXIR)
            return;
        if (HasPerk(player, PERK_HAPPY_HOUR) && roll_chance_i(int32(GetConfig().happyHourChance)))
            GrantExtra(player, item->GetEntry(), "Happy Hour");
    }

    // Bountiful Bags
    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid lootguid) override
    {
        if (granting || !item || !HasPerk(player, PERK_BOUNTIFUL_BAGS))
            return;
        ItemTemplate const* proto = item->GetTemplate();
        if (proto->Class != ITEM_CLASS_TRADE_GOODS && proto->Class != ITEM_CLASS_GEM)
            return;
        if (proto->StartQuest || proto->Bonding == BIND_QUEST_ITEM || !IsGathered(player, lootguid))
            return;
        if (roll_chance_i(int32(GetConfig().bountifulBagsChance)))
            GrantExtra(player, item->GetEntry(), "Bountiful Bags");
    }

    // Bartering. Only the Creature overload: it calls the FactionTemplate one first, so taking
    // both would count twice. Vendors only: the same discount also prices trainers and flights.
    void OnPlayerGetReputationPriceDiscount(Player const* player, Creature const* creature, float& discount) override
    {
        if (creature && creature->HasNpcFlag(UNIT_NPC_FLAG_VENDOR) && HasPerk(player, PERK_BARTERING))
            discount *= 1.0f - GetConfig().bartering / 100.0f;
    }

#ifdef GL_CORE_HAS_TAXI_SPEED
    // Ride Like the Wind
    void OnPlayerTaxiFlightSpeed(Player* player, float& speed) override
    {
        if (HasPerk(player, PERK_RIDE_LIKE_THE_WIND))
            speed *= 1.0f + GetConfig().rideLikeTheWind / 100.0f;
    }
#endif
};

class GuildLevelsPerksUnitScript : public UnitScript
{
public:
    GuildLevelsPerksUnitScript() : UnitScript("GuildLevelsPerksUnitScript", true, { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_HEAL_RECEIVED }) { }

    // The Doctor Is In. A potion heals whoever drinks it, so healer and target are the same player
    // whichever order a core passes them in.
    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* spellInfo) override
    {
        if (!spellInfo || !heal || target != healer || !target || !target->IsPlayer())
            return;
        if (!healingConsumables.count(spellInfo->Id) || !HasPerk(target->ToPlayer(), PERK_DOCTOR_IS_IN))
            return;
        heal += uint32(uint64(heal) * GetConfig().doctorIsIn / 100);
    }

    // Reinforce: what everything was worth at the killing blow.
    void OnDamage(Unit* /*attacker*/, Unit* victim, uint32& damage) override
    {
        if (!victim || !victim->IsPlayer() || damage < victim->GetHealth() || !victim->IsAlive())
            return;
        Player* player = victim->ToPlayer();
        if (!HasPerk(player, PERK_REINFORCE_1))
            return;

        PerkData* data = DataOf(player);
        data->snapshot.clear();
        data->snapshotAt = GameTime::GetGameTime();
        ForEachItem(player, [data](Item* item)
        {
            if (item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
                data->snapshot.emplace_back(item->GetGUID(), item->GetUInt32Value(ITEM_FIELD_DURABILITY));
        });
    }
};

// Chug-A-Lug
class GuildLevelsPerksSpellScript : public AllSpellScript
{
public:
    GuildLevelsPerksSpellScript() : AllSpellScript("GuildLevelsPerksSpellScript", { ALLSPELLHOOK_ON_CALC_MAX_DURATION }) { }

    void OnCalcMaxDuration(Aura const* aura, int32& maxDuration) override
    {
        if (maxDuration <= 0 || !GetConfig().enabled)
            return;
        Player* player = aura->GetOwner() ? aura->GetOwner()->ToPlayer() : nullptr;
        if (!player)
            return;
        SpellInfo const* spell = aura->GetSpellInfo();
        if (!IsWellFed(spell) && !IsFlaskOrElixir(spell))
            return;
        if (uint32 pct = Pick(player, PERK_CHUG_A_LUG_2, GetConfig().chugALug2, PERK_CHUG_A_LUG_1, GetConfig().chugALug1))
            maxDuration = int32(std::min<int64>(int64(maxDuration) * (100 + pct) / 100, INT32_MAX));
    }
};

void AddGuildLevelsPerksScripts()
{
    new GuildLevelsPerksPlayerScript();
    new GuildLevelsPerksUnitScript();
    new GuildLevelsPerksSpellScript();
}
