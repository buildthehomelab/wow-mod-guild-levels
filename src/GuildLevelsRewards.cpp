/*
 * mod-guild-levels: the guild vendors and the rewards they sell.
 *
 * Every reward (world table mod_guild_levels_reward) needs a guild level, a standing with the
 * guild (GuildLevelsRep.cpp) and gold. The vendors are creatures with the ScriptName
 * npc_guild_levels_vendor, one in each capital. Talking to one opens a vendor window that this
 * module fills itself, because its prices are its own (heirlooms have no gold price at all) and
 * each player sees their own locks: everything is listed, and a reward they can't buy yet shows
 * as sold out (dark red, 0 in stock). Buying goes through the core's vendor code up to its
 * OnPlayerBeforeBuyItemFromVendor hook, where this module takes over the purchase.
 *
 * The rewards with scripts:
 * - Renowned and Illustrious Guild Tabard (90456, 90457): the guild tabard's look, so it shows
 *   the guild's emblem, and a 10 second teleport to Stormwind or Orgrimmar (8 h / 2 h).
 * - Guild Battle Standard, ranks 1-3 (items 90458-90460): plants a standard (creatures
 *   9503410-9503412) for 15 minutes whose area aura (90461-90463) raises the maximum health of
 *   the planter's guild members within 45 yards.
 * - Cauldron of Battle (90464, 90465): a spellcaster gameobject giving Flask of Battle (90466);
 *   someone who already has the flask can't take another, so one player can't drain it.
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "Chat.h"
#include "Creature.h"
#include "DataMap.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace GuildLevels;

namespace
{
    constexpr char const* VENDOR_SCRIPT = "npc_guild_levels_vendor";

    // The battle standards' looks, by the planter's faction: the stock Alliance and Horde
    // Battle Standards' (creatures 14465 and 14466).
    constexpr uint32 DISPLAY_STANDARD_ALLIANCE = 14541;
    constexpr uint32 DISPLAY_STANDARD_HORDE = 14542;
    constexpr uint32 DATA_GUILD = 1;

    // Where the tabards take you: the mage teleports' spots.
    struct Destination { uint32 map; float x, y, z, o; };
    constexpr Destination STORMWIND = { 0, -9003.46f, 870.031f, 29.6206f, 2.28f };
    constexpr Destination ORGRIMMAR = { 1, 1469.85f, -4221.52f, 58.9939f, 5.98f };

    // Loaded once at startup (world thread), read-only afterwards.
    std::vector<Reward> rewards;
    std::vector<uint32> vendorEntries;
    uint32 vendorScriptId = 0;

    char const* const RANK_NAMES[] = { "Hated", "Hostile", "Unfriendly", "Neutral", "Friendly", "Honored", "Revered", "Exalted" };

    // Items whose details this character's client was already sent; see SendItemDetails.
    class SentItems : public DataMap::Base
    {
    public:
        std::unordered_set<uint32> items;
    };
    constexpr char const* SENT_ITEMS_KEY = "mod-guild-levels-items";

    bool IsGuildVendor(Creature const* creature)
    {
        return creature && vendorScriptId && creature->GetScriptId() == vendorScriptId;
    }

    // The rewards a player is shown, in vendor slot order: the core's own vendor filters (another
    // class's soulbound items, the other faction's items) and the reward's faction.
    std::vector<Reward const*> VisibleRewards(Player const* player)
    {
        TeamId team = player->GetTeamId(true);
        std::vector<Reward const*> out;
        for (Reward const& reward : rewards)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reward.item);
            if (!proto)
                continue;
            if ((reward.team == 1 && team != TEAM_ALLIANCE) || (reward.team == 2 && team != TEAM_HORDE))
                continue;
            if (!(proto->AllowableClass & player->getClassMask()) && proto->Bonding == BIND_WHEN_PICKED_UP)
                continue;
            if ((proto->HasFlag2(ITEM_FLAG2_FACTION_HORDE) && team == TEAM_ALLIANCE) || (proto->HasFlag2(ITEM_FLAG2_FACTION_ALLIANCE) && team == TEAM_HORDE))
                continue;
            out.push_back(&reward);
            if (out.size() >= MAX_VENDOR_ITEMS)
                break;
        }
        return out;
    }

    // Why the player can't buy it yet, or empty.
    std::string LockReason(Player const* player, Reward const& reward)
    {
        if (!player->GetGuildId())
            return "Guild rewards are for guild members.";
        uint8 level = GetLevel(player->GetGuildId());
        if (level < reward.guildLevel)
            return Acore::StringFormat("Requires guild level {} (your guild is level {}).", reward.guildLevel, level);
        if (GetRepRank(player) < reward.repRank)
            return Acore::StringFormat("Requires {} with your guild.", RANK_NAMES[std::min<uint8>(reward.repRank, 7)]);
        return {};
    }

    uint32 Price(Player const* player, Creature const* vendor, Reward const& reward, uint32 count)
    {
        return uint32(std::floor(double(reward.price) * count * player->GetReputationPriceDiscount(vendor)));
    }

    // The vendor window draws a name only for items in the client's cache, and an item it has to
    // ask for stays nameless until the window opens again. So the details go out first, the way
    // the server answers the client's own query, once per item per login.
    void SendItemDetails(Player* player, std::vector<Reward const*> const& list)
    {
        WorldSession* session = player->GetSession();
        SentItems* sent = player->CustomData.GetDefault<SentItems>(SENT_ITEMS_KEY);
        for (Reward const* reward : list)
        {
            if (!sent->items.insert(reward->item).second)
                continue;
            WorldPacket request(CMSG_ITEM_QUERY_SINGLE, 4);
            request << uint32(reward->item);
            session->HandleItemQuerySingleOpcode(request);
        }
    }

    void SendRewardList(Player* player, Creature* vendor)
    {
        RefreshGuildRep(player);    // a guild just joined: no old guild's rep in the locks
        std::vector<Reward const*> list = VisibleRewards(player);
        SendItemDetails(player, list);

        WorldPacket data(SMSG_LIST_INVENTORY, 8 + 1 + list.size() * 8 * 4);
        data << vendor->GetGUID();
        data << uint8(list.size());
        if (list.empty())
        {
            data << uint8(0);   // "vendor has no inventory"
            player->GetSession()->SendPacket(&data);
            return;
        }

        for (std::size_t slot = 0; slot < list.size(); ++slot)
        {
            Reward const& reward = *list[slot];
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reward.item);
            data << uint32(slot + 1);                   // the client counts from 1
            data << uint32(reward.item);
            data << uint32(proto->DisplayInfoID);
            data << int32(LockReason(player, reward).empty() ? -1 : 0);   // locked: sold out
            data << uint32(Price(player, vendor, reward, 1));
            data << uint32(proto->MaxDurability);
            data << uint32(proto->BuyCount);
            data << uint32(0);                          // no extended cost
        }
        player->GetSession()->SendPacket(&data);
    }

    void Buy(Player* player, Creature* vendor, uint32 vendorSlot, uint32 itemId, uint8 count)
    {
        if (!GetConfig().rewardsEnabled || !player->IsAlive())
        {
            player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, vendor, itemId, 0);
            return;
        }

        RefreshGuildRep(player);
        std::vector<Reward const*> list = VisibleRewards(player);
        if (vendorSlot >= list.size() || list[vendorSlot]->item != itemId)
        {
            player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, vendor, itemId, 0);
            return;
        }
        Reward const& reward = *list[vendorSlot];
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);

        std::string why = LockReason(player, reward);
        if (!why.empty())
        {
            ChatHandler(player->GetSession()).SendNotification(why);
            return;
        }

        count = std::max<uint8>(count, 1);
        if (reward.price && count > MAX_MONEY_AMOUNT / reward.price)
            count = uint8(std::max<uint32>(1, MAX_MONEY_AMOUNT / reward.price));
        uint32 price = Price(player, vendor, reward, count);
        if (!player->HasEnoughMoney(price))
        {
            player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, vendor, itemId, 0);
            return;
        }

        uint32 amount = std::max<uint32>(1, proto->BuyCount) * count;
        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, amount);
        if (msg != EQUIP_ERR_OK)
        {
            player->SendEquipError(msg, nullptr, nullptr, itemId);
            return;
        }

        player->ModifyMoney(-int32(price));
        Item* item = player->StoreNewItem(dest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));
        if (!item)
            return;

        WorldPacket data(SMSG_BUY_ITEM, 8 + 4 + 4 + 4);
        data << vendor->GetGUID();
        data << uint32(vendorSlot + 1);
        data << int32(-1);      // unlimited stock
        data << uint32(count);
        player->SendDirectMessage(&data);
        player->SendNewItem(item, amount, true, false, false);
    }
}

namespace GuildLevels
{
    void LoadRewards()
    {
        rewards.clear();
        vendorEntries.clear();
        vendorScriptId = sObjectMgr->GetScriptId(VENDOR_SCRIPT);

        for (auto const& [entry, creature] : *sObjectMgr->GetCreatureTemplates())
            if (vendorScriptId && creature.ScriptID == vendorScriptId)
                vendorEntries.push_back(entry);
        std::sort(vendorEntries.begin(), vendorEntries.end());

        uint32 skipped = 0;
        if (QueryResult result = WorldDatabase.Query("SELECT item, team, guild_level, rep_rank, price, category FROM mod_guild_levels_reward ORDER BY sort, item"))
        {
            do
            {
                Field* f = result->Fetch();
                Reward reward;
                reward.item = f[0].Get<uint32>();
                reward.team = f[1].Get<uint8>();
                reward.guildLevel = std::clamp<uint8>(f[2].Get<uint8>(), 1, MAX_LEVEL);
                reward.repRank = std::min<uint8>(f[3].Get<uint8>(), uint8(REP_EXALTED));
                reward.price = f[4].Get<uint32>();
                reward.category = f[5].Get<uint8>();
                // Rows for items the server doesn't have (another module's pets and mounts that
                // isn't installed) are left out.
                if (!sObjectMgr->GetItemTemplate(reward.item))
                {
                    ++skipped;
                    continue;
                }
                rewards.push_back(reward);
            } while (result->NextRow());
        }

        LOG_INFO("module", "mod-guild-levels: {} guild rewards ({} rows for missing items left out), {} guild vendors",
            rewards.size(), skipped, vendorEntries.size());
    }

    std::vector<Reward> const& GetRewards()
    {
        return rewards;
    }

    std::vector<uint32> const& GetVendorEntries()
    {
        return vendorEntries;
    }
}

class npc_guild_levels_vendor : public CreatureScript
{
public:
    npc_guild_levels_vendor() : CreatureScript(VENDOR_SCRIPT) { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!GetConfig().rewardsEnabled)
            return false;
        player->PlayerTalkClass->SendCloseGossip();
        // No vendor list of the core's is open now; other modules read this in their buy hooks.
        player->GetSession()->SetCurrentVendor(0);
        SendRewardList(player, creature);
        return true;
    }
};

class GuildLevelsRewardsPlayerScript : public PlayerScript
{
public:
    GuildLevelsRewardsPlayerScript() : PlayerScript("GuildLevelsRewardsPlayerScript", { PLAYERHOOK_ON_BEFORE_BUY_ITEM_FROM_VENDOR }) { }

    // A guild vendor has no stock of the core's: every purchase from one is ours. Setting item
    // to 0 tells the core the purchase was handled.
    void OnPlayerBeforeBuyItemFromVendor(Player* player, ObjectGuid vendorGuid, uint32 vendorSlot, uint32& item, uint8 count, uint8 /*bag*/, uint8 /*slot*/) override
    {
        if (!item)
            return;
        Creature* vendor = player->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR);
        if (!IsGuildVendor(vendor))
            return;
        uint32 wanted = item;
        item = 0;
        Buy(player, vendor, vendorSlot, wanted, count);
    }
};

// Renowned and Illustrious Guild Tabard: back to your faction's capital.
class spell_guild_levels_guild_teleport : public SpellScript
{
    PrepareSpellScript(spell_guild_levels_guild_teleport);

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return SPELL_FAILED_DONT_REPORT;
        if (player->InBattleground())
            return SPELL_FAILED_NOT_HERE;
        return SPELL_CAST_OK;
    }

    void HandleTeleport(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return;
        Destination const& to = player->GetTeamId(true) == TEAM_HORDE ? ORGRIMMAR : STORMWIND;
        player->TeleportTo(to.map, to.x, to.y, to.z, to.o);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_guild_levels_guild_teleport::CheckCast);
        OnEffectHit += SpellEffectFn(spell_guild_levels_guild_teleport::HandleTeleport, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// The standard items: only someone in a guild can plant one.
class spell_guild_levels_plant_standard : public SpellScript
{
    PrepareSpellScript(spell_guild_levels_plant_standard);

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return SPELL_FAILED_DONT_REPORT;
        if (!player->GetGuildId())
        {
            ChatHandler(player->GetSession()).SendNotification("You are not in a guild.");
            return SPELL_FAILED_DONT_REPORT;
        }
        return SPELL_CAST_OK;
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_guild_levels_plant_standard::CheckCast);
    }
};

// A planted standard: the planter's faction's banner, wearing its rank's aura. It remembers the
// planter's guild, since the planter may walk off (or log off) while it stands.
struct npc_guild_levels_battle_standard : public ScriptedAI
{
    npc_guild_levels_battle_standard(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
    }

    void IsSummonedBy(WorldObject* summoner) override
    {
        Player* owner = summoner ? summoner->ToPlayer() : nullptr;
        if (!owner)
            return;
        guildId = owner->GetGuildId();
        uint32 display = owner->GetTeamId(true) == TEAM_HORDE ? DISPLAY_STANDARD_HORDE : DISPLAY_STANDARD_ALLIANCE;
        me->SetDisplayId(display);
        me->SetNativeDisplayId(display);
        uint32 rank = std::min<uint32>(me->GetEntry() - NPC_BATTLE_STANDARD_1, NPC_BATTLE_STANDARD_3 - NPC_BATTLE_STANDARD_1);
        DoCastSelf(SPELL_STANDARD_AURA_1 + rank, true);
    }

    uint32 GetData(uint32 id) const override
    {
        return id == DATA_GUILD ? guildId : 0;
    }

    void UpdateAI(uint32 /*diff*/) override { }

private:
    uint32 guildId = 0;
};

// The standard's area aura reaches only members of the planter's guild.
class spell_guild_levels_standard_aura : public AuraScript
{
    PrepareAuraScript(spell_guild_levels_standard_aura);

    bool CheckTarget(Unit* target)
    {
        Player* player = target ? target->ToPlayer() : nullptr;
        Creature* standard = GetCaster() ? GetCaster()->ToCreature() : nullptr;
        if (!player || !standard || !standard->AI())
            return false;
        uint32 guildId = standard->AI()->GetData(DATA_GUILD);
        return guildId && player->GetGuildId() == guildId;
    }

    void Register() override
    {
        DoCheckAreaTarget += AuraCheckAreaTargetFn(spell_guild_levels_standard_aura::CheckTarget);
    }
};

// A cauldron gives each member one flask: taking another would only refresh it and use a charge.
class go_guild_levels_cauldron : public GameObjectScript
{
public:
    go_guild_levels_cauldron() : GameObjectScript("go_guild_levels_cauldron") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (!player->HasAura(SPELL_FLASK_OF_BATTLE))
            return false;
        ChatHandler(player->GetSession()).SendNotification("You already have a Flask of Battle.");
        return true;
    }
};

void AddGuildLevelsRewardsScripts()
{
    new npc_guild_levels_vendor();
    new GuildLevelsRewardsPlayerScript();
    new go_guild_levels_cauldron();
    RegisterSpellScript(spell_guild_levels_guild_teleport);
    RegisterSpellScript(spell_guild_levels_plant_standard);
    RegisterCreatureAI(npc_guild_levels_battle_standard);
    RegisterSpellScript(spell_guild_levels_standard_aura);
}
