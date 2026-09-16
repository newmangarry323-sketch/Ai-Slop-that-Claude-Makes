--[[ ==========================================================================
     nyx_hud — client
     --------------------------------------------------------------------------
     Three loops feed the page: status, money and vehicle. They run at different
     rates because they cost different amounts — the vehicle loop touches a
     dozen natives per tick and sleeps completely while the player is on foot,
     which is most of the time.
     ========================================================================== ]]

local Util = Nyx.Util

local playerCount = 0
local lastVehicle = 0

-- ---------------------------------------------------------------------------
-- Native HUD suppression
-- ---------------------------------------------------------------------------

CreateThread(function()
    -- HideHudComponentThisFrame has to be called every frame, which is why this
    -- is a Wait(0) loop rather than a one-off call at startup.
    while true do
        Wait(0)
        for _, component in ipairs(NyxHud.HideNativeComponents) do
            HideHudComponentThisFrame(component)
        end
    end
end)

-- ---------------------------------------------------------------------------
-- Status: health, armour, voice
-- ---------------------------------------------------------------------------

--- pma-voice and its forks publish the current proximity on a state bag. Read
--- it defensively — a server running a different voice resource, or none, must
--- not produce an error every quarter second.
local function voiceRange()
    local ok, proximity = pcall(function() return LocalPlayer.state.proximity end)
    if not ok or type(proximity) ~= 'table' then return nil end
    return proximity.mode or proximity.name
end

CreateThread(function()
    while true do
        Wait(NyxHud.Tick.status)

        local ped = PlayerPedId()
        local player = PlayerId()

        -- GTA health starts at 100 rather than 0, so the usable range is
        -- 100..max and a "0%" player is a dead one.
        local max = GetEntityMaxHealth(ped)
        local health = math.max(GetEntityHealth(ped) - 100, 0)
        local healthMax = math.max(max - 100, 1)

        SendNUIMessage({
            action = 'status',
            health = Util.Clamp(math.floor(health / healthMax * 100), 0, 100),
            armour = Util.Clamp(GetPedArmour(ped), 0, 100),
            talking = NetworkIsPlayerTalking(player),
            voiceRange = voiceRange(),
            dead = IsPedDeadOrDying(ped, true)
        })
    end
end)

-- ---------------------------------------------------------------------------
-- Money, job and gang
-- ---------------------------------------------------------------------------

local function pushPlayerData()
    local data = exports['nyx_lib']:GetPlayerData()

    SendNUIMessage({
        action = 'player',
        name = data.name,
        cash = data.cash,
        bank = data.bank,
        black = data.black,
        job = data.jobLabel,
        grade = data.gradeLabel,
        gang = data.gangLabel,
        gangGrade = data.gangGradeLabel
    })
end

-- Event-driven, so the HUD updates the instant money moves rather than up to
-- half a second later.
AddEventHandler('nyx:playerDataChanged', pushPlayerData)

CreateThread(function()
    while true do
        Wait(NyxHud.Tick.money)
        pushPlayerData()
    end
end)

-- ---------------------------------------------------------------------------
-- Vehicle
-- ---------------------------------------------------------------------------

CreateThread(function()
    while true do
        local ped = PlayerPedId()
        local vehicle = GetVehiclePedIsIn(ped, false)

        if vehicle ~= 0 then
            local speed = GetEntitySpeed(vehicle)
            local units = NyxHud.SpeedUnits == 'kmh' and 3.6 or 2.236936

            SendNUIMessage({
                action = 'vehicle',
                inVehicle = true,
                speed = math.floor(speed * units),
                units = NyxHud.SpeedUnits,
                rpm = GetVehicleCurrentRpm(vehicle),
                gear = GetVehicleCurrentGear(vehicle),
                -- GetVehicleFuelLevel reads the game's own tank. Servers with a
                -- fuel resource usually write to the same native, but if yours
                -- does not, replace this line with its export.
                fuel = Util.Clamp(math.floor(GetVehicleFuelLevel(vehicle)), 0, 100),
                engine = Util.Clamp(math.floor(GetVehicleEngineHealth(vehicle) / 10), 0, 100),
                lights = IsVehicleSirenOn(vehicle)
            })

            lastVehicle = vehicle
            Wait(NyxHud.Tick.vehicle)
        else
            if lastVehicle ~= 0 then
                SendNUIMessage({ action = 'vehicle', inVehicle = false })
                lastVehicle = 0
            end
            -- On foot this loop costs one native call twice a second.
            Wait(500)
        end
    end
end)

-- ---------------------------------------------------------------------------
-- Toggles that reach outside the page
-- ---------------------------------------------------------------------------

local function applyExternalToggles(prefs)
    -- The radar is a real native and behaves exactly as you would expect.
    DisplayRadar(prefs.toggles.map ~= false)

    -- Chat is not. There is no standard way to hide another resource's chat,
    -- so Nyx announces the intent and leaves the wiring to you — see
    -- nyx/docs/configuration.md, "Making the chat toggle do something".
    TriggerEvent('nyx_hud:chatToggle', prefs.toggles.chat ~= false)
end

AddEventHandler('nyx_hud:prefsChanged', applyExternalToggles)

-- The radar gets turned back on by respawns and by other resources, so reassert
-- the player's choice periodically rather than only when it changes.
CreateThread(function()
    while true do
        Wait(2000)
        local prefs = NyxHud.GetPrefs()
        if prefs.toggles.map == false and IsRadarHidden() == false then
            DisplayRadar(false)
        end
    end
end)

-- ---------------------------------------------------------------------------
-- Announcements
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_hud:announce', function(payload)
    if NyxHud.GetPrefs().toggles.announcements == false then return end

    SendNUIMessage({
        action = 'announce',
        title = Util.SafeString(payload.title, 48),
        text = Util.SafeString(payload.text, 240),
        duration = Util.Clamp(payload.duration or 7000, 1500, 30000)
    })
end)

RegisterNetEvent('nyx_hud:playerCount', function(count)
    playerCount = tonumber(count) or 0
    SendNUIMessage({ action = 'playerCount', count = playerCount })
end)

-- ---------------------------------------------------------------------------
-- Boot
-- ---------------------------------------------------------------------------

CreateThread(function()
    -- Wait for the page to exist before pushing anything into it.
    Wait(500)

    SendNUIMessage({
        action = 'boot',
        watermark = NyxHud.Watermark,
        accent = exports['nyx_lib']:GetAccent().hex
    })

    NyxHud.Publish(false)
    applyExternalToggles(NyxHud.GetPrefs())
    pushPlayerData()

    TriggerServerEvent('nyx_hud:request')
end)

AddEventHandler('nyx:accentChanged', function(_, hex)
    SendNUIMessage({ action = 'nyx:theme', accent = hex })
end)

--- Reset to the config defaults, for when a player has tuned themselves into a
--- corner and cannot find their way out.
RegisterCommand('hudreset', function()
    NyxHud.ResetPrefs()
    Nyx.Notify({ title = 'HUD', description = 'Reset to defaults.', type = 'success' })
end, false)

AddEventHandler('onResourceStop', function(resource)
    if resource ~= GetCurrentResourceName() then return end
    -- Put the game's own HUD back rather than leaving the player with nothing.
    DisplayRadar(true)
end)
