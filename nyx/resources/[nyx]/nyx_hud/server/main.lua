--[[ ==========================================================================
     nyx_hud — server
     --------------------------------------------------------------------------
     Stores each player's HUD preferences against their identifier, and owns the
     announcement command and the player counter.
     ========================================================================== ]]

local Util = Nyx.Util

local function sendPrefs(src)
    local id = exports['nyx_lib']:GetIdentifier(src)
    local stored = id and exports['nyx_lib']:DbGet('prefs', id) or nil
    TriggerClientEvent('nyx_hud:load', src, stored or NyxHud.Defaults)
end

RegisterNetEvent('nyx_hud:request', function()
    sendPrefs(source)
end)

RegisterNetEvent('nyx_hud:save', function(prefs)
    local src = source

    -- Preferences change on every slider nudge; the client already debounces
    -- but a modified one need not.
    if not exports['nyx_lib']:RateLimit(src, 'hudSave', 300) then return end
    if type(prefs) ~= 'table' then return end

    local id = exports['nyx_lib']:GetIdentifier(src)
    if not id then return end

    -- Stored as received. The client sanitises on the way out and again on the
    -- way back in at load, so a hand-edited JSON file cannot inject anything
    -- into the page either.
    exports['nyx_lib']:DbSet('prefs', id, prefs)
end)

-- ---------------------------------------------------------------------------
-- Player counter
-- ---------------------------------------------------------------------------

local function broadcastCount()
    TriggerClientEvent('nyx_hud:playerCount', -1, #GetPlayers())
end

AddEventHandler('playerJoining', function()
    SetTimeout(1000, broadcastCount)
end)

AddEventHandler('playerDropped', function()
    SetTimeout(1000, broadcastCount)
end)

RegisterNetEvent('nyx:lib:clientReady', function()
    local src = source
    TriggerClientEvent('nyx_hud:playerCount', src, #GetPlayers())
end)

-- ---------------------------------------------------------------------------
-- Announcements
-- ---------------------------------------------------------------------------

--- announce <message...>
--- Console-usable, or in-game with: add_ace group.admin nyx.announce allow
RegisterCommand('announce', function(src, args)
    if src ~= 0 and not IsPlayerAceAllowed(src, 'nyx.announce') then
        exports['nyx_lib']:Notify(src, { title = 'Denied', description = 'You cannot announce.', type = 'error' })
        return
    end

    local text = Util.SafeString(table.concat(args, ' '), 240)
    if text == '' then
        if src == 0 then print('^3[nyx]^7 usage: announce <message>') end
        return
    end

    TriggerClientEvent('nyx_hud:announce', -1, {
        title = 'Announcement',
        text = text,
        duration = 9000
    })
end, false)

exports('Announce', function(target, title, text, duration)
    TriggerClientEvent('nyx_hud:announce', target or -1, {
        title = Util.SafeString(title, 48),
        text = Util.SafeString(text, 240),
        duration = duration
    })
end)
