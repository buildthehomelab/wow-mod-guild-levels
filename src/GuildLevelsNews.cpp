/*
 * mod-guild-levels: the guild news feed, like Cataclysm's Guild News.
 *
 * Kept per guild, newest first, at most GuildLevels.News.Keep items: the guild reaching a level,
 * dungeon and raid bosses killed by a guild group, epic (GuildLevels.News.ItemQuality) items a
 * member looted, won in a roll or crafted, and achievements a member earned. Bots' loot and
 * achievements only count with GuildLevels.News.Bots. Items handed out by a master looter don't
 * make the news: the core stores them without any script hook.
 *
 * Items arrive from map threads, so the feed sits behind its own mutex. New items are written
 * with the rest of the module's state every save interval, and each guild's older rows are
 * pruned then.
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "AchievementMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace GuildLevels
{
    namespace
    {
        struct NewsConfig
        {
            bool enabled = true;
            uint32 keep = 100;
            uint32 itemQuality = ITEM_QUALITY_EPIC;
            bool bots = false;
        } newsConfig;

        std::mutex newsLock;
        uint32 nextId = 1;
        std::unordered_map<uint32, std::deque<NewsItem>> feeds;    // newest first
        std::vector<std::pair<uint32, NewsItem>> unsaved;
        std::unordered_set<uint32> pruneGuilds;
        std::unordered_set<uint32> deletedFeeds;

        // Hooks run on map threads while .reload config may rewrite the settings.
        NewsConfig Settings()
        {
            std::lock_guard<std::mutex> guard(newsLock);
            return newsConfig;
        }

        bool Counts(Player const* player, NewsConfig const& settings)
        {
            return settings.enabled && player && player->GetGuildId() && (settings.bots || IsRealPlayer(player));
        }

        void AddItemNews(Player* player, Item const* item, NewsType type)
        {
            NewsConfig settings = Settings();
            if (!Counts(player, settings) || !item)
                return;
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto || proto->Quality < settings.itemQuality)
                return;
            AddNews(player->GetGuildId(), type, player->GetGUID().GetCounter(), proto->ItemId, proto->Quality);
        }
    }

    void LoadNewsConfig()
    {
        NewsConfig c;
        c.enabled = sConfigMgr->GetOption<bool>("GuildLevels.News.Enable", true);
        c.keep = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("GuildLevels.News.Keep", 100), 10, 500);
        c.itemQuality = std::min<uint32>(ITEM_QUALITY_ARTIFACT, sConfigMgr->GetOption<uint32>("GuildLevels.News.ItemQuality", ITEM_QUALITY_EPIC));
        c.bots = sConfigMgr->GetOption<bool>("GuildLevels.News.Bots", false);
        std::lock_guard<std::mutex> guard(newsLock);
        newsConfig = c;
    }

    void AddNews(uint32 guildId, NewsType type, ObjectGuid::LowType guid, uint32 value, uint32 extra)
    {
        if (!guildId)
            return;
        std::lock_guard<std::mutex> guard(newsLock);
        if (!newsConfig.enabled)
            return;

        NewsItem news{ nextId++, uint8(type), uint32(GameTime::GetGameTime().count()), guid, value, extra };
        std::deque<NewsItem>& feed = feeds[guildId];
        feed.push_front(news);
        while (feed.size() > newsConfig.keep)
            feed.pop_back();
        unsaved.emplace_back(guildId, news);
        pruneGuilds.insert(guildId);
        deletedFeeds.erase(guildId);
    }

    std::vector<NewsItem> GetNews(uint32 guildId)
    {
        std::lock_guard<std::mutex> guard(newsLock);
        auto itr = feeds.find(guildId);
        if (itr == feeds.end())
            return {};
        return { itr->second.begin(), itr->second.end() };
    }

    void ForgetGuildNews(uint32 guildId)
    {
        std::lock_guard<std::mutex> guard(newsLock);
        feeds.erase(guildId);
        pruneGuilds.erase(guildId);
        std::erase_if(unsaved, [guildId](auto const& row) { return row.first == guildId; });
        deletedFeeds.insert(guildId);
    }

    void LoadNews()
    {
        std::lock_guard<std::mutex> guard(newsLock);
        feeds.clear();
        unsaved.clear();
        nextId = 1;

        uint32 count = 0;
        if (QueryResult result = CharacterDatabase.Query("SELECT id, guildid, type, time, guid, value, extra FROM mod_guild_levels_news ORDER BY id DESC"))
        {
            do
            {
                Field* f = result->Fetch();
                NewsItem news{ f[0].Get<uint32>(), f[2].Get<uint8>(), f[3].Get<uint32>(), f[4].Get<uint32>(), f[5].Get<uint32>(), f[6].Get<uint32>() };
                uint32 guildId = f[1].Get<uint32>();
                nextId = std::max(nextId, news.id + 1);
                if (!sGuildMgr->GetGuildById(guildId))
                {
                    deletedFeeds.insert(guildId);
                    continue;
                }
                std::deque<NewsItem>& feed = feeds[guildId];
                if (feed.size() < newsConfig.keep)
                {
                    feed.push_back(news);
                    ++count;
                }
                else
                    pruneGuilds.insert(guildId);
            } while (result->NextRow());
        }
        LOG_INFO("module", "mod-guild-levels: {} guild news items loaded", count);
    }

    void SaveNews(CharacterDatabaseTransaction trans, bool& any)
    {
        std::lock_guard<std::mutex> guard(newsLock);
        for (uint32 guildId : deletedFeeds)
        {
            trans->Append("DELETE FROM mod_guild_levels_news WHERE guildid = {}", guildId);
            any = true;
        }
        for (auto const& [guildId, n] : unsaved)
        {
            trans->Append("INSERT INTO mod_guild_levels_news (id, guildid, type, time, guid, value, extra) VALUES ({}, {}, {}, {}, {}, {}, {})",
                n.id, guildId, n.type, n.time, n.guid, n.value, n.extra);
            any = true;
        }
        // Rows older than the oldest one kept in memory are gone from the feed.
        for (uint32 guildId : pruneGuilds)
        {
            auto itr = feeds.find(guildId);
            if (itr == feeds.end() || itr->second.empty())
                continue;
            trans->Append("DELETE FROM mod_guild_levels_news WHERE guildid = {} AND id < {}", guildId, itr->second.back().id);
            any = true;
        }
        deletedFeeds.clear();
        unsaved.clear();
        pruneGuilds.clear();
    }
}

using namespace GuildLevels;

class GuildLevelsNewsPlayerScript : public PlayerScript
{
public:
    GuildLevelsNewsPlayerScript() : PlayerScript("GuildLevelsNewsPlayerScript", {
        PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM, PLAYERHOOK_ON_CREATE_ITEM,
        PLAYERHOOK_ON_ACHI_COMPLETE }) { }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override
    {
        AddItemNews(player, item, NEWS_ITEM_LOOTED);
    }

    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 /*count*/, RollVote /*voteType*/, Roll* /*roll*/) override
    {
        AddItemNews(player, item, NEWS_ITEM_LOOTED);
    }

    void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
    {
        AddItemNews(player, item, NEWS_ITEM_CRAFTED);
    }

    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        if (!Counts(player, Settings()) || !achievement || (achievement->flags & ACHIEVEMENT_FLAG_COUNTER))
            return;
        AddNews(player->GetGuildId(), NEWS_ACHIEVEMENT, player->GetGUID().GetCounter(), achievement->ID, achievement->points);
    }
};

void AddGuildLevelsNewsScripts()
{
    new GuildLevelsNewsPlayerScript();
}
