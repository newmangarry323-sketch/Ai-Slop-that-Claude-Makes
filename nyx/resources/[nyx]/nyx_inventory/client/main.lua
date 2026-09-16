--[[ ==========================================================================
     nyx_inventory — client
     --------------------------------------------------------------------------
     Draws drop markers, opens the right container for where the player is
     standing, and relays NUI actions to the server. It holds no inventory state
     of its own beyond the last view the server sent.
     ========================================================================== ]]

local Util = Nyx.Util

local open = false
local drops = {}    -- id -> vector3
local nearestDrop = nil

-- ---------------------------------------------------------------------------
-- Open / close
-- ---------------------------------------------------------------------------

local function closeInventory()
    if not open then return end
    open = false

    SendNUIMessage({ action = 'close' })
    exports['nyx_lib']:ReleaseFocus()
    TriggerServerEvent('nyx_inventory:close')
    Nyx.Sound('back')
end

--- Work out what the second panel should show: a stash if standing in one, a
--- ground drop if standing on one, otherwise nothing.
local function secondaryRequest()
    local pos = GetEntityCoords(PlayerPedId())

    for _, stash in ipairs(NyxInv.Stashes) do
        if stash.coords and #(pos - stash.coords) <= 2.0 then
            return { kind = 'stash', id = stash.id }
        end
    end

    if nearestDrop then
        return { kind = 'drop', id = nearestDrop }
    end

    return {}
end

local function openInventory(request)
    if open then return closeInventory() end
    if IsPauseMenuActive() then return end
    if IsPedDeadOrDying(PlayerPedId(), true) then
        Nyx.Notify({ title = 'Inventory', description = 'Not while you are down.', type = 'warn' })
        return
    end

    open = true
    exports['nyx_lib']:RequestFocus(true)
    TriggerServerEvent('nyx_inventory:open', request or secondaryRequest())
    Nyx.Sound('open')
end

RegisterCommand(NyxInv.Command, function() openInventory() end, false)

if NyxInv.Key and NyxInv.Key ~= '' then
    RegisterKeyMapping(NyxInv.Command, 'Open your inventory', 'keyboard', NyxInv.Key)
end

exports('OpenStash', function(id)
    openInventory({ kind = 'stash', id = id })
end)

exports('Close', closeInventory)

AddEventHandler('nyx:forceClose', function(resource)
    if resource == GetCurrentResourceName() then
        open = false
        SendNUIMessage({ action = 'close' })
        TriggerServerEvent('nyx_inventory:close')
    end
end)

AddEventHandler('nyx:accentChanged', function(_, hex)
    SendNUIMessage({ action = 'nyx:theme', accent = hex })
end)

-- ---------------------------------------------------------------------------
-- Server pushes
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_inventory:opened', function()
    SendNUIMessage({
        action = 'open',
        accent = exports['nyx_lib']:GetAccent().hex
    })
end)

RegisterNetEvent('nyx_inventory:update', function(payload)
    SendNUIMessage({
        action = 'update',
        primary = payload.primary,
        secondary = payload.secondary
    })
end)

RegisterNetEvent('nyx_inventory:equip', function(payload)
    local ped = PlayerPedId()
    local hash = GetHashKey(payload.weapon)

    if not IsWeaponValid(hash) then return end

    GiveWeaponToPed(ped, hash, payload.ammo or 0, false, true)
    Nyx.Sound('pickup')
end)

RegisterNetEvent('nyx_inventory:consume', function(payload)
    local ped = PlayerPedId()

    if payload.effect == 'heal' then
        local max = GetEntityMaxHealth(ped)
        SetEntityHealth(ped, math.min(GetEntityHealth(ped) + payload.value, max))
    elseif payload.effect == 'armour' then
        SetPedArmour(ped, Util.Clamp(GetPedArmour(ped) + payload.value, 0, 100))
    end

    Nyx.Notify({ title = 'Used', description = payload.label, type = 'success' })
    Nyx.Sound('select')
end)

RegisterNetEvent('nyx_inventory:drops', function(list)
    drops = {}
    for _, drop in ipairs(list or {}) do
        drops[drop.id] = vector3(drop.coords.x, drop.coords.y, drop.coords.z)
    end
end)

-- ---------------------------------------------------------------------------
-- Ground drops
-- ---------------------------------------------------------------------------

CreateThread(function()
    while true do
        local wait = 700
        local pos = GetEntityCoords(PlayerPedId())
        local found = nil

        for id, coords in pairs(drops) do
            local dist = #(pos - coords)

            if dist < 25.0 then
                -- Close enough to be worth drawing every frame.
                wait = 0

                if NyxInv.DropMarker then
                    DrawMarker(
                        27,                                   -- flat ring
                        coords.x, coords.y, coords.z - 0.95,
                        0.0, 0.0, 0.0,
                        0.0, 0.0, 0.0,
                        0.45, 0.45, 0.45,
                        255, 43, 214, 110,
                        false, false, 2, false, nil, nil, false
                    )
                end

                if dist < NyxInv.DropDistance then found = id end
            end
        end

        nearestDrop = found
        Wait(wait)
    end
end)

-- Stash prompts.
CreateThread(function()
    local prompted = false

    for _, stash in ipairs(NyxInv.Stashes) do
        if stash.prompt and stash.coords then prompted = true end
    end

    if not prompted then return end

    while true do
        local wait = 800
        local pos = GetEntityCoords(PlayerPedId())

        if not open then
            for _, stash in ipairs(NyxInv.Stashes) do
                if stash.prompt and stash.coords and #(pos - stash.coords) < 12.0 then
                    wait = 0

                    if #(pos - stash.coords) < 2.0 then
                        BeginTextCommandDisplayHelp('STRING')
                        AddTextComponentSubstringPlayerName('Press ~INPUT_CONTEXT~ to open ' .. stash.label)
                        EndTextCommandDisplayHelp(0, false, true, -1)

                        if IsControlJustReleased(0, 38) then
                            openInventory({ kind = 'stash', id = stash.id })
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
-- NUI callbacks
-- ---------------------------------------------------------------------------

RegisterNUICallback('close', function(_, cb)
    closeInventory()
    cb({ ok = true })
end)

RegisterNUICallback('sound', function(data, cb)
    Nyx.Sound(data.key or 'hover')
    cb({ ok = true })
end)

RegisterNUICallback('move', function(data, cb)
    TriggerServerEvent('nyx_inventory:move', {
        from = data.from,
        fromSlot = data.fromSlot,
        to = data.to,
        toSlot = data.toSlot,
        count = data.count
    })
    cb({ ok = true })
end)

RegisterNUICallback('use', function(data, cb)
    TriggerServerEvent('nyx_inventory:use', data.slot)
    cb({ ok = true })
end)

RegisterNUICallback('drop', function(data, cb)
    TriggerServerEvent('nyx_inventory:drop', { slot = data.slot, count = data.count })
    cb({ ok = true })
end)

RegisterNUICallback('give', function(data, cb)
    -- The nearest player is resolved here, then re-checked for distance on the
    -- server. The client only ever nominates a candidate.
    local ped = PlayerPedId()
    local pos = GetEntityCoords(ped)

    local target, best = nil, NyxInv.GiveDistance

    for _, playerId in ipairs(GetActivePlayers()) do
        if playerId ~= PlayerId() then
            local otherPed = GetPlayerPed(playerId)
            local dist = #(pos - GetEntityCoords(otherPed))
            if dist < best then
                best = dist
                target = GetPlayerServerId(playerId)
            end
        end
    end

    if not target then
        Nyx.Notify({ title = 'Nobody there', description = 'Stand next to someone.', type = 'warn' })
        cb({ ok = false })
        return
    end

    TriggerServerEvent('nyx_inventory:give', { target = target, slot = data.slot, count = data.count })
    cb({ ok = true })
end)

AddEventHandler('onResourceStop', function(resource)
    if resource ~= GetCurrentResourceName() then return end
    SetNuiFocus(false, false)
end)
