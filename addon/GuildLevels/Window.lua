-- GuildLevels window: the guild's level and XP bar, then two tabs: every perk (the ones the
-- guild has in colour, the rest grayed with the level they come at) and this week's guild XP by
-- member. A "Guild Level" button on the guild tab of the social window opens it, as does /glevel.

local GL = GuildLevels
local WIDTH, HEIGHT = 384, 486
local ROW_HEIGHT = 46
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

local title = window:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -20)

local subtitle = window:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
subtitle:SetPoint("TOP", title, "BOTTOM", 0, -4)

-- XP bar
local bar = CreateFrame("StatusBar", nil, window)
bar:SetSize(WIDTH - 60, 16)
bar:SetPoint("TOP", 0, -64)
bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
bar:SetStatusBarColor(0.25, 0.75, 0.25)
local barBg = bar:CreateTexture(nil, "BACKGROUND")
barBg:SetAllPoints()
barBg:SetTexture(0, 0, 0, 0.5)
local barBorder = CreateFrame("Frame", nil, bar)
barBorder:SetPoint("TOPLEFT", -3, 3)
barBorder:SetPoint("BOTTOMRIGHT", 3, -3)
barBorder:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 10 })
barBorder:SetBackdropBorderColor(0.6, 0.6, 0.6)
local barText = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
barText:SetPoint("CENTER")

local detail = window:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
detail:SetPoint("TOP", bar, "BOTTOM", 0, -6)
detail:SetWidth(WIDTH - 50)

local cash = window:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
cash:SetPoint("TOP", detail, "BOTTOM", 0, -4)
cash:SetWidth(WIDTH - 50)

-- Tabs
local tabs, current = {}, "perks"
local function makeTab(key, text, x)
    local tab = CreateFrame("Button", nil, window, "UIPanelButtonTemplate")
    tab:SetSize(110, 22)
    tab:SetPoint("TOPLEFT", x, -134)
    tab:SetText(text)
    tab:SetScript("OnClick", function()
        current = key
        GuildLevelsDB.tab = key
        if key == "roster" then
            GL.Send("ROSTER")
        end
        GL.RefreshWindow()
    end)
    tabs[key] = tab
end
makeTab("perks", "Perks", 24)
makeTab("roster", "Contribution", 138)

-- Scrolling list shared by both tabs
local scroll = CreateFrame("ScrollFrame", "GuildLevelsScroll", window, "UIPanelScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 20, -164)
scroll:SetPoint("BOTTOMRIGHT", -40, 18)
local content = CreateFrame("Frame", nil, scroll)
content:SetSize(WIDTH - 64, 10)
scroll:SetScrollChild(content)

local perkRows, rosterRows = {}, {}

local function perkRow(i)
    local row = perkRows[i]
    if row then
        return row
    end
    row = CreateFrame("Frame", nil, content)
    row:SetSize(WIDTH - 64, ROW_HEIGHT)
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
    row.text:SetWidth(WIDTH - 112)
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
    row:SetSize(WIDTH - 64, 18)
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

    -- Cash Flow (guild level 5) pays into the bank without a word; this is where it shows.
    if GL.level >= 5 then
        cash:SetText("Cash Flow this week: " .. GL.Money(GL.cashWeek) .. "  (you: " .. GL.Money(GL.myCashWeek) .. ")")
    else
        cash:SetText("")
    end

    for key, tab in pairs(tabs) do
        if key == current then
            tab:LockHighlight()
        else
            tab:UnlockHighlight()
        end
    end

    for _, row in ipairs(perkRows) do
        row:Hide()
    end
    for _, row in ipairs(rosterRows) do
        row:Hide()
    end
    if current == "roster" then
        drawRoster()
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
    if current == "roster" then
        GL.Send("ROSTER")
    end
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
    current = GuildLevelsDB.tab == "roster" and "roster" or "perks"
    makeGuildButton()
    if GuildFrame then
        GuildFrame:HookScript("OnShow", function()
            GL.Send("STATE")
        end)
    end
end)
