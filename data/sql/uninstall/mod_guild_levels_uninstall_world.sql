-- mod-guild-levels: removes the module's world rows.
DELETE FROM `spell_dbc` WHERE `ID` BETWEEN 90450 AND 90455;
DELETE FROM `spell_script_names` WHERE `spell_id` IN (90452, 90453, 90454);
DELETE FROM `gameobject_template` WHERE `entry` = 9503400;
DELETE FROM `gameobject` WHERE `id` = 9503400;
