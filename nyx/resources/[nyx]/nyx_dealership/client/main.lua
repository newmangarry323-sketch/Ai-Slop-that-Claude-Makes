--[[ ==========================================================================
     nyx_dealership — client
     ========================================================================== ]]

local Util = Nyx.Util

local open = false
local activeLocation = nil
local owned = {}          -- vehicle ids this player already has
local allowed = {}        -- vehicle ids this player may buy
local selected = nil
local testDrive = nil     -- { entity, endsAt, returnTo }

-- ---------------------------------------------------------------------------
-- Catalogue
-- ---------------------------------------------------------------------------

local function findVehicle(id)
    for _, vehicle in ipairs(NyxDealer.Vehicles) do
        if vehicle.id == id then return vehicle end
    end
    return nil
end

--- Catalogue for the page: categories with live counts, and the vehicles this
--- player is allowed to see.
local function buildCatalogue()
    local byCategory = {}

    for _, vehicle in ipairs(NyxDealer.Vehicles) do
        if allowed[vehicle.id] then
            local list = byCategory[vehicle.category]
            if not list then
                list = {}
                byCategory[vehicle.category] = list
            end

            list[#list + 1] = {
                id = vehicle.id,
                label = vehicle.label,
                price = vehicle.price,
                drift = vehicle.drift or false,
                owned = owned[vehicle.id] or false
            }
        end
    end

    local categories = {}
    for _, category in ipairs(NyxDealer.Categories) do
        categories[#categories + 1] = {
            id = category.id,
            label = category.label,
            count = byCategory[category.id] and #byCategory[category.id] or 0,
            vehicles = byCategory[category.id] or {}
        }
    end

    return categories
end

-- ---------------------------------------------------------------------------
-- Open / close
-- ---------------------------------------------------------------------------

local function closeShowroom()
    if not open then return end
    open = false
    selected = nil

    SendNUIMessage({ action = 'close' })
    exports['nyx_lib']:ReleaseFocus()

    NyxDealer.DestroyPreview()
    NyxDealer.StopCamera()

    activeLocation = nil
    Nyx.Sound('back')
end

local function openShowroom(location)
    if open then return end
    if testDrive then
        Nyx.Notify({ title = 'Test drive', description = 'Finish your test drive first.', type = 'warn' })
        return
    end

    open = true
    activeLocation = location

    exports['nyx_lib']:RequestFocus(true)
    NyxDealer.StartCamera(location)
    NyxDealer.SpinPreview()

    SendNUIMessage({
        action = 'open',
        title = location.label,
        accent = exports['nyx_lib']:GetAccent().hex,
        categories = buildCatalogue(),
        perPage = NyxDealer.PerPage,
        respray = NyxDealer.Respray.enabled and NyxDealer.Respray.colours or nil,
        testDrive = NyxDealer.TestDrive.enabled,
        currency = NyxDealer.PayFrom
    })

    Nyx.Sound('open')
end

AddEventHandler('nyx:forceClose', function(resource)
    if resource == GetCurrentResourceName() then closeShowroom() end
end)

AddEventHandler('nyx:accentChanged', function(_, hex)
    SendNUIMessage({ action = 'nyx:theme', accent = hex })
end)

-- ---------------------------------------------------------------------------
-- Showroom proximity
-- ---------------------------------------------------------------------------

CreateThread(function()
    -- Blips are static; place them once.
    for _, location in ipairs(NyxDealer.Locations) do
        if location.blip then
            local blip = AddBlipForCoord(location.ped.x, location.ped.y, location.ped.z)
            SetBlipSprite(blip, location.blip.sprite or 225)
            SetBlipColour(blip, location.blip.colour or 6)
            SetBlipScale(blip, location.blip.scale or 0.8)
            SetBlipAsShortRange(blip, true)
            BeginTextCommandSetBlipName('STRING')
            AddTextComponentSubstringPlayerName(location.label)
            EndTextCommandSetBlipName(blip)
        end
    end

    while true do
        local wait = 1000
        local pos = GetEntityCoords(PlayerPedId())

        if not open and not testDrive then
            for _, location in ipairs(NyxDealer.Locations) do
                local dist = #(pos - vector3(location.ped.x, location.ped.y, location.ped.z))

                if dist < 20.0 then
                    -- Close enough that the prompt needs to be responsive.
                    wait = 0

                    if dist < NyxDealer.InteractDistance then
                        BeginTextCommandDisplayHelp('STRING')
                        AddTextComponentSubstringPlayerName('Press ~INPUT_CONTEXT~ to browse ' .. location.label)
                        EndTextCommandDisplayHelp(0, false, true, -1)

                        if IsControlJustReleased(0, 38) then  -- E
                            openShowroom(location)
                        end
                    end
                    break
                end
            end
        end

        Wait(wait)
    end
end)

-- ---------------------------------------------------------------------------
-- Test drive
-- ---------------------------------------------------------------------------

local function endTestDrive(silent)
    if not testDrive then return end

    local entity = testDrive.entity
    local returnTo = testDrive.returnTo
    testDrive = nil

    if entity and DoesEntityExist(entity) then
        DeleteEntity(entity)
    end

    if returnTo and NyxDealer.TestDrive.returnOnEnd then
        local ped = PlayerPedId()
        DoScreenFadeOut(300)
        local deadline = GetGameTimer() + 900
        while not IsScreenFadedOut() and GetGameTimer() < deadline do Wait(0) end

        SetEntityCoords(ped, returnTo.x, returnTo.y, returnTo.z, false, false, false, false)
        SetEntityHeading(ped, returnTo.w or 0.0)
        Wait(200)
        DoScreenFadeIn(300)
    end

    SendNUIMessage({ action = 'testDriveEnd' })

    if not silent then
        Nyx.Notify({ title = 'Test drive', description = 'Time is up.', type = 'info' })
    end
end

local function startTestDrive(vehicle)
    if not NyxDealer.TestDrive.enabled or not activeLocation then return end

    local ok, hash = NyxDealer.RequestModel(vehicle.model)
    if not ok then
        Nyx.Notify({ title = 'Unavailable', description = 'That vehicle could not be loaded.', type = 'error' })
        return
    end

    local location = activeLocation
    local returnTo = location.ped

    closeShowroom()

    local spot = location.spawn
    local entity = CreateVehicle(hash, spot.x, spot.y, spot.z, spot.w, true, false)
    SetModelAsNoLongerNeeded(hash)

    SetVehicleOnGroundProperly(entity)
    SetEntityAsMissionEntity(entity, true, true)
    SetPedIntoVehicle(PlayerPedId(), entity, -1)
    SetVehicleEngineOn(entity, true, true, false)

    testDrive = {
        entity = entity,
        endsAt = GetGameTimer() + NyxDealer.TestDrive.seconds * 1000,
        returnTo = returnTo
    }

    Nyx.Notify({
        title = 'Test drive',
        description = ('%d seconds.'):format(NyxDealer.TestDrive.seconds),
        type = 'info'
    })

    CreateThread(function()
        while testDrive do
            local remaining = math.max(math.ceil((testDrive.endsAt - GetGameTimer()) / 1000), 0)

            SendNUIMessage({ action = 'testDrive', seconds = remaining })

            if remaining <= 0 then
                endTestDrive(false)
                break
            end

            -- Leaving the car ends the drive; otherwise a player can park it
            -- and walk away with a free vehicle sitting in the world.
            if GetVehiclePedIsIn(PlayerPedId(), false) ~= testDrive.entity then
                Wait(3000)
                if testDrive and GetVehiclePedIsIn(PlayerPedId(), false) ~= testDrive.entity then
                    endTestDrive(false)
                    break
                end
            end

            Wait(250)
        end
    end)
end

-- ---------------------------------------------------------------------------
-- NUI callbacks
-- ---------------------------------------------------------------------------

RegisterNUICallback('close', function(_, cb)
    closeShowroom()
    cb({ ok = true })
end)

RegisterNUICallback('sound', function(data, cb)
    Nyx.Sound(data.key or 'hover')
    cb({ ok = true })
end)

RegisterNUICallback('select', function(data, cb)
    local vehicle = findVehicle(data.id)
    if not vehicle or not allowed[vehicle.id] then
        cb({ ok = false })
        return
    end

    selected = vehicle
    local stats = NyxDealer.SetPreview(vehicle.model)

    cb({
        ok = stats ~= nil,
        stats = stats or { power = 0, acceleration = 0, braking = 0, handling = 0 }
    })
end)

RegisterNUICallback('buy', function(data, cb)
    local vehicle = findVehicle(data.id)
    if not vehicle then
        cb({ ok = false })
        return
    end

    -- The client never decides whether a purchase succeeds. It asks.
    TriggerServerEvent('nyx_dealership:buy', vehicle.id)
    cb({ ok = true })
end)

RegisterNUICallback('testDrive', function(data, cb)
    local vehicle = findVehicle(data.id)
    if not vehicle or not allowed[vehicle.id] then
        cb({ ok = false })
        return
    end

    startTestDrive(vehicle)
    cb({ ok = true })
end)

RegisterNUICallback('respray', function(data, cb)
    if not NyxDealer.Respray.enabled then
        cb({ ok = false })
        return
    end

    local index = tonumber(data.index)
    local colour = index and NyxDealer.Respray.colours[index] or nil
    if not colour then
        cb({ ok = false })
        return
    end

    cb({ ok = NyxDealer.ApplyRespray(colour.primary, colour.secondary) })
end)

-- ---------------------------------------------------------------------------
-- Server pushes
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_dealership:catalogue', function(payload)
    allowed = {}
    for _, id in ipairs(payload.allowed or {}) do allowed[id] = true end

    owned = {}
    for _, id in ipairs(payload.owned or {}) do owned[id] = true end

    if open then
        SendNUIMessage({ action = 'catalogue', categories = buildCatalogue() })
    end
end)

RegisterNetEvent('nyx_dealership:bought', function(payload)
    owned[payload.id] = true

    local vehicle = findVehicle(payload.id)
    if not vehicle then return end

    closeShowroom()

    local ok, hash = NyxDealer.RequestModel(vehicle.model)
    if not ok then return end

    local spot = payload.spawn or (NyxDealer.Locations[1] and NyxDealer.Locations[1].spawn)
    if not spot then
        SetModelAsNoLongerNeeded(hash)
        return
    end

    local entity = CreateVehicle(hash, spot.x, spot.y, spot.z, spot.w, true, false)
    SetModelAsNoLongerNeeded(hash)

    SetVehicleOnGroundProperly(entity)
    SetEntityAsMissionEntity(entity, true, true)
    SetVehicleNumberPlateText(entity, payload.plate or 'NYX')
    SetPedIntoVehicle(PlayerPedId(), entity, -1)
    SetVehicleEngineOn(entity, true, true, false)

    -- Hand the keys to whatever key system the server runs, if any.
    TriggerEvent('nyx_dealership:keysGranted', NetworkGetNetworkIdFromEntity(entity), payload.plate)

    Nyx.Notify({ title = 'Purchased', description = vehicle.label, type = 'success' })
    Nyx.Sound('buy')
end)

RegisterNetEvent('nyx_dealership:refused', function(reason)
    SendNUIMessage({ action = 'refused', reason = reason })
    Nyx.Sound('error')
end)

-- ---------------------------------------------------------------------------
-- Cleanup
-- ---------------------------------------------------------------------------

CreateThread(function()
    while true do
        Wait(1000)
        -- Dying mid-showroom would otherwise leave the camera locked to a car
        -- while the player respawns somewhere else entirely.
        if open and IsPedDeadOrDying(PlayerPedId(), true) then
            closeShowroom()
        end
    end
end)

AddEventHandler('onResourceStop', function(resource)
    if resource ~= GetCurrentResourceName() then return end
    endTestDrive(true)
    SetNuiFocus(false, false)
end)
