-- mod-guild-levels: guild reputation's rewards. The guild vendors (one in each capital), what they
-- sell, and the reward items the module adds: the guild tabards, battle standards and cauldrons.
-- Their spells and the guild factions are in mod_guild_levels_spells.sql (tools/build_patch.py).
--
-- Ids: creatures 9503401-9503408 (vendors, also their spawn guids) and 9503410-9503412 (standards),
-- gameobjects 9503401-9503402 (cauldrons), items 90456-90460 and 90464-90465, spell group 9503410.

-- What the guild vendors sell. A reward needs the guild level, the buyer's standing with their guild
-- (rep_rank: 3 Neutral, 4 Friendly, 5 Honored, 6 Revered, 7 Exalted) and the price in copper (less
-- the vendor's usual reputation discount). team: 0 both factions, 1 Alliance, 2 Horde. category is
-- for the addon: 1 tabard, 2 heirloom, 3 pet, 4 mount, 5 consumable. Rows for items the server
-- doesn't have are left out at startup: the pets and mounts (90216-90221) come from mod-retail-pets.
CREATE TABLE IF NOT EXISTS `mod_guild_levels_reward` (
    `item`        INT UNSIGNED      NOT NULL,
    `team`        TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `guild_level` TINYINT UNSIGNED  NOT NULL DEFAULT 1,
    `rep_rank`    TINYINT UNSIGNED  NOT NULL DEFAULT 3,
    `price`       INT UNSIGNED      NOT NULL DEFAULT 0 COMMENT 'copper',
    `category`    TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `sort`        SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'vendor order',
    `comment`     VARCHAR(100)      NOT NULL DEFAULT '',
    PRIMARY KEY (`item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-guild-levels';

DELETE FROM `mod_guild_levels_reward`;
INSERT INTO `mod_guild_levels_reward` (`item`, `team`, `guild_level`, `rep_rank`, `price`, `category`, `sort`, `comment`) VALUES
(90456, 0, 3, 5, 500000, 1, 1, 'Renowned Guild Tabard'),
(90457, 0, 12, 7, 1500000, 1, 2, 'Illustrious Guild Tabard'),
(42949, 0, 5, 4, 1000000, 2, 3, 'heirloom shoulders'),
(42950, 0, 5, 4, 1000000, 2, 4, 'heirloom shoulders'),
(42951, 0, 5, 4, 1000000, 2, 5, 'heirloom shoulders'),
(42952, 0, 5, 4, 1000000, 2, 6, 'heirloom shoulders'),
(42984, 0, 5, 4, 1000000, 2, 7, 'heirloom shoulders'),
(42985, 0, 5, 4, 1000000, 2, 8, 'heirloom shoulders'),
(44099, 0, 5, 4, 1000000, 2, 9, 'heirloom shoulders'),
(44100, 0, 5, 4, 1000000, 2, 10, 'heirloom shoulders'),
(44101, 0, 5, 4, 1000000, 2, 11, 'heirloom shoulders'),
(44102, 0, 5, 4, 1000000, 2, 12, 'heirloom shoulders'),
(44103, 0, 5, 4, 1000000, 2, 13, 'heirloom shoulders'),
(44105, 0, 5, 4, 1000000, 2, 14, 'heirloom shoulders'),
(44107, 0, 5, 4, 1000000, 2, 15, 'heirloom shoulders'),
(48677, 0, 10, 5, 1500000, 2, 16, 'heirloom chest'),
(48683, 0, 10, 5, 1500000, 2, 17, 'heirloom chest'),
(48685, 0, 10, 5, 1500000, 2, 18, 'heirloom chest'),
(48687, 0, 10, 5, 1500000, 2, 19, 'heirloom chest'),
(48689, 0, 10, 5, 1500000, 2, 20, 'heirloom chest'),
(48691, 0, 10, 5, 1500000, 2, 21, 'heirloom chest'),
(42943, 0, 15, 5, 2000000, 2, 22, 'heirloom weapon'),
(42944, 0, 15, 5, 2000000, 2, 23, 'heirloom weapon'),
(42945, 0, 15, 5, 2000000, 2, 24, 'heirloom weapon'),
(42946, 0, 15, 5, 2000000, 2, 25, 'heirloom weapon'),
(42947, 0, 15, 5, 2000000, 2, 26, 'heirloom weapon'),
(42948, 0, 15, 5, 2000000, 2, 27, 'heirloom weapon'),
(44091, 0, 15, 5, 2000000, 2, 28, 'heirloom weapon'),
(44092, 0, 15, 5, 2000000, 2, 29, 'heirloom weapon'),
(44093, 0, 15, 5, 2000000, 2, 30, 'heirloom weapon'),
(44094, 0, 15, 5, 2000000, 2, 31, 'heirloom weapon'),
(44095, 0, 15, 5, 2000000, 2, 32, 'heirloom weapon'),
(44096, 0, 15, 5, 2000000, 2, 33, 'heirloom weapon'),
(48716, 0, 15, 5, 2000000, 2, 34, 'heirloom weapon'),
(48718, 0, 15, 5, 2000000, 2, 35, 'heirloom weapon'),
(42991, 0, 18, 6, 2500000, 2, 36, 'Swift Hand of Justice'),
(42992, 0, 18, 6, 2500000, 2, 37, 'Discerning Eye of the Beast'),
(44098, 1, 18, 6, 2500000, 2, 38, 'Inherited Insignia of the Alliance'),
(44097, 2, 18, 6, 2500000, 2, 39, 'Inherited Insignia of the Horde'),
(90216, 0, 8, 5, 1500000, 3, 40, 'Armadillo Pup (mod-retail-pets)'),
(90217, 0, 16, 6, 3000000, 3, 41, 'Dark Phoenix Hatchling (mod-retail-pets)'),
(90218, 0, 22, 7, 5000000, 3, 42, 'Lil\' Tarecgosa (mod-retail-pets)'),
(90219, 1, 20, 7, 10000000, 4, 43, 'Golden King (mod-retail-pets)'),
(90220, 2, 20, 7, 10000000, 4, 44, 'Kor\'kron Annihilator (mod-retail-pets)'),
(90221, 0, 25, 7, 20000000, 4, 45, 'Dark Phoenix (mod-retail-pets)'),
(90458, 0, 4, 4, 100000, 5, 46, 'Guild Battle Standard'),
(90464, 0, 10, 5, 500000, 5, 47, 'Cauldron of Battle'),
(90459, 0, 13, 5, 250000, 5, 48, 'Greater Guild Battle Standard'),
(90465, 0, 19, 6, 1000000, 5, 49, 'Big Cauldron of Battle'),
(90460, 0, 21, 6, 500000, 5, 50, 'Grand Guild Battle Standard');

-- The reward items. Their Item.dbc rows (tools/build_patch.py) give the client the same class,
-- display and slot. The vendors charge the reward price, so BuyPrice stays 0.
DELETE FROM `item_template` WHERE `entry` IN (90456, 90457, 90458, 90459, 90460, 90464, 90465);
INSERT INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `InventoryType`, `ItemLevel`,
    `maxcount`, `stackable`, `bonding`, `Material`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `description`) VALUES
(90456, 4, 0, 'Renowned Guild Tabard', 20621, 3, 19, 1, 1, 1, 1, 7, 90456, 0, 0, 'Shows your guild\'s emblem.'),
(90457, 4, 0, 'Illustrious Guild Tabard', 20621, 4, 19, 1, 1, 1, 1, 7, 90457, 0, 0, 'Shows your guild\'s emblem.'),
(90458, 0, 8, 'Guild Battle Standard', 16263, 1, 0, 1, 0, 20, 1, 4, 90458, 0, -1, ''),
(90459, 0, 8, 'Greater Guild Battle Standard', 932, 2, 0, 1, 0, 20, 1, 4, 90459, 0, -1, ''),
(90460, 0, 8, 'Grand Guild Battle Standard', 3091, 3, 0, 1, 0, 20, 1, 4, 90460, 0, -1, ''),
(90464, 0, 8, 'Cauldron of Battle', 46254, 2, 0, 60, 0, 5, 1, 3, 90464, 0, -1, ''),
(90465, 0, 8, 'Big Cauldron of Battle', 46255, 3, 0, 60, 0, 5, 1, 3, 90465, 0, -1, '');

-- The guild vendors, beside each capital's tabard vendor. Talking to one opens its window
-- (npc_guild_levels_vendor); it has no npc_vendor rows of its own.
DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 9503401 AND 9503412;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 9503401 AND 9503412;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `speed_walk`, `speed_run`,
    `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`, `unit_flags2`, `type`, `flags_extra`, `ScriptName`) VALUES
(9503401, 'Mirabel Ashford', 'Guild Vendor', 60, 60, 12, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
(9503402, 'Dorn Ironstave', 'Guild Vendor', 60, 60, 55, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
(9503403, 'Ilyara Moonwhisper', 'Guild Vendor', 60, 60, 80, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
(9503404, 'Vaaros', 'Guild Vendor', 60, 60, 1638, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
(9503405, 'Grezka', 'Guild Vendor', 60, 60, 29, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
(9503406, 'Edwin Harrow', 'Guild Vendor', 60, 60, 68, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
(9503407, 'Nahla Windmane', 'Guild Vendor', 60, 60, 104, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
(9503408, 'Caelis Dawnmere', 'Guild Vendor', 60, 60, 1604, 129, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_guild_levels_vendor'),
-- The battle standards: faction 35 and can't be attacked; the script wears the
-- planter's faction's banner (Alliance 14541, Horde 14542) and its rank's aura.
(9503410, 'Guild Battle Standard', '', 1, 1, 35, 0, 1, 1, 2000, 2000, 1, 770, 2048, 10, 0, 'npc_guild_levels_battle_standard'),
(9503411, 'Guild Battle Standard', '', 1, 1, 35, 0, 1, 1, 2000, 2000, 1, 770, 2048, 10, 0, 'npc_guild_levels_battle_standard'),
(9503412, 'Guild Battle Standard', '', 1, 1, 35, 0, 1, 1, 2000, 2000, 1, 770, 2048, 10, 0, 'npc_guild_levels_battle_standard');
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(9503401, 0, 19177, 1, 1),
(9503402, 0, 19174, 1, 1),
(9503403, 0, 19179, 1, 1),
(9503404, 0, 19172, 1, 1),
(9503405, 0, 19181, 1, 1),
(9503406, 0, 19188, 1, 1),
(9503407, 0, 19183, 1, 1),
(9503408, 0, 19170, 1, 1),
(9503410, 0, 14541, 1, 1),
(9503411, 0, 14541, 1, 1),
(9503412, 0, 14541, 1, 1);

-- Only added when the spawn isn't there yet, so a spot picked in game with .npc move survives this
-- file running again.
INSERT IGNORE INTO `creature` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `MovementType`, `Comment`) VALUES
(9503401, 9503401, 0, 1, 1, -8892.8, 608.83, 95.34, 0.31, 300, 0, 0, 'mod-guild-levels: Mirabel Ashford, guild vendor in Stormwind, beside Rebecca Laughlin'),
(9503402, 9503402, 0, 1, 1, -5022.45, -1001.67, 503.96, 1.99, 300, 0, 0, 'mod-guild-levels: Dorn Ironstave, guild vendor in Ironforge, beside Lyesa Steelbrow'),
(9503403, 9503403, 1, 1, 1, 10070.7, 2193.21, 1346.7, 1.59, 300, 0, 0, 'mod-guild-levels: Ilyara Moonwhisper, guild vendor in Darnassus, beside Shalumon'),
(9503404, 9503404, 530, 1, 1, -4095.49, -11634.47, -138.78, 1.6, 300, 0, 0, 'mod-guild-levels: Vaaros, guild vendor in the Exodar, beside Issca'),
(9503405, 9503405, 1, 1, 1, 1580.79, -4296.55, 26.3, 3.75, 300, 0, 0, 'mod-guild-levels: Grezka, guild vendor in Orgrimmar, beside Garyl'),
(9503406, 9503406, 0, 1, 1, 1588.55, 207.97, -55.26, 4.76, 300, 0, 0, 'mod-guild-levels: Edwin Harrow, guild vendor in the Undercity, beside Merill Pleasance'),
(9503407, 9503407, 1, 1, 1, -1288.51, 121.57, 131.64, 0.72, 300, 0, 0, 'mod-guild-levels: Nahla Windmane, guild vendor in Thunder Bluff, beside Thrumn'),
(9503408, 9503408, 530, 1, 1, 9467.49, -7353.41, 16.18, 1.57, 300, 0, 0, 'mod-guild-levels: Caelis Dawnmere, guild vendor in Silvermoon City, beside Kredis');

-- The cauldrons: spellcaster gameobjects that give party and raid members of the one who set them
-- out a Flask of Battle (90466), 10 or 25 times, one each while it lasts (go_guild_levels_cauldron).
-- The summoning spell removes them after 10 minutes.
DELETE FROM `gameobject_template` WHERE `entry` IN (9503401, 9503402);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`,
    `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`, `Data8`, `Data9`, `Data10`, `Data11`,
    `Data12`, `Data13`, `Data14`, `Data15`, `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`,
    `Data23`, `AIName`, `ScriptName`, `VerifiedBuild`) VALUES
(9503401, 22, 7409, 'Cauldron of Battle', '', '', '', 1, 90466, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 'go_guild_levels_cauldron', 0),
(9503402, 22, 7408, 'Big Cauldron of Battle', '', '', '', 1.3, 90466, 25, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 'go_guild_levels_cauldron', 0);

-- The reward spells' scripts.
DELETE FROM `spell_script_names` WHERE `spell_id` BETWEEN 90456 AND 90463;
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(90456, 'spell_guild_levels_guild_teleport'),
(90457, 'spell_guild_levels_guild_teleport'),
(90458, 'spell_guild_levels_plant_standard'),
(90459, 'spell_guild_levels_plant_standard'),
(90460, 'spell_guild_levels_plant_standard'),
(90461, 'spell_guild_levels_standard_aura'),
(90462, 'spell_guild_levels_standard_aura'),
(90463, 'spell_guild_levels_standard_aura');

-- Flask of Battle is a flask: a Battle and a Guardian elixir (groups 1 and 2). Of the standards'
-- auras, only the highest rank counts.
DELETE FROM `spell_group` WHERE `spell_id` IN (90461, 90462, 90463, 90466);
INSERT INTO `spell_group` (`id`, `spell_id`) VALUES
(1, 90466),
(2, 90466),
(9503410, 90461),
(9503410, 90462),
(9503410, 90463);
DELETE FROM `spell_group_stack_rules` WHERE `group_id` = 9503410;
INSERT INTO `spell_group_stack_rules` (`group_id`, `stack_rule`, `description`) VALUES
(9503410, 4, 'mod-guild-levels: Guild Battle Standard (highest rank)');
