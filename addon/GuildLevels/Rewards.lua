-- GuildLevels: the guild vendors' tooltips. A guild vendor lists every reward; one you can't buy
-- yet shows sold out. Its tooltip says what it needs: the guild level and your standing with the
-- guild, in red while you don't have it.

local GL = GuildLevels
local RED, GREEN = { 1, 0.13, 0.13 }, { 0.25, 0.9, 0.25 }

local function addRequirements(tooltip, reward)
    local guildLow, repLow = GL.RewardLocks(reward)
    local c = guildLow and RED or GREEN
    tooltip:AddLine("Requires guild level " .. reward.level, c[1], c[2], c[3])
    c = repLow and RED or GREEN
    tooltip:AddLine("Requires " .. GL.RankName(reward.rank) .. " with your guild", c[1], c[2], c[3])
    if GL.level == 0 then
        tooltip:AddLine("Guild rewards are for guild members.", RED[1], RED[2], RED[3])
    end
    tooltip:Show()
end
GL.AddRewardRequirements = addRequirements

hooksecurefunc(GameTooltip, "SetMerchantItem", function(tooltip, index)
    if not GL.AtGuildVendor() then
        return
    end
    local link = GetMerchantItemLink(index)
    local item = link and tonumber(link:match("item:(%d+)"))
    local reward = item and GL.RewardFor(item)
    if reward then
        addRequirements(tooltip, reward)
    end
end)

-- When a guild vendor's window opens, ask for the state so the tooltips' locks are current.
local frame = CreateFrame("Frame")
frame:RegisterEvent("MERCHANT_SHOW")
frame:SetScript("OnEvent", function()
    if GL.connected and GL.AtGuildVendor() then
        GL.Send("STATE")
    end
end)
