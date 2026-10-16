-- GuildLevels toast: the guild reached a new level. A banner at the top of the screen with the
-- level and the perk it brought, the level-up sound, and a fade after a few seconds.

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

local icon = toast:CreateTexture(nil, "ARTWORK")
icon:SetSize(40, 40)
icon:SetPoint("LEFT", 18, 0)

local heading = toast:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
heading:SetPoint("TOPLEFT", icon, "TOPRIGHT", 12, -2)

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

function GL.ShowLevelUp(level, perk)
    heading:SetText("Guild Level " .. level .. "!")
    if perk and perk ~= "" then
        line:SetText("New guild perk: |cffffd200" .. perk .. "|r")
    else
        line:SetText("Your guild has reached a new level.")
    end
    icon:SetTexture("Interface\\Icons\\" .. (GL.ICONS[level] or "INV_Misc_TabardPVP_02"))
    elapsed = 0
    toast:SetAlpha(1)
    toast:Show()
    PlaySoundFile("Sound\\Interface\\LevelUp.wav")
end
