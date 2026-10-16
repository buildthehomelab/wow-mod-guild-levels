-- mod-guild-levels: guild reputation. The standing itself is the core's (character_reputation,
-- faction 1168); this is the guild it was earned in and the rep earned this week, for the cap.

CREATE TABLE IF NOT EXISTS `mod_guild_levels_rep` (
    `guid`     INT UNSIGNED NOT NULL COMMENT 'character',
    `guildid`  INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'the guild the rep belongs to',
    `week`     INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'weeks since 1970, starting Tuesday',
    `week_rep` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-guild-levels';
