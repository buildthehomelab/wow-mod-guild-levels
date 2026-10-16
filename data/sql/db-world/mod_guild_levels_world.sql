-- mod-guild-levels: the server-only perk auras, the spell scripts, and Mobile Banking's chest.
--
-- 90450 Mount Up, 90451 The Quick and the Dead and 90455 For Great Justice are hidden passive
-- auras the client never sees (Attributes 0x40 passive + 0x80 hidden). The module sets their
-- amounts from the config:
--   90450: Mod Mounted Speed Always (130) and Mod Mounted Flight Speed Always (209), stacking
--          with other mount speed bonuses.
--   90451: Mod Speed Always (129), the ghost's run speed.
--   90455: a Dummy aura (4) whose amount is the chance of one more boss coin; mod-boss-coins
--          reads it when BossCoins.BossKills.BonusAura = 90455.
-- The castable perks 90452-90454 are in mod_guild_levels_spells.sql, written by
-- tools/build_patch.py from the client rows so the two can't disagree.

DELETE FROM `spell_dbc` WHERE `ID` IN (90450, 90451, 90455);
INSERT INTO `spell_dbc`
    (`ID`, `Attributes`, `AttributesEx`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`, `EquippedItemClass`,
     `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`, `ImplicitTargetA_1`, `EffectAura_1`,
     `Effect_2`, `EffectDieSides_2`, `EffectBasePoints_2`, `ImplicitTargetA_2`, `EffectAura_2`,
     `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`, `SchoolMask`,
     `EffectMultipleValue_1`, `EffectMultipleValue_2`, `EffectChainAmplitude_1`, `EffectChainAmplitude_2`,
     `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`)
VALUES
    (90450, 192, 268435456, 1, 21, 1, -1,
     6, 1, 9, 1, 130,
     6, 1, 9, 1, 209,
     1, 'Mount Up', 16712190, 1,
     0, 0, 1, 1, 1, 1),
    (90451, 192, 268435456, 1, 21, 1, -1,
     6, 1, 99, 1, 129,
     0, 0, 0, 0, 0,
     1, 'The Quick and the Dead', 16712190, 1,
     0, 0, 1, 0, 1, 0),
    (90455, 192, 268435456, 1, 21, 1, -1,
     6, 1, 9, 1, 4,
     0, 0, 0, 0, 0,
     1, 'For Great Justice', 16712190, 1,
     0, 0, 1, 0, 1, 0);

-- The castable perks' scripts.
DELETE FROM `spell_script_names` WHERE `spell_id` IN (90452, 90453, 90454);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
    (90452, 'spell_guild_levels_mobile_banking'),
    (90453, 'spell_guild_levels_have_group'),
    (90454, 'spell_guild_levels_mass_resurrection');

-- Mobile Banking's guild chest: a guild bank (type 34) shaped like a chest (the Battered Chest's
-- model). The summoning spell despawns it after 5 minutes.
DELETE FROM `gameobject_template` WHERE `entry` = 9503400;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`,
    `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`, `Data8`, `Data9`, `Data10`, `Data11`,
    `Data12`, `Data13`, `Data14`, `Data15`, `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`,
    `Data23`, `AIName`, `ScriptName`, `VerifiedBuild`)
VALUES (9503400, 34, 259, 'Guild Chest', '', '', '', 1.2,
    0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', '', 0);
