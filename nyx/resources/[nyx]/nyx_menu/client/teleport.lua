--[[ ==========================================================================
     nyx_menu — teleporting
     --------------------------------------------------------------------------
     Naked SetEntityCoords over a long distance drops the player through the
     map, because the collision mesh for the destination has not streamed in
     yet. The sequence below is the one that actually works: fade out, freeze,
     move, spin until collision reports ready (with a timeout so a bad
     destination cannot hang the client forever), unfreeze, fade in.
     ========================================================================== ]]

local Util = Nyx.Util

local busy = false

--- Find a sane Z if the configured one is under the map. GetGroundZFor_3dCoord
--- only answers once the tile is streamed, hence the retries.
--- @return number z
local function groundZ(x, y, z)
    for _, probe in ipairs({ z + 1.0, z + 25.0, z + 100.0, 400.0, 800.0 }) do
        local found, gz = GetGroundZFor_3dCoord(x, y, probe, false)
        if found and gz > -100.0 then return gz + 0.5 end
        Wait(0)
    end
    return z
end

--- @param coords vector4 x, y, z, heading
--- @param opts table|nil { keepVehicle, heal, clearWanted, snapToGround }
--- @return boolean
function NyxMenu.DoTeleport(coords, opts)
    if busy then return false end

    opts = opts or {}
    busy = true

    local cfg = NyxMenu.Teleport
    local fade = math.max(cfg.fadeMs or 350, 0)
    local ped = PlayerPedId()

    -- Take the car only when the player is the one driving it. A passenger
    -- teleporting the vehicle would yank the driver across the map with them.
    local entity = ped
    local vehicle = GetVehiclePedIsIn(ped, false)
    if opts.keepVehicle ~= false and cfg.keepVehicle and vehicle ~= 0 and GetPedInVehicleSeat(vehicle, -1) == ped then
        entity = vehicle
    end

    if fade > 0 then
        DoScreenFadeOut(fade)
        local deadline = GetGameTimer() + fade + 500
        while not IsScreenFadedOut() and GetGameTimer() < deadline do Wait(0) end
    end

    FreezeEntityPosition(entity, true)
    SetEntityCollision(entity, false, false)
    SetEntityCoords(entity, coords.x, coords.y, coords.z, false, false, false, false)
    SetEntityHeading(entity, coords.w or 0.0)

    -- Wait for the world to arrive. 15s is generous; if it has not streamed by
    -- then the player is somewhere the map does not exist and we land anyway
    -- rather than locking the client in a frozen black screen.
    local deadline = GetGameTimer() + 15000
    while GetGameTimer() < deadline do
        RequestCollisionAtCoord(coords.x, coords.y, coords.z)
        if HasCollisionLoadedAroundEntity(entity) then break end
        Wait(0)
    end

    if opts.snapToGround then
        local z = groundZ(coords.x, coords.y, coords.z)
        SetEntityCoords(entity, coords.x, coords.y, z, false, false, false, false)
    end

    SetEntityCollision(entity, true, true)
    FreezeEntityPosition(entity, false)

    -- Landing inside a vehicle with no velocity is fine; landing on foot
    -- mid-fall is not, so kill any inherited momentum.
    SetEntityVelocity(entity, 0.0, 0.0, 0.0)

    if entity ~= ped then
        SetVehicleOnGroundProperly(entity)
    end

    if opts.heal or cfg.healOnArrive then
        SetEntityHealth(ped, GetEntityMaxHealth(ped))
        SetPedArmour(ped, 100)
    end

    if opts.clearWanted or cfg.clearWantedOnArrive then
        ClearPlayerWantedLevel(PlayerId())
    end

    Wait(100)

    if fade > 0 then
        DoScreenFadeIn(fade)
    end

    busy = false
    return true
end

function NyxMenu.IsTeleporting()
    return busy
end

-- ---------------------------------------------------------------------------
-- Config lookup
-- ---------------------------------------------------------------------------

--- Resolve a spot id to its config entry. Ids are the only thing that ever
--- crosses the wire, so both sides need this.
--- @return table|nil spot, table|nil section
function NyxMenu.FindSpot(spotId)
    for _, section in ipairs(NyxMenu.Locations) do
        for _, group in ipairs(section.groups or {}) do
            for _, spot in ipairs(group.spots or {}) do
                if spot.id == spotId then return spot, section end
            end
        end
    end
    return nil, nil
end

-- ---------------------------------------------------------------------------
-- Coordinate capture helper
-- ---------------------------------------------------------------------------

RegisterCommand('nyxcoords', function()
    local ped = PlayerPedId()
    local c = GetEntityCoords(ped)
    local h = GetEntityHeading(ped)

    local line = ('{ id = \'newspot\', label = \'New Spot\', coords = vec4(%.2f, %.2f, %.2f, %.1f) },')
        :format(c.x, c.y, c.z, h)

    print('^5[nyx]^7 ' .. line)
    TriggerEvent('chat:addMessage', { args = { 'nyx', line } })

    Nyx.Notify({
        title = 'Coordinates captured',
        description = 'Printed to F8 and chat — paste it into nyx_menu/config.lua.',
        type = 'info',
        duration = 6000
    })
end, false)
