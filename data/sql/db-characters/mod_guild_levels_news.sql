-- mod-guild-levels: the guild news feed, newest GuildLevels.News.Keep items per guild.

CREATE TABLE IF NOT EXISTS `mod_guild_levels_news` (
    `id`      INT UNSIGNED     NOT NULL COMMENT 'assigned by the module, rising',
    `guildid` INT UNSIGNED     NOT NULL,
    `type`    TINYINT UNSIGNED NOT NULL COMMENT '0 guild level, 1 boss, 2 item looted, 3 item crafted, 4 achievement',
    `time`    INT UNSIGNED     NOT NULL COMMENT 'unix time',
    `guid`    INT UNSIGNED     NOT NULL DEFAULT 0 COMMENT 'the member, 0 for the whole guild',
    `value`   INT UNSIGNED     NOT NULL DEFAULT 0 COMMENT 'level, encounter, item entry or achievement',
    `extra`   INT UNSIGNED     NOT NULL DEFAULT 0 COMMENT 'difficulty, item quality or achievement points',
    PRIMARY KEY (`id`),
    KEY `idx_guildid` (`guildid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-guild-levels';
