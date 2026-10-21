-- GuildLevels toast: the guild reached a new level. A banner at the top of the screen with the
-- level and the perk it brought, the level-up sound, and a fade after a few seconds. With DragonUI
-- its own level-up banner shows it instead; this one is the stock fallback.

local GL = GuildLevels
local SHOW, FADE = 5, 1.5

local toast = CreateFrame("Frame", "GuildLevelsToast", UIParent)
toast:SetSize(360, 72)
toast:SetPoint("TOP", 0, -140)
toast:SetFrameStrata("HIGH")
toast:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
toast:Hide()

local toastIcon = toast:CreateTexture(nil, "ARTWORK")
toastIcon:SetSize(40, 40)
toastIcon:SetPoint("LEFT", 18, 0)

local heading = toast:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
heading:SetPoint("TOPLEFT", toastIcon, "TOPRIGHT", 12, -2)

local line = toast:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
line:SetPoint("TOPLEFT", heading, "BOTTOMLEFT", 0, -6)

local elapsed = 0
toast:SetScript("OnUpdate", function(self, delta)
    elapsed = elapsed + delta
    if elapsed > SHOW + FADE then
        self:Hide()
    elseif elapsed > SHOW then
        self:SetAlpha(1 - (elapsed - SHOW) / FADE)
    end
end)
toast:SetScript("OnMouseDown", function(self)
    self:Hide()
    if GL.Toggle then
        GL.Toggle()
    end
end)
toast:EnableMouse(true)

local function PerkDescription(level)
    for _, perk in ipairs(GL.perks) do
        if perk.level == level then
            return perk.description
        end
    end
end

function GL.ShowLevelUp(level, perk)
    local icon = "Interface\\Icons\\" .. (GL.ICONS[level] or "INV_Misc_TabardPVP_02")
    PlaySoundFile("Sound\\Interface\\LevelUp.wav")
    local dragon = _G.DragonUI
    if dragon and dragon.ShowGuildLevelUp and dragon.ShowGuildLevelUp(level, perk, icon, PerkDescription(level)) then
        return
    end
    heading:SetText("Guild Level " .. level .. "!")
    if perk and perk ~= "" then
        line:SetText("New guild perk: |cffffd200" .. perk .. "|r")
    else
        line:SetText("Your guild has reached a new level.")
    end
    toastIcon:SetTexture(icon)
    elapsed = 0
    toast:SetAlpha(1)
    toast:Show()
end
