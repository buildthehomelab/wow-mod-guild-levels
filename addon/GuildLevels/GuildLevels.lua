-- GuildLevels: the client half of mod-guild-levels. Asks the server for the guild's level, XP
-- and perks, and keeps them for Window.lua to draw.
--
-- Wire format, both directions: an addon whisper to yourself, "<COMMAND>:<field>:<field>...".
--   to the server    HELLO, STATE, ROSTER, NEWS, REWARDS
--   from the server  S:<level>:<xp>:<needed>:<today>:<daily cap>:<total>:<my week>:<my total>
--                      :<guild Cash Flow this week>:<guild Cash Flow total>:<my Cash Flow this week>
--                      :<my guild rep>:<my rep rank, 3 Neutral..7 Exalted>:<my rep this week>:<weekly cap>
--                    V:<creature entry>,...   the guild vendors
--                    W:<item>,<team>,<guild level>,<rep rank>,<price>,<category>;... ... WEND
--                      the guild rewards; team 0 both, 1 Alliance, 2 Horde; price in copper
--                    UP:<level>:<perk name>   (the guild just levelled up)
--                    K:<level>:<name>:<description> ... KEND
--                    R:<name>,<week>,<total>;... ... REND
--                    N:<type>,<time>,<member>,<value>,<extra>,<text>,<text2>;... ... NEND:<server time>
--                      newest first. type 0 guild level (value level, text perk), 1 boss (value
--                      encounter, extra difficulty, text boss, text2 instance), 2 item looted and
--                      3 item crafted (value item, extra quality, text item name), 4 achievement
--                      (value id, extra points, text name).
--
-- Other addons (DragonUI's guild window) read the GuildLevels table and set GL.OnUpdate to hear
-- about new data.

local PREFIX = "GuildLevels"
local MAX_LEVEL = 25

GuildLevels = {
    connected = false,  -- the server answered, so mod-guild-levels is there
    level = 0,          -- 0: not in a guild
    xp = 0, needed = 0, today = 0, cap = 0, total = 0,
    myWeek = 0, myTotal = 0,
    cashWeek = 0, cashTotal = 0, myCashWeek = 0,   -- copper
    perks = {},         -- in level order: { level, name, description }
    roster = {},        -- { name, week, total }, most this week first
    news = {},          -- { type, time, who, value, extra, text, text2 }, newest first
    rep = 0, repRank = 3, repWeek = 0, repCap = 0,  -- my guild reputation
    rewards = {},       -- in vendor order: { item, team, level, rank, price, category }
    vendors = {},       -- [creature entry] = true for the guild vendors
    REWARD_CATEGORIES = { "Tabards", "Heirlooms", "Companions", "Mounts", "Consumables" },
    newsClockOffset = 0,    -- server time minus time(), for "3 hours ago"
    NEWS_GUILD_LEVEL = 0, NEWS_ENCOUNTER = 1, NEWS_ITEM_LOOTED = 2, NEWS_ITEM_CRAFTED = 3, NEWS_ACHIEVEMENT = 4,
    MAX_LEVEL = MAX_LEVEL,
}
local GL = GuildLevels
local pendingPerks, pendingRoster, pendingNews, pendingRewards

-- Icons for the perk list, by the level the perk comes with.
GL.ICONS = {
    [2] = "Spell_Holy_BorrowedTime", [3] = "Ability_Mount_RidingHorse", [4] = "Achievement_Reputation_01",
    [5] = "INV_Misc_Coin_02", [6] = "Spell_Holy_BorrowedTime", [7] = "INV_Hammer_20",
    [8] = "Ability_Mount_Gryphon_01", [9] = "INV_Hammer_20", [10] = "INV_Alchemy_EndlessFlask_06",
    [11] = "INV_Box_01", [12] = "Achievement_Reputation_01", [13] = "INV_BannerPVP_02",
    [14] = "Trade_BlackSmithing", [15] = "Spell_Holy_GuardianSpirit", [16] = "INV_Misc_Coin_02",
    [17] = "INV_Potion_54", [18] = "INV_Misc_Coin_17", [19] = "INV_BannerPVP_02",
    [20] = "Trade_Alchemy", [21] = "Spell_Shadow_Twilight", [22] = "INV_Alchemy_EndlessFlask_06",
    [23] = "INV_Misc_Bag_10_Blue", [24] = "INV_Misc_Coin_01", [25] = "Spell_Holy_Resurrection",
}

function GL.Send(message)
    SendAddonMessage(PREFIX, message, "WHISPER", UnitName("player"))
end

function GL.Refresh()
    if GL.RefreshWindow then
        GL.RefreshWindow()
    end
    if GL.RefreshGuildButton then
        GL.RefreshGuildButton()
    end
    if GL.OnUpdate then
        GL.OnUpdate()
    end
end

local function onMessage(message)
    local command, rest = message:match("^(%u+):?(.*)$")
    if command == "S" then
        local v = {}
        for n in rest:gmatch("[^:]+") do
            v[#v + 1] = tonumber(n) or 0
        end
        GL.connected = true
        GL.level, GL.xp, GL.needed, GL.today, GL.cap, GL.total, GL.myWeek, GL.myTotal =
            v[1] or 0, v[2] or 0, v[3] or 0, v[4] or 0, v[5] or 0, v[6] or 0, v[7] or 0, v[8] or 0
        GL.cashWeek, GL.cashTotal, GL.myCashWeek = v[9] or 0, v[10] or 0, v[11] or 0
        GL.rep, GL.repRank, GL.repWeek, GL.repCap = v[12] or 0, v[13] or 3, v[14] or 0, v[15] or 0
        GL.Refresh()
    elseif command == "V" then
        GL.vendors = {}
        for entry in rest:gmatch("%d+") do
            GL.vendors[tonumber(entry)] = true
        end
    elseif command == "W" then
        pendingRewards = pendingRewards or {}
        for row in rest:gmatch("[^;]+") do
            local item, team, level, rank, price, category = row:match("^(%d+),(%d+),(%d+),(%d+),(%d+),(%d+)$")
            if item then
                pendingRewards[#pendingRewards + 1] = {
                    item = tonumber(item), team = tonumber(team), level = tonumber(level),
                    rank = tonumber(rank), price = tonumber(price), category = tonumber(category),
                }
            end
        end
    elseif command == "WEND" then
        GL.rewards = pendingRewards or {}
        pendingRewards = nil
        GL.Refresh()
    elseif command == "UP" then
        local level, perk = rest:match("^(%d+):(.*)$")
        if level and GL.ShowLevelUp then
            GL.ShowLevelUp(tonumber(level), perk)
        end
    elseif command == "K" then
        local level, name, description = rest:match("^(%d+):([^:]*):(.*)$")
        if level then
            pendingPerks = pendingPerks or {}
            pendingPerks[#pendingPerks + 1] = { level = tonumber(level), name = name, description = description }
        end
    elseif command == "KEND" then
        GL.perks = pendingPerks or {}
        pendingPerks = nil
        GL.Refresh()
    elseif command == "R" then
        pendingRoster = pendingRoster or {}
        for row in rest:gmatch("[^;]+") do
            local name, week, total = row:match("^([^,]+),(%d+),(%d+)$")
            if name then
                pendingRoster[#pendingRoster + 1] = { name = name, week = tonumber(week), total = tonumber(total) }
            end
        end
    elseif command == "REND" then
        GL.roster = pendingRoster or {}
        pendingRoster = nil
        GL.Refresh()
    elseif command == "N" then
        pendingNews = pendingNews or {}
        for row in rest:gmatch("[^;]+") do
            local t, when, who, value, extra, text, text2 = row:match("^(%d+),(%d+),([^,]*),(%d+),(%d+),([^,]*),(.*)$")
            if t then
                pendingNews[#pendingNews + 1] = {
                    type = tonumber(t), time = tonumber(when), who = who ~= "" and who or nil,
                    value = tonumber(value), extra = tonumber(extra), text = text, text2 = text2,
                }
            end
        end
    elseif command == "NEND" then
        GL.news = pendingNews or {}
        pendingNews = nil
        local now = tonumber(rest)
        if now then
            GL.newsClockOffset = now - time()
        end
        GL.Refresh()
    end
end

-- The next perk the guild will get, or nil at the cap.
function GL.NextPerk()
    for _, perk in ipairs(GL.perks) do
        if perk.level > GL.level then
            return perk
        end
    end
end

-- Guild reputation. Ranks are the core's: 0 Hated .. 3 Neutral .. 7 Exalted.
GL.RANK_FLOOR = { [0] = -42000, -6000, -3000, 0, 3000, 9000, 21000, 42000 }
GL.RANK_TOP = 42999

function GL.RankName(rank)
    return _G["FACTION_STANDING_LABEL" .. ((rank or 3) + 1)] or "?"
end

-- Progress within my current rank: value, size of the rank.
function GL.RankProgress()
    local floor = GL.RANK_FLOOR[GL.repRank] or 0
    local top = GL.RANK_FLOOR[GL.repRank + 1] or (GL.RANK_TOP + 1)
    return GL.rep - floor, top - floor
end

-- Is this reward for my faction?
function GL.RewardForMe(reward)
    if reward.team == 0 then
        return true
    end
    return (reward.team == 1) == (UnitFactionGroup("player") == "Alliance")
end

-- What's missing to buy it: guild, guild level, rep. Both are nil once it can be bought.
function GL.RewardLocks(reward)
    local guildLow = GL.level < reward.level
    local repLow = GL.repRank < reward.rank
    if GL.level == 0 then
        guildLow = true
    end
    return guildLow and true or nil, repLow and true or nil
end

function GL.RewardFor(item)
    for _, reward in ipairs(GL.rewards) do
        if reward.item == item then
            return reward
        end
    end
end

-- The creature entry of a 3.3.5 creature GUID ("0xF130" .. 6 hex entry .. 6 hex counter).
function GL.GuidEntry(guid)
    if not guid or guid:sub(3, 6) ~= "F130" then
        return nil
    end
    return tonumber(guid:sub(7, 12), 16)
end

function GL.AtGuildVendor()
    local entry = GL.GuidEntry(UnitGUID("npc"))
    return entry and GL.vendors[entry] or false
end

-- Copper as "12g 34s 56c", in coin colours, leaving out the empty leading parts.
function GL.Money(copper)
    copper = math.floor(copper or 0)
    local gold, silver, rest = math.floor(copper / 10000), math.floor(copper / 100) % 100, copper % 100
    local parts = {}
    if gold > 0 then
        parts[#parts + 1] = "|cffffd700" .. GL.Number(gold) .. "g|r"
    end
    if gold > 0 or silver > 0 then
        parts[#parts + 1] = "|cffc7c7cf" .. silver .. "s|r"
    end
    parts[#parts + 1] = "|cffeda55f" .. rest .. "c|r"
    return table.concat(parts, " ")
end

-- 1234567 -> "1,234,567"
function GL.Number(n)
    local text = tostring(math.floor(n or 0))
    local out = text:reverse():gsub("(%d%d%d)", "%1,"):reverse()
    return (out:gsub("^,", ""))
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("PLAYER_GUILD_UPDATE")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:RegisterEvent("UPDATE_FACTION")
-- Rep changes come in bursts (a boss gives everyone rep at once): ask for the state once, a moment
-- after the last one, and only while something shows it.
local stateDelay
frame:SetScript("OnUpdate", function(self, elapsed)
    if stateDelay then
        stateDelay = stateDelay - elapsed
        if stateDelay <= 0 then
            stateDelay = nil
            GL.Send("STATE")
        end
    end
end)
frame:SetScript("OnEvent", function(self, event, prefix, message, channel, sender)
    if event == "UPDATE_FACTION" then
        if GL.connected and ((GuildLevelsFrame and GuildLevelsFrame:IsShown()) or (MerchantFrame and MerchantFrame:IsShown())) then
            stateDelay = 1.5
        end
        return
    end
    if event == "CHAT_MSG_ADDON" then
        if prefix == PREFIX and channel == "WHISPER" and sender == UnitName("player") then
            onMessage(message)
        end
    elseif event == "PLAYER_ENTERING_WORLD" then
        if not GL.helloSent then
            GL.helloSent = true
            GL.Send("HELLO")
        end
    elseif event == "PLAYER_GUILD_UPDATE" then
        GL.Send("STATE")
    end
end)

SLASH_GUILDLEVELS1 = "/guildlevels"
SLASH_GUILDLEVELS2 = "/glevel"
SlashCmdList.GUILDLEVELS = function()
    if GL.Toggle then
        GL.Toggle()
    end
end
