--[[ ==========================================================================
     nyx_dealership — server
     --------------------------------------------------------------------------
     Owns prices and ownership. The client sends a vehicle id and nothing else;
     the price comes from config.lua on this side, so a modified client cannot
     buy a Zentorno for a dollar.
     ========================================================================== ]]

local Util = Nyx.Util

local function findVehicle(id)
    if type(id) ~= 'string' then return nil end
    for _, vehicle in ipairs(NyxDealer.Vehicles) do
        if vehicle.id == id then return vehicle end
    end
    return nil
end

local function ownedIds(src)
    local identifier = exports['nyx_lib']:GetIdentifier(src)
    if not identifier then return {} end

    local record = exports['nyx_lib']:DbGet('owned', identifier) or {}
    local ids = {}
    for id in pairs(record) do ids[#ids + 1] = id end
    return ids, record
end

local function allowedIds(src)
    local ids = {}
    for _, vehicle in ipairs(NyxDealer.Vehicles) do
        if not vehicle.ace or IsPlayerAceAllowed(src, vehicle.ace) then
            ids[#ids + 1] = vehicle.id
        end
    end
    return ids
end

local function pushCatalogue(src)
    TriggerClientEvent('nyx_dealership:catalogue', src, {
        allowed = allowedIds(src),
        owned = ownedIds(src)
    })
end

RegisterNetEvent('nyx:lib:clientReady', function()
    pushCatalogue(source)
end)

-- ---------------------------------------------------------------------------
-- Purchase
-- ---------------------------------------------------------------------------

--- Plates must be unique enough not to collide and short enough to fit. Eight
--- characters of upper alphanumerics is what GTA accepts.
local function makePlate()
    local chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
    local plate = ''
    for _ = 1, 8 do
        local i = math.random(#chars)
        plate = plate .. chars:sub(i, i)
    end
    return plate
end

local function refuse(src, reason)
    TriggerClientEvent('nyx_dealership:refused', src, reason)
end

RegisterNetEvent('nyx_dealership:buy', function(vehicleId)
    local src = source

    -- A purchase is the one action here worth spending money on, so it gets the
    -- tightest limit.
    if not exports['nyx_lib']:RateLimit(src, 'buy', 2000) then
        refuse(src, 'Slow down.')
        return
    end

    local vehicle = findVehicle(vehicleId)
    if not vehicle then return end

    if vehicle.ace and not IsPlayerAceAllowed(src, vehicle.ace) then
        refuse(src, 'You do not have access to that vehicle.')
        return
    end

    local identifier = exports['nyx_lib']:GetIdentifier(src)
    if not identifier then
        refuse(src, 'Your character is not loaded yet.')
        return
    end

    local price = Util.PositiveInt(vehicle.price) or 0

    -- RemoveMoney returns false when the player cannot afford it, which is the
    -- only check that matters — never read the balance and then deduct, because
    -- two purchases can interleave between those two calls.
    if price > 0 and not exports['nyx_lib']:RemoveMoney(src, NyxDealer.PayFrom, price) then
        refuse(src, 'You cannot afford that.')
        exports['nyx_lib']:Notify(src, {
            title = 'Declined',
            description = ('You need $%d in your %s.'):format(price, NyxDealer.PayFrom),
            type = 'error'
        })
        return
    end

    local plate = makePlate()

    if NyxDealer.StoreOwnedVehicles then
        local _, record = ownedIds(src)
        record[vehicle.id] = { plate = plate, model = vehicle.model, boughtAt = os.time() }
        exports['nyx_lib']:DbSet('owned', identifier, record)
    end

    TriggerClientEvent('nyx_dealership:bought', src, {
        id = vehicle.id,
        plate = plate,
        spawn = NyxDealer.Locations[1] and NyxDealer.Locations[1].spawn or nil
    })

    pushCatalogue(src)

    --- Hook for an existing garage or ownership system. Everything it needs is
    --- in the payload; see nyx/docs/api.md for the shape.
    TriggerEvent('nyx_dealership:purchased', src, {
        identifier = identifier,
        vehicleId = vehicle.id,
        model = vehicle.model,
        label = vehicle.label,
        plate = plate,
        price = price
    })

    print(('^5[nyx]^7 %s bought %s (%s) for $%d'):format(GetPlayerName(src), vehicle.label, plate, price))
end)

-- ---------------------------------------------------------------------------
-- Exports
-- ---------------------------------------------------------------------------

exports('GetOwnedVehicles', function(src)
    local _, record = ownedIds(src)
    return record
end)

--- Grant a vehicle without charging for it — for admin tools and rewards.
exports('GiveVehicle', function(src, vehicleId)
    local vehicle = findVehicle(vehicleId)
    if not vehicle then return false end

    local identifier = exports['nyx_lib']:GetIdentifier(src)
    if not identifier then return false end

    local plate = makePlate()
    local _, record = ownedIds(src)
    record[vehicle.id] = { plate = plate, model = vehicle.model, boughtAt = os.time() }
    exports['nyx_lib']:DbSet('owned', identifier, record)

    pushCatalogue(src)
    return true, plate
end)
