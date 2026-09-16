--[[ ==========================================================================
     nyx_inventory — server
     --------------------------------------------------------------------------
     Access control, in one sentence: a player may touch their own container and
     exactly one secondary container, the one the server opened for them. Every
     request names containers by key, and every key is checked against that pair
     before a single item moves.
     ========================================================================== ]]

local Util = Nyx.Util

local playerKey = {}     -- src -> 'player:<identifier>'
local openSecondary = {} -- src -> key of the container they have open, if any
local dropCounter = 0
local drops = {}         -- key -> { coords, expires }

-- ---------------------------------------------------------------------------
-- Player containers
-- ---------------------------------------------------------------------------

local function containerFor(src)
    local key = playerKey[src]
    if key then return NyxInv.Containers[key], key end

    local identifier = exports['nyx_lib']:GetIdentifier(src)
    if not identifier then return nil, nil end

    key = 'player:' .. identifier
    playerKey[src] = key

    local container = NyxInv.Container(key, { label = 'Inventory' })

    if not NyxInv.Load(container, 'inv', identifier) then
        -- First time this character has ever opened an inventory.
        for _, entry in ipairs(NyxInv.StartingItems) do
            NyxInv.AddItem(container, entry.name, entry.count)
        end
        NyxInv.Save(container, 'inv', identifier)
    end

    return container, key
end

local function savePlayer(src)
    local container = NyxInv.Containers[playerKey[src] or '']
    if not container then return end

    local identifier = exports['nyx_lib']:GetIdentifier(src)
    if identifier then NyxInv.Save(container, 'inv', identifier) end
end

--- The one place that decides whether `src` is allowed to touch `key`.
local function mayAccess(src, key)
    if not key then return false end
    if key == playerKey[src] then return true end
    return openSecondary[src] == key
end

local function push(src)
    local container = containerFor(src)
    if not container then return end

    local secondary = openSecondary[src] and NyxInv.Containers[openSecondary[src]] or nil

    TriggerClientEvent('nyx_inventory:update', src, {
        primary = NyxInv.View(container),
        secondary = secondary and NyxInv.View(secondary) or nil
    })
end

NyxInv.Push = push

-- ---------------------------------------------------------------------------
-- Opening
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_inventory:open', function(request)
    local src = source
    if not exports['nyx_lib']:RateLimit(src, 'invOpen', 300) then return end

    local container = containerFor(src)
    if not container then
        exports['nyx_lib']:Notify(src, { title = 'Inventory', description = 'Your character is still loading.', type = 'warn' })
        return
    end

    openSecondary[src] = nil

    request = type(request) == 'table' and request or {}

    if request.kind == 'stash' then
        for _, stash in ipairs(NyxInv.Stashes) do
            if stash.id == request.id then
                if stash.ace and not IsPlayerAceAllowed(src, stash.ace) then
                    exports['nyx_lib']:Notify(src, { title = 'Locked', description = 'You cannot open that.', type = 'error' })
                    return
                end

                local key = 'stash:' .. stash.id
                local stashContainer = NyxInv.Container(key, {
                    slots = stash.slots,
                    maxWeight = stash.maxWeight,
                    label = stash.label
                })

                if not stashContainer.loaded then
                    NyxInv.Load(stashContainer, 'stash', stash.id)
                    stashContainer.loaded = true
                end

                openSecondary[src] = key
                break
            end
        end

    elseif request.kind == 'drop' then
        local key = 'drop:' .. tostring(request.id)
        local drop = drops[key]

        if drop and NyxInv.Containers[key] then
            -- Distance is checked against the server's own record of where the
            -- drop is, not against anything the client sent.
            local pos = GetEntityCoords(GetPlayerPed(src))
            if #(pos - drop.coords) <= NyxInv.DropDistance + 1.5 then
                openSecondary[src] = key
            end
        end
    end

    TriggerClientEvent('nyx_inventory:opened', src)
    push(src)
end)

RegisterNetEvent('nyx_inventory:close', function()
    local src = source
    local secondary = openSecondary[src]

    if secondary then
        local container = NyxInv.Containers[secondary]
        if container and secondary:sub(1, 6) == 'stash:' then
            NyxInv.Save(container, 'stash', secondary:sub(7))
        end
    end

    openSecondary[src] = nil
    savePlayer(src)
end)

-- ---------------------------------------------------------------------------
-- Moving
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_inventory:move', function(payload)
    local src = source
    if not exports['nyx_lib']:RateLimit(src, 'invMove', 60) then return end
    if type(payload) ~= 'table' then return end

    if not mayAccess(src, payload.from) or not mayAccess(src, payload.to) then
        push(src)
        return
    end

    local from = NyxInv.Containers[payload.from]
    local to = NyxInv.Containers[payload.to]
    if not from or not to then return end

    local ok, reason = NyxInv.Move(from, payload.fromSlot, to, payload.toSlot, payload.count)
    if not ok and reason == 'no room' then
        exports['nyx_lib']:Notify(src, { title = 'No room', description = 'That will not fit.', type = 'warn' })
    end

    savePlayer(src)
    push(src)

    -- Anyone else standing in the same drop or stash sees it change too.
    if payload.from ~= playerKey[src] or payload.to ~= playerKey[src] then
        for other, key in pairs(openSecondary) do
            if other ~= src and (key == payload.from or key == payload.to) then push(other) end
        end
    end
end)

-- ---------------------------------------------------------------------------
-- Using
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_inventory:use', function(slot)
    local src = source
    if not exports['nyx_lib']:RateLimit(src, 'invUse', 500) then return end

    local container = containerFor(src)
    if not container then return end

    slot = Util.PositiveInt(slot, container.slots)
    if not slot then return end

    local entry = container.items[slot]
    if not entry then return end

    local def = NyxInv.ItemDef(entry.name)
    if not def then return end

    if def.weapon then
        TriggerClientEvent('nyx_inventory:equip', src, { weapon = def.weapon, ammo = def.ammo or 0 })
        return
    end

    if not def.use or def.use == 'none' then
        exports['nyx_lib']:Notify(src, { title = def.label, description = 'Nothing happens.', type = 'info' })
        return
    end

    NyxInv.RemoveSlot(container, slot, 1)
    TriggerClientEvent('nyx_inventory:consume', src, { effect = def.use, value = def.value or 0, label = def.label })

    savePlayer(src)
    push(src)
end)

-- ---------------------------------------------------------------------------
-- Giving
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_inventory:give', function(payload)
    local src = source
    if not exports['nyx_lib']:RateLimit(src, 'invGive', 600) then return end
    if type(payload) ~= 'table' then return end

    local target = tonumber(payload.target)
    if not target or target == src or GetPlayerName(target) == nil then return end

    -- Proximity is measured server-side from both peds. A client claiming to be
    -- next to someone across the map gets nothing.
    local here = GetEntityCoords(GetPlayerPed(src))
    local there = GetEntityCoords(GetPlayerPed(target))
    if #(here - there) > NyxInv.GiveDistance + 1.0 then
        exports['nyx_lib']:Notify(src, { title = 'Too far', description = 'Stand closer.', type = 'warn' })
        return
    end

    local mine = containerFor(src)
    local theirs = containerFor(target)
    if not mine or not theirs then return end

    local slot = Util.PositiveInt(payload.slot, mine.slots)
    if not slot then return end

    local entry = mine.items[slot]
    if not entry then return end

    local count = Util.PositiveInt(payload.count, entry.count) or entry.count

    if not NyxInv.CanFit(theirs, entry.name, count) then
        exports['nyx_lib']:Notify(src, { title = 'No room', description = 'They cannot carry that.', type = 'warn' })
        return
    end

    local def = NyxInv.ItemDef(entry.name) or {}
    NyxInv.RemoveSlot(mine, slot, count)
    NyxInv.AddItem(theirs, entry.name, count)

    savePlayer(src)
    savePlayer(target)
    push(src)
    push(target)

    exports['nyx_lib']:Notify(target, {
        title = 'Received',
        description = ('%dx %s from %s'):format(count, def.label or entry.name, exports['nyx_lib']:GetName(src)),
        type = 'success'
    })
end)

-- ---------------------------------------------------------------------------
-- Dropping
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_inventory:drop', function(payload)
    local src = source
    if not exports['nyx_lib']:RateLimit(src, 'invDrop', 400) then return end
    if type(payload) ~= 'table' then return end

    local container = containerFor(src)
    if not container then return end

    local slot = Util.PositiveInt(payload.slot, container.slots)
    if not slot then return end

    local entry = container.items[slot]
    if not entry then return end

    local count = Util.PositiveInt(payload.count, entry.count) or entry.count
    local coords = GetEntityCoords(GetPlayerPed(src))

    -- Merge into a drop already at the player's feet rather than littering the
    -- ground with one pile per item.
    local key = nil
    for existingKey, drop in pairs(drops) do
        if #(coords - drop.coords) < 1.2 then key = existingKey break end
    end

    if not key then
        dropCounter = dropCounter + 1
        key = 'drop:' .. dropCounter
        drops[key] = {
            coords = coords,
            expires = NyxInv.DropLifetime > 0 and (os.time() + NyxInv.DropLifetime) or nil
        }
        NyxInv.Container(key, { label = 'Ground', slots = 40, maxWeight = 1000000 })
    end

    NyxInv.RemoveSlot(container, slot, count)
    NyxInv.AddItem(NyxInv.Containers[key], entry.name, count)

    savePlayer(src)
    push(src)

    TriggerClientEvent('nyx_inventory:drops', -1, { { id = key:sub(6), coords = drops[key].coords } })
end)

--- Reap expired drops and tell every client the current list.
CreateThread(function()
    while true do
        Wait(30000)

        local now = os.time()
        local live = {}

        for key, drop in pairs(drops) do
            local container = NyxInv.Containers[key]
            local empty = not container or not next(container.items)

            if empty or (drop.expires and now > drop.expires) then
                drops[key] = nil
                NyxInv.Containers[key] = nil

                -- Anyone standing in a drop that just vanished has to be kicked
                -- out of it, or they would keep a handle on a dead container.
                for src, openKey in pairs(openSecondary) do
                    if openKey == key then
                        openSecondary[src] = nil
                        push(src)
                    end
                end
            else
                live[#live + 1] = { id = key:sub(6), coords = drop.coords }
            end
        end

        TriggerClientEvent('nyx_inventory:drops', -1, live)
    end
end)

-- ---------------------------------------------------------------------------
-- Lifecycle
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx:lib:clientReady', function()
    local src = source
    containerFor(src)

    local live = {}
    for key, drop in pairs(drops) do
        live[#live + 1] = { id = key:sub(6), coords = drop.coords }
    end

    TriggerClientEvent('nyx_inventory:drops', src, live)
end)

AddEventHandler('playerDropped', function()
    local src = source
    savePlayer(src)

    local secondary = openSecondary[src]
    if secondary and secondary:sub(1, 6) == 'stash:' then
        local container = NyxInv.Containers[secondary]
        if container then NyxInv.Save(container, 'stash', secondary:sub(7)) end
    end

    openSecondary[src] = nil
    -- The container itself stays in memory keyed by identifier, so a reconnect
    -- during the same session picks up exactly where they left off.
    playerKey[src] = nil
end)

AddEventHandler('onResourceStop', function(resource)
    if resource ~= GetCurrentResourceName() then return end

    for key, container in pairs(NyxInv.Containers) do
        if key:sub(1, 7) == 'player:' then
            NyxInv.Save(container, 'inv', key:sub(8))
        elseif key:sub(1, 6) == 'stash:' then
            NyxInv.Save(container, 'stash', key:sub(7))
        end
    end
end)

-- ---------------------------------------------------------------------------
-- Exports — the surface other resources use to move items around.
-- ---------------------------------------------------------------------------

exports('AddItem', function(src, name, count)
    local container = containerFor(src)
    if not container then return false end

    if not NyxInv.CanFit(container, name, count or 1) then return false end

    local added = NyxInv.AddItem(container, name, count or 1)
    savePlayer(src)
    push(src)
    return added > 0, added
end)

exports('RemoveItem', function(src, name, count)
    local container = containerFor(src)
    if not container then return false end

    local ok = NyxInv.RemoveItem(container, name, count or 1)
    savePlayer(src)
    push(src)
    return ok
end)

exports('GetItemCount', function(src, name)
    local container = containerFor(src)
    return container and NyxInv.CountItem(container, name) or 0
end)

exports('HasItem', function(src, name, count)
    local container = containerFor(src)
    if not container then return false end
    return NyxInv.CountItem(container, name) >= (count or 1)
end)

exports('GetInventory', function(src)
    local container = containerFor(src)
    return container and NyxInv.View(container) or nil
end)
