# mod-guild-levels

Cataclysm-style guild levels for [AzerothCore](https://www.azerothcore.org) (WotLK 3.3.5a). A guild
earns guild XP from its members' quests and from dungeon and raid bosses killed as a guild group,
and climbs from level 1 to 25. Every level from 2 to 25 unlocks a guild perk, in Cataclysm's order.
The GuildLevels addon shows the guild's level, XP, perks and each member's contribution.

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
| 3 | Mount Up | +10% mounted and flying speed (not in battlegrounds and arenas) |
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

## Install

```bash
cd modules
git clone https://github.com/buildthehomelab/wow-mod-guild-levels.git mod-guild-levels
```

Clone into `mod-guild-levels` (no `wow-` prefix): AzerothCore derives the loader symbol from the
folder name. Rebuild, copy `conf/mod_guild_levels.conf.dist` to `mod_guild_levels.conf`, and start
the worldserver; it applies `data/sql/db-world` and `data/sql/db-characters` itself.

### Client patch

Mobile Banking, Have Group, Will Travel and Mass Resurrection are new spells (90452-90454), so the
client needs them in its `Spell.dbc`. Add them to the patch that carries your realm's Spell.dbc:

```bash
python3 tools/build_patch.py --patch patch-W.MPQ --out patch-W-new.MPQ \
    --sql data/sql/db-world/mod_guild_levels_spells.sql
```

The tool copies every other file of the patch unchanged, and writes the server's rows from the
same Spell.dbc rows (`--sql`), so client and server agree. It needs Python 3 and
[StormLib](https://github.com/ladislav-zezula/StormLib). Without the patch the spells don't show in
the spellbook. The other perks need nothing on the client.

### The addon

`addon/GuildLevels` goes in `Interface/AddOns/GuildLevels`. It adds a **Guild Level** button to the
guild tab of the social window, and `/glevel` opens the window: the guild's level and XP, today's
XP against the daily cap, Cash Flow paid into the bank this week (the guild's and yours), the
perks (the guild's in colour, the rest with the level they come at), and each member's guild XP
this week and in total. When the guild levels up, a banner shows the new level and its perk,
with the level-up sound; clicking it opens the window. `sql/portalkeeper_addon.sql` adds it to
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
| spells | 90450 Mount Up, 90451 The Quick and the Dead, 90455 For Great Justice (server only); 90452 Mobile Banking, 90453 Have Group, Will Travel, 90454 Mass Resurrection |
| gameobject | 9503400 Guild Chest (a guild bank) |
| characters tables | `mod_guild_levels`, `mod_guild_levels_member` |

## License

MIT. The perks and the XP table follow Blizzard's Cataclysm guild advancement.
