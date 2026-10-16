-- mod-guild-levels: removes the module's world rows. Items players own stay in their bags; run
-- this after removing the module and its client patch rows.
DELETE FROM `spell_dbc` WHERE `ID` BETWEEN 90450 AND 90466;
DELETE FROM `faction_dbc` WHERE `ID` IN (1168, 1169);
DELETE FROM `spell_script_names` WHERE `spell_id` BETWEEN 90452 AND 90463;
DELETE FROM `spell_group` WHERE `spell_id` IN (90461, 90462, 90463, 90466);
DELETE FROM `spell_group_stack_rules` WHERE `group_id` = 9503410;
DROP TABLE IF EXISTS `mod_guild_levels_reward`;
DELETE FROM `item_template` WHERE `entry` IN (90456, 90457, 90458, 90459, 90460, 90464, 90465);
DELETE FROM `creature` WHERE `id` BETWEEN 9503401 AND 9503412;
DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 9503401 AND 9503412;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 9503401 AND 9503412;
DELETE FROM `gameobject` WHERE `id` IN (9503401, 9503402);
DELETE FROM `gameobject_template` WHERE `entry` IN (9503401, 9503402);
DELETE FROM `gameobject_template` WHERE `entry` = 9503400;
DELETE FROM `gameobject` WHERE `id` = 9503400;
