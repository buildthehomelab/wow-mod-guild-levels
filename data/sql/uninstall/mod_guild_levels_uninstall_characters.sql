-- mod-guild-levels: removes guild levels, members' guild XP and the learned perk spells.
DROP TABLE IF EXISTS `mod_guild_levels`;
DROP TABLE IF EXISTS `mod_guild_levels_member`;
DELETE FROM `character_spell` WHERE `spell` IN (90452, 90453, 90454);
DELETE FROM `character_aura` WHERE `spell` BETWEEN 90450 AND 90455;
