-- mod-guild-levels: guild levels and each member's guild XP.

CREATE TABLE IF NOT EXISTS `mod_guild_levels` (
    `guildid`  INT UNSIGNED     NOT NULL,
    `level`    TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `xp`       BIGINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT 'into the current level',
    `total_xp` BIGINT UNSIGNED  NOT NULL DEFAULT 0,
    `day`      INT UNSIGNED     NOT NULL DEFAULT 0 COMMENT 'days since 1970, server time',
    `day_xp`   BIGINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT 'guild XP earned that day, for the daily cap',
    `cash_week`       INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT 'the week cash_week_money is for',
    `cash_week_money` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Cash Flow into the guild bank that week, copper',
    `cash_total`      BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Cash Flow in all, copper',
    PRIMARY KEY (`guildid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-guild-levels';

CREATE TABLE IF NOT EXISTS `mod_guild_levels_member` (
    `guid`     INT UNSIGNED    NOT NULL COMMENT 'character',
    `guildid`  INT UNSIGNED    NOT NULL,
    `week`     INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT 'weeks since 1970, starting Tuesday',
    `week_xp`  BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `total_xp` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `week_cash` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Cash Flow from their loot this week, copper',
    PRIMARY KEY (`guid`),
    KEY `idx_guildid` (`guildid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-guild-levels';
