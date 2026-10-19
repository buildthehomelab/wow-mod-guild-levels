-- GuildLevels window: the guild's level and XP bar, your guild reputation, then four tabs: every
-- perk (the ones the guild has in colour, the rest grayed with the level they come at), every
-- guild reward with what it needs, the guild news, and this week's guild XP by member. All stock
-- Blizzard art; with DragonUI its own guild window shows the same instead. A "Guild Level" button
-- on the guild tab of the social window opens it, as does /glevel.

local GL = GuildLevels
local WIDTH, HEIGHT = 384, 506
local ROW_HEIGHT = 46
local CONTENT_WIDTH = WIDTH - 68
local GOLD, GRAY, WHITE = { 1, 0.82, 0 }, { 0.5, 0.5, 0.5 }, { 1, 1, 1 }

local window = CreateFrame("Frame", "GuildLevelsFrame", UIParent)
window:SetSize(WIDTH, HEIGHT)
window:SetPoint("CENTER")
window:SetFrameStrata("HIGH")
window:SetToplevel(true)
window:SetMovable(true)
window:EnableMouse(true)
window:RegisterForDrag("LeftButton")
window:SetScript("OnDragStart", window.StartMoving)
window:SetScript("OnDragStop", window.StopMovingOrSizing)
window:SetClampedToScreen(true)
window:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
window:Hide()
tinsert(UISpecialFrames, "GuildLevelsFrame")

local close = CreateFrame("Button", nil, window, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

-- Title on the stock header plate, cut in three so a long guild name widens it without stretching
-- the ends.
local headerMiddle = window:CreateTexture(nil, "ARTWORK")
headerMiddle:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
headerMiddle:SetTexCoord(0.31, 0.67, 0, 0.63)
headerMiddle:SetPoint("TOP", 0, 12)
headerMiddle:SetSize(100, 40)
local headerLeft = window:CreateTexture(nil, "ARTWORK")
headerLeft:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
headerLeft:SetTexCoord(0.21, 0.31, 0, 0.63)
headerLeft:SetPoint("RIGHT", headerMiddle, "LEFT")
headerLeft:SetSize(30, 40)
local headerRight = window:CreateTexture(nil, "ARTWORK")
headerRight:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
headerRight:SetTexCoord(0.67, 0.77, 0, 0.63)
headerRight:SetPoint("LEFT", headerMiddle, "RIGHT")
headerRight:SetSize(30, 40)

local title = window:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", headerMiddle, "TOP", 0, -14)

local subtitle = window:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
subtitle:SetPoint("TOP", 0, -32)

-- XP bar, as the skill bars on the character window
local bar = CreateFrame("StatusBar", nil, window)
bar:SetSize(271, 15)
bar:SetPoint("TOP", 0, -56)
bar:SetStatusBarTexture("Interface\\PaperDollInfoFrame\\UI-Character-Skills-Bar")
bar:SetStatusBarColor(0.25, 0.75, 0.25)
local barBg = bar:CreateTexture(nil, "BACKGROUND")
barBg:SetAllPoints()
barBg:SetTexture(1, 1, 1, 0.2)
local barBorder = CreateFrame("Frame", nil, bar)
barBorder:SetSize(281, 32)
barBorder:SetPoint("LEFT", -5, 0)
local barBorderTexture = barBorder:CreateTexture(nil, "ARTWORK")
barBorderTexture:SetAllPoints()
barBorderTexture:SetTexture("Interface\\PaperDollInfoFrame\\UI-Character-Skills-BarBorder")
local barText = barBorder:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
barText:SetPoint("CENTER", 0, 1)

local detail = window:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
detail:SetPoint("TOP", bar, "BOTTOM", 0, -10)
detail:SetWidth(WIDTH - 50)

local repLine = window:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
repLine:SetPoint("TOP", detail, "BOTTOM", 0, -4)
repLine:SetWidth(WIDTH - 50)

local cash = window:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
cash:SetPoint("TOP", repLine, "BOTTOM", 0, -4)
cash:SetWidth(WIDTH - 50)

-- Tabs along the bottom, as on the character window
local TABS = { "perks", "rewards", "news", "roster" }
local TAB_TEXT = { perks = "Perks", rewards = "Rewards", news = "News", roster = "Contribution" }
local current = "perks"

-- News comes again only when the last copy is a minute old, so this window and DragonUI's guild
-- window (which asks when it opens) don't both ask for it.
local function requestNews()
    if GL.newsAt and GetTime() - GL.newsAt < 60 then
        return
    end
    if DragonUIGuildFrame and DragonUIGuildFrame:IsShown() then
        return
    end
    GL.Send("NEWS")
end

local function requestTab(key)
    if key == "roster" then
        GL.Send("ROSTER")
    elseif key == "rewards" and not GL.rewards[1] then
        GL.Send("REWARDS")
    elseif key == "news" then
        requestNews()
    end
end

for i, key in ipairs(TABS) do
    local tab = CreateFrame("Button", "GuildLevelsFrameTab" .. i, window, "CharacterFrameTabButtonTemplate")
    tab:SetID(i)
    tab:SetText(TAB_TEXT[key])
    if i == 1 then
        tab:SetPoint("TOPLEFT", window, "BOTTOMLEFT", 11, 2)
    else
        tab:SetPoint("LEFT", _G["GuildLevelsFrameTab" .. (i - 1)], "RIGHT", -15, 0)
    end
    -- The template's own scripts are CharacterFrame's.
    tab:SetScript("OnShow", function(self)
        PanelTemplates_TabResize(self, 0)
    end)
    tab:SetScript("OnClick", function(self)
        PanelTemplates_SetTab(window, self:GetID())
        PlaySound("igCharacterInfoTab")
        current = key
        GuildLevelsDB.tab = key
        requestTab(key)
        GL.RefreshWindow()
    end)
end
PanelTemplates_SetNumTabs(window, #TABS)

-- The list, in a stock options box
local inset = CreateFrame("Frame", nil, window)
inset:SetPoint("TOPLEFT", 16, -146)
inset:SetPoint("BOTTOMRIGHT", -16, 16)
inset:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 5, right = 5, top = 5, bottom = 5 },
})
inset:SetBackdropColor(0.1, 0.1, 0.1)
inset:SetBackdropBorderColor(0.4, 0.4, 0.4)

local scroll = CreateFrame("ScrollFrame", "GuildLevelsScroll", inset, "UIPanelScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", inset, "TOPLEFT", 8, -8)
scroll:SetPoint("BOTTOMRIGHT", inset, "BOTTOMRIGHT", -28, 8)
local content = CreateFrame("Frame", nil, scroll)
content:SetSize(CONTENT_WIDTH, 10)
scroll:SetScrollChild(content)

local perkRows, rosterRows, rewardRows, newsRows = {}, {}, {}, {}
local REWARD_HEIGHT = 40

local function perkRow(i)
    local row = perkRows[i]
    if row then
        return row
    end
    row = CreateFrame("Frame", nil, content)
    row:SetSize(CONTENT_WIDTH, ROW_HEIGHT)
    row:SetPoint("TOPLEFT", 0, -(i - 1) * ROW_HEIGHT)
    row.icon = row:CreateTexture(nil, "ARTWORK")
    row.icon:SetSize(32, 32)
    row.icon:SetPoint("TOPLEFT", 2, -4)
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, 0)
    row.level = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.level:SetPoint("TOPRIGHT", -4, -4)
    row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.text:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -2)
    row.text:SetWidth(CONTENT_WIDTH - 46)
    row.text:SetJustifyH("LEFT")
    row.text:SetJustifyV("TOP")
    perkRows[i] = row
    return row
end

local function rosterRow(i)
    local row = rosterRows[i]
    if row then
        return row
    end
    row = CreateFrame("Frame", nil, content)
    row:SetSize(CONTENT_WIDTH, 18)
    row:SetPoint("TOPLEFT", 0, -(i - 1) * 18)
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.name:SetPoint("LEFT", 4, 0)
    row.week = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.week:SetPoint("RIGHT", -110, 0)
    row.total = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.total:SetPoint("RIGHT", -4, 0)
    rosterRows[i] = row
    return row
end

local function color(fs, c)
    fs:SetTextColor(c[1], c[2], c[3])
end

local function drawPerks()
    for i, perk in ipairs(GL.perks) do
        local row = perkRow(i)
        local have = GL.level >= perk.level
        row.icon:SetTexture("Interface\\Icons\\" .. (GL.ICONS[perk.level] or "INV_Misc_QuestionMark"))
        row.icon:SetDesaturated(not have)
        row.name:SetText(perk.name)
        color(row.name, have and GOLD or GRAY)
        row.level:SetText("Level " .. perk.level)
        color(row.level, have and WHITE or GRAY)
        row.text:SetText(perk.description)
        color(row.text, have and WHITE or GRAY)
        row:Show()
    end
    for i = #GL.perks + 1, #perkRows do
        perkRows[i]:Hide()
    end
    content:SetHeight(math.max(10, #GL.perks * ROW_HEIGHT))
end

local function rewardRow(i)
    local row = rewardRows[i]
    if row then
        return row
    end
    row = CreateFrame("Frame", nil, content)
    row:SetSize(CONTENT_WIDTH, REWARD_HEIGHT)
    row:EnableMouse(true)
    row.icon = row:CreateTexture(nil, "ARTWORK")
    row.icon:SetSize(30, 30)
    row.icon:SetPoint("TOPLEFT", 2, -4)
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, 0)
    row.name:SetWidth(CONTENT_WIDTH - 136)
    row.name:SetJustifyH("LEFT")
    row.price = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.price:SetPoint("TOPRIGHT", -4, -4)
    row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.text:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3)
    row.header = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.header:SetPoint("LEFT", 4, 0)
    row:SetScript("OnEnter", function(self)
        if not self.reward then
            return
        end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetHyperlink("item:" .. self.reward.item)
        GL.AddRewardRequirements(GameTooltip, self.reward)
    end)
    row:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    rewardRows[i] = row
    return row
end

-- Item names come from the client's item cache; a missing one is asked for and drawn when it
-- arrives (3.3.5 has no event for that, so the tab looks again a few times).
local nameQuery = CreateFrame("GameTooltip", "GuildLevelsNameQuery", UIParent, "GameTooltipTemplate")
local retries, retryTimer = 0, 0
local retryFrame = CreateFrame("Frame")
retryFrame:Hide()
retryFrame:SetScript("OnUpdate", function(self, elapsed)
    retryTimer = retryTimer + elapsed
    if retryTimer < 1 then
        return
    end
    retryTimer = 0
    retries = retries + 1
    self:Hide()
    GL.RefreshWindow()
end)

local function drawRewards()
    local RED, GREEN = "|cffff2020", "|cff40e040"
    local count, y, missing = 0, 0, false
    local lastCategory
    for _, reward in ipairs(GL.rewards) do
        if GL.RewardForMe(reward) then
            if reward.category ~= lastCategory then
                lastCategory = reward.category
                count = count + 1
                local header = rewardRow(count)
                header.reward = nil
                header:SetHeight(22)
                header:SetPoint("TOPLEFT", 0, -y)
                header.header:SetText(GL.REWARD_CATEGORIES[reward.category] or "Other")
                header.icon:Hide()
                header.name:SetText("")
                header.price:SetText("")
                header.text:SetText("")
                header:Show()
                y = y + 22
            end

            count = count + 1
            local row = rewardRow(count)
            row.reward = reward
            row:SetHeight(REWARD_HEIGHT)
            row:SetPoint("TOPLEFT", 0, -y)
            row.header:SetText("")
            local name, _, quality = GetItemInfo(reward.item)
            if not name then
                missing = true
                nameQuery:SetOwner(UIParent, "ANCHOR_NONE")
                nameQuery:SetHyperlink("item:" .. reward.item)
                name, quality = "Item " .. reward.item, 1
            end
            local guildLow, repLow = GL.RewardLocks(reward)
            local locked = guildLow or repLow
            row.icon:SetTexture(GetItemIcon(reward.item) or "Interface\\Icons\\INV_Misc_QuestionMark")
            row.icon:SetDesaturated(locked)
            row.icon:Show()
            local r, g, b = GetItemQualityColor(quality or 1)
            row.name:SetText(name)
            if locked then
                row.name:SetTextColor(0.5, 0.5, 0.5)
            else
                row.name:SetTextColor(r, g, b)
            end
            row.price:SetText(GL.Money(reward.price))
            row.text:SetText((guildLow and RED or GREEN) .. "Guild level " .. reward.level .. "|r   "
                .. (repLow and RED or GREEN) .. GL.RankName(reward.rank) .. "|r")
            row:Show()
            y = y + REWARD_HEIGHT
        end
    end
    for i = count + 1, #rewardRows do
        rewardRows[i]:Hide()
    end
    content:SetHeight(math.max(10, y))
    if count == 0 then
        local row = rewardRow(1)
        row.reward = nil
        row:SetPoint("TOPLEFT", 0, 0)
        row.icon:Hide()
        row.name:SetText("")
        row.price:SetText("")
        row.text:SetText("")
        row.header:SetText(GL.connected and "No guild rewards on this realm." or "Waiting for the server...")
        row:Show()
    end
    if missing and retries < 10 then
        retryFrame:Show()
    end
end

local function drawRoster()
    local header = rosterRow(1)
    header.name:SetText("Member")
    header.week:SetText("This week")
    header.total:SetText("Total")
    for _, fs in ipairs({ header.name, header.week, header.total }) do
        color(fs, GOLD)
    end
    header:Show()

    local count = 1
    for _, entry in ipairs(GL.roster) do
        count = count + 1
        local row = rosterRow(count)
        row.name:SetText(entry.name)
        row.week:SetText(GL.Number(entry.week))
        row.total:SetText(GL.Number(entry.total))
        for _, fs in ipairs({ row.name, row.week, row.total }) do
            color(fs, entry.name == UnitName("player") and GOLD or WHITE)
        end
        row:Show()
    end
    for i = count + 1, #rosterRows do
        rosterRows[i]:Hide()
    end
    content:SetHeight(math.max(10, count * 18))
end

-- News
local NEWS_HEIGHT = 34

local function newsRow(i)
    local row = newsRows[i]
    if row then
        return row
    end
    row = CreateFrame("Button", nil, content)
    row:SetSize(CONTENT_WIDTH, NEWS_HEIGHT)
    row:SetPoint("TOPLEFT", 0, -(i - 1) * NEWS_HEIGHT)
    row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
    row:GetHighlightTexture():SetBlendMode("ADD")
    row.icon = row:CreateTexture(nil, "ARTWORK")
    row.icon:SetSize(26, 26)
    row.icon:SetPoint("LEFT", 2, 0)
    row.time = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    row.time:SetPoint("TOPRIGHT", -2, -3)
    row.time:SetJustifyH("RIGHT")
    row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.text:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 6, 2)
    row.text:SetPoint("RIGHT", -64, 0)
    row.text:SetHeight(30)
    row.text:SetJustifyH("LEFT")
    row.text:SetJustifyV("MIDDLE")
    row:SetScript("OnEnter", function(self)
        if not self.link then
            return
        end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetHyperlink(self.link)
        GameTooltip:Show()
    end)
    row:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    row:SetScript("OnClick", function(self)
        if self.chatLink and IsModifiedClick("CHATLINK") then
            ChatEdit_InsertLink(self.chatLink)
        end
    end)
    newsRows[i] = row
    return row
end

local function ago(seconds)
    seconds = math.max(0, seconds)
    if seconds < 60 then
        return "Just now"
    elseif seconds < 3600 then
        return format("%d |4minute:minutes; ago", math.floor(seconds / 60))
    elseif seconds < 86400 then
        return format("%d |4hour:hours; ago", math.floor(seconds / 3600))
    end
    return format("%d |4day:days; ago", math.floor(seconds / 86400))
end

local function member(name)
    return "|cffffffff" .. (name or UNKNOWN) .. "|r"
end

-- Text, icon and the hyperlink for the tooltip of one news item; nil for a type this addon
-- doesn't know.
local function newsItem(item)
    if item.type == GL.NEWS_GUILD_LEVEL then
        local text = format("The guild reached |cff40ff40level %d|r.", item.value)
        if item.text ~= "" then
            text = text .. format(" New perk: |cffffd200%s|r", item.text)
        end
        return text, "Interface\\Icons\\Achievement_Reputation_01"
    elseif item.type == GL.NEWS_ENCOUNTER then
        local boss = "|cffffd200" .. (item.text ~= "" and item.text or UNKNOWN) .. "|r"
        if item.text2 ~= "" then
            return format("Guild group defeated %s in %s.", boss, item.text2), "Interface\\Icons\\INV_Misc_Head_Dragon_01"
        end
        return format("Guild group defeated %s.", boss), "Interface\\Icons\\INV_Misc_Head_Dragon_01"
    elseif item.type == GL.NEWS_ITEM_LOOTED or item.type == GL.NEWS_ITEM_CRAFTED then
        local _, link, _, _, _, _, _, _, _, icon = GetItemInfo(item.value)
        if not link then
            local c = ITEM_QUALITY_COLORS[item.extra] or ITEM_QUALITY_COLORS[4]
            link = format("%s|Hitem:%d:0:0:0:0:0:0:0:0|h[%s]|h|r", c.hex, item.value,
                item.text ~= "" and item.text or ("Item " .. item.value))
        end
        local verb = item.type == GL.NEWS_ITEM_CRAFTED and "%s crafted %s." or "%s obtained %s."
        return format(verb, member(item.who), link), icon or "Interface\\Icons\\INV_Misc_QuestionMark",
            "item:" .. item.value, link
    elseif item.type == GL.NEWS_ACHIEVEMENT then
        local _, name, _, _, _, _, _, _, _, icon = GetAchievementInfo(item.value)
        local link = GetAchievementLink(item.value)
        return format("%s earned %s.", member(item.who), link or ("|cffffff00[" .. (name or item.text) .. "]|r")),
            icon or "Interface\\Icons\\Achievement_General", link, link
    end
end

local function drawNews()
    local now = time() + (GL.newsClockOffset or 0)
    local count = 0
    for _, item in ipairs(GL.news) do
        local text, icon, link, chatLink = newsItem(item)
        if text then
            count = count + 1
            local row = newsRow(count)
            row.icon:SetTexture(icon)
            row.icon:Show()
            row.text:SetText(text)
            row.time:SetText(ago(now - (item.time or now)))
            row.link, row.chatLink = link, chatLink
            row:Show()
        end
    end
    if count == 0 then
        count = 1
        local row = newsRow(1)
        row.icon:Hide()
        row.time:SetText("")
        row.text:SetText(GL.newsAt and "Nothing has happened in the guild yet." or "Waiting for the server...")
        row.link, row.chatLink = nil, nil
        row:Show()
    end
    for i = count + 1, #newsRows do
        newsRows[i]:Hide()
    end
    content:SetHeight(math.max(10, count * NEWS_HEIGHT))
end

function GL.RefreshWindow()
    if not window:IsShown() then
        return
    end

    local guild = GetGuildInfo("player")
    if not GL.connected then
        title:SetText("Guild Levels")
        subtitle:SetText("Waiting for the server (mod-guild-levels)...")
    elseif not guild or GL.level == 0 then
        title:SetText("Guild Levels")
        subtitle:SetText("You are not in a guild.")
    else
        title:SetText(guild)
        subtitle:SetText("Guild level " .. GL.level)
    end

    if GL.level >= GL.MAX_LEVEL then
        bar:SetMinMaxValues(0, 1)
        bar:SetValue(1)
        barText:SetText("Maximum level")
    else
        bar:SetMinMaxValues(0, math.max(1, GL.needed))
        bar:SetValue(GL.xp)
        barText:SetText(GL.Number(GL.xp) .. " / " .. GL.Number(GL.needed))
    end

    local parts = {}
    if GL.cap > 0 then
        parts[#parts + 1] = "Today: " .. GL.Number(GL.today) .. " / " .. GL.Number(GL.cap)
    end
    parts[#parts + 1] = "You this week: " .. GL.Number(GL.myWeek)
    local nextPerk = GL.NextPerk()
    if nextPerk then
        parts[#parts + 1] = "Next: " .. nextPerk.name .. " (level " .. nextPerk.level .. ")"
    end
    detail:SetText(table.concat(parts, "   "))

    if GL.level > 0 then
        local into, size = GL.RankProgress()
        local text = "Guild reputation: " .. GL.RankName(GL.repRank)
        if GL.repRank < 7 then
            text = text .. " " .. GL.Number(into) .. " / " .. GL.Number(size)
        end
        if GL.repCap > 0 then
            text = text .. "   This week: " .. GL.Number(GL.repWeek) .. " / " .. GL.Number(GL.repCap)
        end
        repLine:SetText(text)
    else
        repLine:SetText("")
    end

    -- Cash Flow (guild level 5) pays into the bank without a word; this is where it shows.
    if GL.level >= 5 then
        cash:SetText("Cash Flow this week: " .. GL.Money(GL.cashWeek) .. "  (you: " .. GL.Money(GL.myCashWeek) .. ")")
    else
        cash:SetText("")
    end

    headerMiddle:SetWidth(math.max(100, title:GetStringWidth() + 20))

    for i, key in ipairs(TABS) do
        if key == current then
            PanelTemplates_SetTab(window, i)
        end
    end

    for _, row in ipairs(perkRows) do
        row:Hide()
    end
    for _, row in ipairs(rosterRows) do
        row:Hide()
    end
    for _, row in ipairs(rewardRows) do
        row:Hide()
    end
    for _, row in ipairs(newsRows) do
        row:Hide()
    end
    if current == "roster" then
        drawRoster()
    elseif current == "rewards" then
        drawRewards()
    elseif current == "news" then
        drawNews()
    else
        drawPerks()
    end
end

function GL.Toggle()
    if window:IsShown() then
        window:Hide()
        return
    end
    window:Show()
    if not GL.perks[1] then
        GL.Send("HELLO")
    else
        GL.Send("STATE")
    end
    -- Before HELLO is answered the rewards come with it.
    if current ~= "rewards" or GL.perks[1] then
        requestTab(current)
    end
    retries = 0
    GL.RefreshWindow()
end

window:SetScript("OnShow", function()
    PlaySound("igCharacterInfoOpen")
end)
window:SetScript("OnHide", function()
    PlaySound("igCharacterInfoClose")
end)

-- The button on the guild tab of the social window.
local guildButton
function GL.RefreshGuildButton()
    if not guildButton then
        return
    end
    if GL.connected and GL.level > 0 then
        guildButton:SetText("Guild Level " .. GL.level)
        guildButton:Show()
    else
        guildButton:Hide()
    end
end

local function makeGuildButton()
    if guildButton or not GuildFrame then
        return
    end
    guildButton = CreateFrame("Button", "GuildLevelsGuildButton", GuildFrame, "UIPanelButtonTemplate")
    guildButton:SetSize(120, 22)
    guildButton:SetPoint("TOPRIGHT", GuildFrame, "TOPRIGHT", -40, -42)
    guildButton:SetScript("OnClick", GL.Toggle)
    guildButton:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine("Guild Level " .. GL.level)
        if GL.level < GL.MAX_LEVEL then
            GameTooltip:AddLine(GL.Number(GL.xp) .. " / " .. GL.Number(GL.needed) .. " guild XP", 1, 1, 1)
        end
        local nextPerk = GL.NextPerk()
        if nextPerk then
            GameTooltip:AddLine("Next perk: " .. nextPerk.name .. " (level " .. nextPerk.level .. ")", 1, 0.82, 0)
        end
        GameTooltip:Show()
    end)
    guildButton:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    GL.RefreshGuildButton()
end

local loader = CreateFrame("Frame")
loader:RegisterEvent("PLAYER_LOGIN")
loader:SetScript("OnEvent", function()
    -- Saved variables are only there once the addon has loaded.
    GuildLevelsDB = GuildLevelsDB or {}
    current = TAB_TEXT[GuildLevelsDB.tab] and GuildLevelsDB.tab or "perks"
    makeGuildButton()
    if GuildFrame then
        GuildFrame:HookScript("OnShow", function()
            GL.Send("STATE")
        end)
    end
end)
