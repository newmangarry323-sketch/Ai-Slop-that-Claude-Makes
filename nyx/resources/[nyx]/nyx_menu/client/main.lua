--[[ ==========================================================================
     nyx_menu — NUI controller
     --------------------------------------------------------------------------
     Owns the menu's open/close lifecycle, builds the payload the page renders
     from, and turns every NUI callback into either a local action or a server
     request. No coordinates, weapon hashes or prices are ever sent to the page:
     it deals in ids, and the Lua side resolves them against config.lua.
     ========================================================================== ]]

local Util = Nyx.Util

local open = false
local occupancy = {}       -- spotId -> count
local allowedWeapons = {}  -- id -> true, pushed by the server
local lastTeleport = 0

-- ---------------------------------------------------------------------------
-- Payload
-- ---------------------------------------------------------------------------

local function hudAvailable()
    local state = GetResourceState('nyx_hud')
    return state == 'started' or state == 'starting'
end

--- Read nyx_hud's preferences, or nil when it is not installed. The Misc tab
--- renders a friendly "not installed" panel in that case rather than breaking.
local function hudPrefs()
    if not hudAvailable() then return nil end
    local ok, prefs = pcall(function() return exports['nyx_hud']:GetPrefs() end)
    if not ok then return nil end
    return prefs
end

--- Locations, flattened for the page: ids, labels, art and live counts only.
local function buildLocations()
    local out = {}

    for _, section in ipairs(NyxMenu.Locations) do
        local groups = {}

        for _, group in ipairs(section.groups or {}) do
            local spots = {}
            for _, spot in ipairs(group.spots or {}) do
                spots[#spots + 1] = {
                    id = spot.id,
                    label = spot.label,
                    image = spot.image,
                    count = occupancy[spot.id] or 0
                }
            end
            groups[#groups + 1] = { label = group.label, spots = spots }
        end

        out[#out + 1] = { id = section.id, label = section.label, groups = groups }
    end

    return out
end

--- Weapons, minus anything this player has no ACE access to. The server is the
--- authority; this only stops the page offering something that would be
--- refused, which is a nicer failure than a silent no-op.
local function buildWeapons()
    local out = {}

    for _, section in ipairs(NyxMenu.Weapons) do
        local groups = {}

        for _, group in ipairs(section.groups or {}) do
            local items = {}
            for _, item in ipairs(group.items or {}) do
                if allowedWeapons[item.id] then
                    items[#items + 1] = {
                        id = item.id,
                        label = item.label,
                        image = item.image,
                        kit = item.kit ~= nil and #item.kit or nil
                    }
                end
            end
            if #items > 0 then
                groups[#groups + 1] = { label = group.label, items = items }
            end
        end

        if #groups > 0 then
            out[#out + 1] = { id = section.id, label = section.label, groups = groups }
        end
    end

    return out
end

local function buildPayload()
    local player = exports['nyx_lib']:GetPlayerData()
    local accent = exports['nyx_lib']:GetAccent()

    return {
        action = 'open',
        brand = Nyx.Config.brand,
        accent = accent.hex,
        accents = exports['nyx_lib']:GetAccents(),
        player = {
            name = player.name,
            job = player.jobLabel,
            grade = player.gradeLabel
        },
        stats = NyxMenu.Stats.enabled and NyxMenu.GetStats() or nil,
        locations = buildLocations(),
        weapons = NyxMenu.WeaponsTab.enabled and buildWeapons() or {},
        hud = hudPrefs()
    }
end

-- ---------------------------------------------------------------------------
-- Open / close
-- ---------------------------------------------------------------------------

local function closeMenu()
    if not open then return end
    open = false
    SendNUIMessage({ action = 'close' })
    exports['nyx_lib']:ReleaseFocus()
    if NyxMenu.Sounds then Nyx.Sound('back') end
end

local function openMenu()
    if open then return closeMenu() end
    if IsPauseMenuActive() then return end

    -- Another Nyx surface holding focus gets evicted by RequestFocus, but a
    -- dead player opening a teleport menu is a different problem.
    if NyxMenu.Teleport.blockWhenDead and IsPedDeadOrDying(PlayerPedId(), true) then
        Nyx.Notify({ title = 'Unavailable', description = 'Not while you are down.', type = 'warn' })
        return
    end

    open = true
    exports['nyx_lib']:RequestFocus(true)
    SendNUIMessage(buildPayload())
    if NyxMenu.Sounds then Nyx.Sound('open') end
end

RegisterCommand(NyxMenu.Command, openMenu, false)

if NyxMenu.Key and NyxMenu.Key ~= '' then
    -- Registers a *default* only. A player who has already bound this key to
    -- something else keeps their binding, and anyone can rebind in
    -- Settings > Key Bindings > FiveM.
    RegisterKeyMapping(NyxMenu.Command, 'Open the Nyx menu', 'keyboard', NyxMenu.Key)
end

-- nyx_lib evicting us in favour of another menu.
AddEventHandler('nyx:forceClose', function(resource)
    if resource == GetCurrentResourceName() then
        open = false
        SendNUIMessage({ action = 'close' })
    end
end)

AddEventHandler('nyx:accentChanged', function(_, hex)
    SendNUIMessage({ action = 'nyx:theme', accent = hex })
end)

AddEventHandler('nyx_menu:statsChanged', function()
    if open then SendNUIMessage({ action = 'stats', stats = NyxMenu.GetStats() }) end
end)

-- ---------------------------------------------------------------------------
-- NUI callbacks
-- ---------------------------------------------------------------------------

RegisterNUICallback('close', function(_, cb)
    closeMenu()
    cb({ ok = true })
end)

RegisterNUICallback('sound', function(data, cb)
    if NyxMenu.Sounds then Nyx.Sound(data.key or 'hover') end
    cb({ ok = true })
end)

RegisterNUICallback('teleport', function(data, cb)
    local spot = NyxMenu.FindSpot(data.id)
    if not spot then
        cb({ ok = false, reason = 'unknown' })
        return
    end

    local now = GetGameTimer()
    if now - lastTeleport < (NyxMenu.Teleport.cooldownMs or 0) then
        Nyx.Notify({ title = 'Slow down', description = 'Teleport is still cooling down.', type = 'warn' })
        cb({ ok = false, reason = 'cooldown' })
        return
    end

    if NyxMenu.Teleport.blockWhenDead and IsPedDeadOrDying(PlayerPedId(), true) then
        Nyx.Notify({ title = 'Unavailable', description = 'Not while you are down.', type = 'warn' })
        cb({ ok = false, reason = 'dead' })
        return
    end

    lastTeleport = now
    closeMenu()

    -- The server gets the last word on restricted spots. It answers with
    -- nyx_menu:doTeleport, which is where the jump actually happens.
    TriggerServerEvent('nyx_menu:requestTeleport', spot.id)
    cb({ ok = true })
end)

RegisterNUICallback('giveWeapon', function(data, cb)
    if not NyxMenu.WeaponsTab.enabled then
        cb({ ok = false })
        return
    end
    TriggerServerEvent('nyx_menu:requestWeapon', data.id)
    cb({ ok = true })
end)

RegisterNUICallback('setAccent', function(data, cb)
    cb({ ok = exports['nyx_lib']:SetAccent(data.id) })
end)

RegisterNUICallback('hud', function(data, cb)
    if not hudAvailable() then
        cb({ ok = false })
        return
    end

    local ok = pcall(function()
        if data.field == 'style' or data.field == 'moneyStyle' then
            exports['nyx_hud']:SetStyle(data.field, data.value)
        elseif data.field == 'toggle' then
            exports['nyx_hud']:SetToggle(data.key, data.value)
        elseif data.field == 'crosshair' then
            exports['nyx_hud']:SetCrosshair(data.value)
        end
    end)

    cb({ ok = ok, hud = ok and hudPrefs() or nil })
end)

-- ---------------------------------------------------------------------------
-- Server pushes
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_menu:doTeleport', function(spotId)
    local spot = NyxMenu.FindSpot(spotId)
    if not spot then return end

    NyxMenu.DoTeleport(spot.coords, {
        snapToGround = spot.snapToGround,
        heal = spot.heal,
        clearWanted = spot.clearWanted
    })

    Nyx.Notify({ title = 'Teleported', description = spot.label, type = 'success' })
end)

RegisterNetEvent('nyx_menu:doGiveWeapon', function(payload)
    local ped = PlayerPedId()

    if payload.clearFirst then
        RemoveAllPedWeapons(ped, true)
    end

    for _, entry in ipairs(payload.weapons or {}) do
        local hash = GetHashKey(entry.weapon)
        if IsWeaponValid(hash) then
            GiveWeaponToPed(ped, hash, entry.ammo or 0, false, NyxMenu.WeaponsTab.equipOnGive)
        end
    end

    Nyx.Notify({ title = 'Loadout', description = payload.label, type = 'success' })
    if NyxMenu.Sounds then Nyx.Sound('pickup') end
end)

RegisterNetEvent('nyx_menu:permissions', function(ids)
    allowedWeapons = {}
    for _, id in ipairs(ids or {}) do allowedWeapons[id] = true end
    if open then SendNUIMessage({ action = 'weapons', weapons = buildWeapons() }) end
end)

RegisterNetEvent('nyx_menu:occupancy', function(counts)
    occupancy = counts or {}
    if open then SendNUIMessage({ action = 'occupancy', occupancy = occupancy }) end
end)

-- ---------------------------------------------------------------------------
-- Occupancy reporting
-- ---------------------------------------------------------------------------
-- The client works out which configured spot it is standing at and only tells
-- the server when that answer changes. Standing still costs nothing.

if NyxMenu.Occupancy.enabled then
    CreateThread(function()
        local radius = NyxMenu.Occupancy.radius or 60.0
        local interval = NyxMenu.Occupancy.checkInterval or 3000
        local reported = nil

        while true do
            Wait(interval)

            local pos = GetEntityCoords(PlayerPedId())
            local nearest, nearestDist = nil, radius

            for _, section in ipairs(NyxMenu.Locations) do
                for _, group in ipairs(section.groups or {}) do
                    for _, spot in ipairs(group.spots or {}) do
                        local dist = #(pos - vector3(spot.coords.x, spot.coords.y, spot.coords.z))
                        if dist < nearestDist then
                            nearest, nearestDist = spot.id, dist
                        end
                    end
                end
            end

            if nearest ~= reported then
                reported = nearest
                TriggerServerEvent('nyx_menu:atSpot', nearest)
            end
        end
    end)
end

AddEventHandler('onResourceStop', function(resource)
    if resource ~= GetCurrentResourceName() then return end
    -- Leaving focus grabbed would strand the player with a cursor and no keys.
    SetNuiFocus(false, false)
end)
