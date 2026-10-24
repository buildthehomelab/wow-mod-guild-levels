# mod-guild-levels

Cataclysm-style guild levels for [AzerothCore](https://www.azerothcore.org) (WotLK 3.3.5a). A guild
earns guild XP from its members' quests and from dungeon and raid bosses killed as a guild group,
and climbs from level 1 to 25. Every level from 2 to 25 unlocks a guild perk, in Cataclysm's order.
Members also earn reputation with their guild, which buys guild rewards (heirlooms, tabards,
mounts, pets, battle standards and cauldrons) from a guild vendor in each capital. The GuildLevels
addon shows the guild's level, XP, perks, rewards and each member's contribution.

## Guild XP

| Source | Guild XP |
|---|---|
| A quest | 25% of the quest's XP (`GuildLevels.XP.QuestShare`). Counts at the level cap too, where the quest pays money instead. Repeat completions that give no XP give none. |
| A dungeon boss killed by a guild group | 2000 (`GuildLevels.XP.BossDungeon`): 50% with 3 guild members in the group, 100% with 4, 125% with 5 |
| A raid boss killed by a guild group | 6000 (`GuildLevels.XP.BossRaid`) with 10 guild members (`GuildLevels.XP.RaidGuildMembers`, at most 80% of the raid) |

Heroic dungeons and raids pay 1.5×. A boss that is gray to the highest-level guild member pays
nothing. A boss is one of the instance's encounters (DungeonEncounter.dbc), paid when the core
credits it and once per instance, so a council of several creatures pays once. The players in the
instance are the group: the guild with the most members among them is the guild group. Quest XP
is paid when the reward goes through, not when the quest giver's window shows it.

The XP needed for each level is Cataclysm's table (16.58 million for level 2 up to 54.73 million
for level 25, 855.69 million in all) times `GuildLevels.XP.CurveScale`. The default, 0.0125, makes
the whole climb about 10.7 million guild XP: about three months for three to five active players. A guild earns at most
150,000 guild XP a day (`GuildLevels.XP.DailyCap`) until level 20.

**Bots.** Only real players earn guild XP. Bots ([mod-playerbots](https://github.com/mod-playerbots/mod-playerbots))
that are members of the guild count toward the size of a guild group, so one player with four guild
bots is a full guild group. A guild with no real player in it never earns anything, so bot-run
guilds stay at level 1 (`GuildLevels.XP.BotGuildsEarn` changes that).

A member's contribution is kept per week (weeks start on Tuesday) and in total, and is reset when
they leave the guild.

## Perks

| Level | Perk | Effect |
|---|---|---|
| 2 | Fast Track | +5% XP from kills and quests |
| 3 | Mount Up | +10% mounted and flying speed (not in battlegrounds and arenas); with [mod-forever-druid](https://github.com/buildthehomelab/wow-mod-forever-druid) and [mod-forever-shaman](https://github.com/buildthehomelab/wow-mod-forever-shaman) also Travel Form, the flight forms and Ghost Wolf |
| 4 | Mr. Popularity | +5% reputation from kills and quests |
| 5 | Cash Flow | an extra 5% of money looted from enemies goes into the guild bank (the addon shows the week's total) |
| 6 | Fast Track (Rank 2) | +10% XP |
| 7 | Reinforce | 10% less durability lost by dying |
| 8 | Ride Like the Wind | flight paths 25% faster (replaces Hasty Hearth, see below) |
| 9 | Reinforce (Rank 2) | 20% less durability lost by dying |
| 10 | Chug-A-Lug | Well Fed, flask and elixir buffs last 50% longer |
| 11 | Mobile Banking | spell: summons a guild chest that opens your guild bank for 5 min (1 hour cooldown) |
| 12 | Mr. Popularity (Rank 2) | +10% reputation |
| 13 | Honorable Mention | +5% honor from honorable kills |
| 14 | Working Overtime | +10% chance of a crafting skill-up (a second roll at a tenth of the recipe's own chance) |
| 15 | The Quick and the Dead | +50% health and mana when a guild member of your group resurrects you; +100% speed as a ghost |
| 16 | Cash Flow (Rank 2) | an extra 10% |
| 17 | The Doctor Is In | healing potions and healthstones heal 50% more (replaces Guild Mail, see below) |
| 18 | For Great Justice | a 10% chance of one more boss coin per boss award (replaces Justice Points, see below) |
| 19 | Honorable Mention (Rank 2) | +10% honor |
| 20 | Happy Hour | a 50% chance of an extra flask or elixir when you make one |
| 21 | Have Group, Will Travel | spell: summons your party or raid to you (outdoors, 2 hour cooldown) |
| 22 | Chug-A-Lug (Rank 2) | buffs last 100% longer |
| 23 | Bountiful Bags | a 15% chance of an extra trade good or gem from herb and ore nodes, Skinning and Disenchanting |
| 24 | Bartering | 10% lower prices and repairs at vendors |
| 25 | Mass Resurrection | spell: brings dead party and raid members within 100 yards back with 35% health and mana (10 min cooldown; once per player every 10 min) |

Every value is in the config. Where 3.3.5 has nothing to match, the perk changed:

- **Ride Like the Wind** (from Mists of Pandaria) replaces Hasty Hearth. It needs a core hook,
  `PlayerScript::OnPlayerTaxiFlightSpeed`, which stock AzerothCore doesn't have (see
  [The core hook](#the-core-hook)). Without it the module still builds, and the perk does nothing.
- **The Doctor Is In** (from Mists of Pandaria) replaces Guild Mail, which does nothing on
  realms with `MailDeliveryDelay` set to 0. Healing potions are consumables of the potion
  subclass; healthstones are found by name.
- **For Great Justice** adds to [mod-boss-coins](https://github.com/buildthehomelab/wow-mod-boss-coins)'
  boss coins instead of Justice Points. This module only gives the player a hidden aura (90455)
  whose amount is the chance; mod-boss-coins rolls it on each boss award when its
  `BossCoins.BossKills.BonusAura` is 90455. Without that setting the perk does nothing.
- **Chug-A-Lug** and **Happy Hour** are about food, flasks and elixirs, because 3.3.5 has no guild
  cauldrons or feasts.
- **Honorable Mention** adds to honor from honorable kills. The core has no hook for battleground
  bonus honor.
- **Reinforce** gives back part of the durability lost between the killing blow and resurrection
  (the death itself and the spirit healer), because the core has no hook on durability loss.
- **The Quick and the Dead**'s resurrection bonus counts resurrection spells cast by guild members
  of your group (Mass Resurrection too), not the spirit healer or running back to your corpse. A
  soulstone taken while a guild member's resurrection is still on offer counts too.
- **Mobile Banking**'s chest is a guild bank like the ones in the capitals: anyone who uses it opens
  their own guild's bank.

The three spells are learned when the guild reaches their level and unlearned when it doesn't have
them (left the guild, `.guildlevel set` lower). The perks are checked every two seconds.

## Guild news

Like Cataclysm's Guild News, the module keeps a feed per guild, newest first: the guild reaching a
level, dungeon and raid bosses killed by a guild group, epic items members looted, won in a roll
or crafted, and achievements members earned. Bots' loot and achievements stay out unless
`GuildLevels.News.Bots` is on. The same member looting or crafting the same item again within an hour adds
nothing new. Items a master looter hands out don't show: the core gives them
out without a script hook. It keeps the newest `GuildLevels.News.Keep` items (100) and deletes
older ones. The feed goes to addons with the `NEWS` request (see `src/GuildLevelsAddon.cpp`); the
GuildLevels addon keeps it in `GuildLevels.news` for a guild window to show.

## Guild reputation

As in Cataclysm, each member earns reputation with their guild from the guild XP they bring in
themself: one point per 36 guild XP (`GuildLevels.Rep.XPPerRep`; Cataclysm's 360 is for its far
larger guild XP). A level 60 quest or a dungeon boss killed in a guild group is about 55 rep, a raid
boss about 165; everyone in the guild group gets the boss's rep. It counts what the member earned,
so it still comes once the guild is level 25 or has reached its daily XP cap. A member can earn
4,375 a week (`GuildLevels.Rep.WeeklyCap`, Cataclysm 4.2), Tuesday to Monday. Only real players
earn it. Like other reputation it follows the realm's `Rate.Reputation.Gain`, which scales both the
rep and the weekly cap (on a 4x realm: a point per 9 guild XP, 17,500 a week); set
`GuildLevels.Rep.RealmRate = 0` to keep it at 1x.

It is a real faction, **Your Guild** (Faction.dbc 1168) under a **Guild** header (1169), so it is in
the reputation pane, can be watched as a bar and gives the usual "Reputation with Your Guild
increased" lines. The rep belongs to the guild it was earned in: joining another guild starts it
over at Neutral. While a member is in no guild it waits under Inactive, back where it was if they
rejoin the same guild. The client needs the two factions (see [Client patch](#client-patch)).

## Guild vendors and rewards

A guild vendor stands beside the tabard vendor in each capital: Mirabel Ashford (Stormwind), Dorn
Ironstave (Ironforge), Ilyara Moonwhisper (Darnassus), Vaaros (the Exodar), Grezka (Orgrimmar),
Edwin Harrow (the Undercity), Nahla Windmane (Thunder Bluff) and Caelis Dawnmere (Silvermoon City).
Each sells every reward of its faction. A reward needs a guild level, your standing with your
guild and gold; one you can't buy yet shows as sold out (dark red), and trying says what's
missing. With the addon, its tooltip shows both requirements in red or green.

| Reward | Guild level | Standing | Price |
|---|---|---|---|
| Renowned Guild Tabard: your guild's emblem; teleports you to Stormwind or Orgrimmar (8 h) | 3 | Honored | 50g |
| Illustrious Guild Tabard: the same, every 2 h | 12 | Exalted | 150g |
| Heirloom shoulders (all 13 of 3.3.5) | 5 | Friendly | 100g |
| Heirloom chests (6) | 10 | Honored | 150g |
| Heirloom weapons (14) | 15 | Honored | 200g |
| Heirloom trinkets: Swift Hand of Justice, Discerning Eye of the Beast, Inherited Insignia of your faction | 18 | Revered | 250g |
| Armadillo Pup (companion) | 8 | Honored | 150g |
| Dark Phoenix Hatchling (companion) | 16 | Revered | 300g |
| Lil' Tarecgosa (companion) | 22 | Exalted | 500g |
| Golden King (Alliance) / Kor'kron Annihilator (Horde) (mount) | 20 | Exalted | 1,000g |
| Dark Phoenix (mount; flies where flying mounts can) | 25 | Exalted | 2,000g |
| Guild Battle Standard: planted for 15 min, +5% maximum health for guild members within 45 yards | 4 | Friendly | 10g |
| Greater Guild Battle Standard: +10% | 13 | Honored | 25g |
| Grand Guild Battle Standard: +15% (only the highest standard counts) | 21 | Revered | 50g |
| Cauldron of Battle: 10 min, 10 Flasks of Battle for your party or raid | 10 | Honored | 50g |
| Big Cauldron of Battle: 25 flasks | 19 | Revered | 100g |

Flask of Battle: +15 to all stats for an hour, through death, one per member from a cauldron; it is a flask (a Battle and a
Guardian elixir), so Chug-A-Lug makes it last longer. Prices are before the vendor's usual
reputation discount (and Bartering). The list is the world table `mod_guild_levels_reward`
(item, faction, guild level, standing, price in copper, vendor order); rows for items the server
doesn't have are left out at startup. The companions and mounts are items of
[mod-retail-pets](https://github.com/buildthehomelab/wow-mod-retail-pets) (90216-90221) wearing
[mod-retail-library](https://github.com/buildthehomelab/wow-mod-retail-library) looks; without
those modules the vendors sell the rest.

## Install

```bash
cd modules
git clone https://github.com/buildthehomelab/wow-mod-guild-levels.git mod-guild-levels
```

Clone into `mod-guild-levels` (no `wow-` prefix): AzerothCore derives the loader symbol from the
folder name. Rebuild, copy `conf/mod_guild_levels.conf.dist` to `mod_guild_levels.conf`, and start
the worldserver; it applies `data/sql/db-world` and `data/sql/db-characters` itself.

### Client patch

Mobile Banking, Have Group, Will Travel and Mass Resurrection are new spells (90452-90454), and the
rewards add spells (90456-90466), items (90456-90460, 90464-90465) and the guild factions (1168,
1169), so the client needs them in its `Spell.dbc`, `Item.dbc` and `Faction.dbc`. Add them to the
patch that carries your realm's Spell.dbc and Item.dbc:

```bash
python3 tools/build_patch.py --patch patch-W.MPQ --client /path/to/client --out patch-W-new.MPQ \
    --sql data/sql/db-world/mod_guild_levels_spells.sql
```

The tool copies every other file of the patch unchanged; when the patch has no Faction.dbc yet it
starts from the client's own (`--client`, its newest enUS locale patch). It writes the server's
rows from the same rows (`--sql`), so client and server agree. It needs Python 3 and
[StormLib](https://github.com/ladislav-zezula/StormLib). Without the patch the spells don't show in
the spellbook, the reward items show a question mark and the reputation pane has no Guild. Build
mod-retail-pets' rows into the patch first if you use it: both tools add to the same file.

### The addon

`addon/GuildLevels` goes in `Interface/AddOns/GuildLevels`. It adds a side tab (a guild tabard with the
level on it) to the guild tab of the social window, and `/glevel` opens the window: the guild's level and XP, today's
XP against the daily cap, Cash Flow paid into the bank this week (the guild's and yours), your
guild reputation and this week's rep against the cap, and tabs for the perks (the guild's in
colour, the rest with the level they come at), the rewards with what each needs, the guild news,
and each member's guild XP this week and in total. The window uses only stock Blizzard art; with
DragonUI, its own guild window shows the same instead. At a guild vendor, a reward's tooltip shows its guild level and standing. When the guild levels up, a banner shows the new level and its perk,
with the level-up sound; clicking it opens the window. With DragonUI's Level Up Enhance module on,
DragonUI's own level-up banner shows it instead. `sql/portalkeeper_addon.sql` adds it to
[Portalkeeper](https://github.com/buildthehomelab/wow-Portalkeeper)'s addon list.

## The core hook

Ride Like the Wind needs this in `FlightPathMovementGenerator::DoReset`
(`src/server/game/Movement/MovementGenerators/WaypointMovementGenerator.cpp`):

```cpp
float speed = sWorld->getFloatConfig(CONFIG_TAXI_FLIGHT_SPEED);
sScriptMgr->OnPlayerTaxiFlightSpeed(player, speed);
init.SetVelocity(speed);
```

plus `PLAYERHOOK_ON_TAXI_FLIGHT_SPEED` and `virtual void OnPlayerTaxiFlightSpeed(Player*, float&)`
in `PlayerScript`, and its `ScriptMgr` call. `mod-guild-levels.cmake` looks for the hook in the core
and turns the perk on when it is there.

## Commands

| Command | Who | |
|---|---|---|
| `.guildlevel` | everyone | your guild's level and XP |
| `.guildlevel info [guild]` | GM | any guild's (or the selected player's) |
| `.guildlevel set <level> [guild]` | admin | set a guild's level |
| `.guildlevel addxp <xp> [guild]` | admin | give a guild XP, ignoring the daily cap |

## Uninstall

Remove the module and rebuild, then run `data/sql/uninstall/mod_guild_levels_uninstall_world.sql`
on the world database and `mod_guild_levels_uninstall_characters.sql` on the characters database
(it drops the guild levels and the learned perk spells).

## IDs

| What | IDs |
|---|---|
| spells | 90450 Mount Up, 90451 The Quick and the Dead, 90455 For Great Justice (server only); 90452 Mobile Banking, 90453 Have Group, Will Travel, 90454 Mass Resurrection; 90456-90457 Guild Teleport (tabards), 90458-90460 Guild Battle Standard, 90461-90463 its auras, 90464-90465 the cauldrons, 90466 Flask of Battle |
| items | 90456 Renowned and 90457 Illustrious Guild Tabard, 90458-90460 the battle standards, 90464-90465 the cauldrons |
| factions | 1168 Your Guild (reputation index 105), 1169 Guild (the header, index 106) |
| creatures | 9503401-9503408 the guild vendors (also their spawn guids), 9503410-9503412 the battle standards |
| gameobjects | 9503400 Guild Chest (a guild bank), 9503401-9503402 the cauldrons |
| spell group | 9503410 (the battle standards' auras: only the highest counts) |
| world table | `mod_guild_levels_reward` |
| characters tables | `mod_guild_levels`, `mod_guild_levels_member`, `mod_guild_levels_news`, `mod_guild_levels_rep` |

## License

MIT. The perks and the XP table follow Blizzard's Cataclysm guild advancement.
