--[[ ==========================================================================
     Nyx — server API
     --------------------------------------------------------------------------
         exports['nyx_lib']:GetIdentifier(src)
         exports['nyx_lib']:GetName(src)
         exports['nyx_lib']:GetFramework()
         exports['nyx_lib']:GetMoney(src, account)
         exports['nyx_lib']:AddMoney(src, account, amount)
         exports['nyx_lib']:RemoveMoney(src, account, amount)   -- false = broke
         exports['nyx_lib']:GetJob(src)
         exports['nyx_lib']:HasPermission(src, ace)
         exports['nyx_lib']:RateLimit(src, key, ms)             -- false = too soon
         exports['nyx_lib']:Notify(src, opts)
     ========================================================================== ]]

local Bridge = Nyx.Bridge
local Store = Nyx.Store
local Util = Nyx.Util

exports('GetIdentifier', function(src) return Bridge.GetIdentifier(src) end)
exports('GetName', function(src) return Bridge.GetName(src) end)
exports('GetFramework', function() return Bridge.GetFramework() end)
exports('GetMoney', function(src, account) return Bridge.GetMoney(src, account) end)
exports('AddMoney', function(src, account, amount) return Bridge.AddMoney(src, account, amount) end)
exports('RemoveMoney', function(src, account, amount) return Bridge.RemoveMoney(src, account, amount) end)
exports('GetJob', function(src) return Bridge.GetJob(src) end)

exports('Notify', function(src, opts) TriggerClientEvent('nyx:notify', src, opts or {}) end)

--- ACE check, e.g. HasPermission(src, 'nyx.admin'). Grant in server.cfg with
---     add_ace group.admin nyx.admin allow
exports('HasPermission', function(src, ace)
    if type(ace) ~= 'string' or ace == '' then return false end
    return IsPlayerAceAllowed(src, ace)
end)

-- ---------------------------------------------------------------------------
-- Rate limiting
-- ---------------------------------------------------------------------------
-- Every net event a client can fire is an endpoint a modified client can spam.
-- This will not stop a determined attacker but it does stop the common case:
-- a loop that fires a purchase or a teleport a thousand times a second.

local lastCall = {}   -- src -> key -> ms timestamp

local function rateLimit(src, key, ms)
    local now = GetGameTimer()
    local perPlayer = lastCall[src]

    if not perPlayer then
        perPlayer = {}
        lastCall[src] = perPlayer
    end

    local previous = perPlayer[key]
    if previous and (now - previous) < (ms or 500) then return false end

    perPlayer[key] = now
    return true
end

Nyx.RateLimit = rateLimit
exports('RateLimit', function(src, key, ms) return rateLimit(src, tostring(key), ms) end)

-- ---------------------------------------------------------------------------
-- Accent persistence
-- ---------------------------------------------------------------------------

local function unlockedAccents(src)
    local out = {}
    for _, preset in ipairs(Nyx.Accents) do
        if preset.locked and IsPlayerAceAllowed(src, 'nyx.accent.' .. preset.id) then
            out[#out + 1] = preset.id
        end
    end
    return out
end

local function sendAccentState(src)
    local id = Bridge.GetIdentifier(src)
    local stored = id and Store.Get('accents', id) or nil
    TriggerClientEvent('nyx:lib:accentState', src, stored or Nyx.DefaultAccent, unlockedAccents(src))
end

RegisterNetEvent('nyx:lib:setAccent', function(accentId)
    local src = source
    if not rateLimit(src, 'setAccent', 250) then return end

    -- Re-check the id and the lock server-side. The client already checked,
    -- but a client check is a convenience, never a control.
    local preset
    for _, p in ipairs(Nyx.Accents) do
        if p.id == accentId then preset = p break end
    end
    if not preset then return end

    if preset.locked and not Nyx.Config.unlockAllAccents and not IsPlayerAceAllowed(src, 'nyx.accent.' .. preset.id) then
        return
    end

    local id = Bridge.GetIdentifier(src)
    if id then Store.Set('accents', id, preset.id) end
end)

RegisterNetEvent('nyx:lib:clientReady', function()
    local src = source
    sendAccentState(src)

    if Bridge.GetFramework() == 'standalone' then
        Bridge.PushWallet(src)
    end
end)

-- ---------------------------------------------------------------------------
-- Lifecycle
-- ---------------------------------------------------------------------------

AddEventHandler('playerDropped', function()
    local src = source
    lastCall[src] = nil
    -- Flush now rather than waiting for the autosave: a player who leaves and
    -- the server crashes a minute later should not lose the session.
    Store.FlushAll()
end)

--- Console-side inspection, e.g. `nyx` in the server console.
RegisterCommand('nyx', function(src)
    if src ~= 0 and not IsPlayerAceAllowed(src, 'nyx.admin') then return end

    local lines = {
        ('framework : %s'):format(Bridge.GetFramework()),
        ('players   : %d'):format(#GetPlayers()),
        ('accents   : %d preset(s)'):format(#Nyx.Accents)
    }

    for _, line in ipairs(lines) do
        if src == 0 then print('^5[nyx]^7 ' .. line) else TriggerClientEvent('chat:addMessage', src, { args = { 'nyx', line } }) end
    end
end, false)
