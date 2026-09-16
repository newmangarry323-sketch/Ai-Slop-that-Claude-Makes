--[[ ==========================================================================
     nyx_inventory — the container model
     --------------------------------------------------------------------------
     One data structure underneath everything: a container is a fixed number of
     slots, each holding { name, count }. Player inventories, ground drops and
     stashes are all containers; only their key prefix and their limits differ.

         player:<identifier>
         drop:<n>
         stash:<id>

     The server holds the only copy. Clients are sent a rendered view and send
     back moves, which are re-checked here against the real thing. There is no
     code path where a client's number becomes a stored number without passing
     through PositiveInt and a slot bounds check.

     A note on JSON: slot maps are keyed by integer in memory but json.encode
     turns a sparse integer-keyed table into an object with string keys, and
     decoding it back gives strings. Rather than fight that, `serialise` and
     `deserialise` convert to and from a flat list on the way to and from disk.
     ========================================================================== ]]

local Util = Nyx.Util

NyxInv.Containers = {}

local Containers = NyxInv.Containers

-- ---------------------------------------------------------------------------
-- Items
-- ---------------------------------------------------------------------------

--- @return table|nil the item definition, or nil for an item that does not exist
function NyxInv.ItemDef(name)
    if type(name) ~= 'string' then return nil end
    return NyxInv.Items[name]
end

-- ---------------------------------------------------------------------------
-- Containers
-- ---------------------------------------------------------------------------

--- Get or create a container. `opts` only applies at creation.
function NyxInv.Container(key, opts)
    local container = Containers[key]
    if container then return container end

    opts = opts or {}
    container = {
        key = key,
        slots = opts.slots or NyxInv.Slots,
        maxWeight = opts.maxWeight or NyxInv.MaxWeight,
        label = opts.label or 'Inventory',
        items = {}
    }

    Containers[key] = container
    return container
end

function NyxInv.Weight(container)
    local total = 0
    for _, entry in pairs(container.items) do
        local def = NyxInv.ItemDef(entry.name)
        if def then total = total + (def.weight or 0) * entry.count end
    end
    return total
end

local function firstFreeSlot(container)
    for slot = 1, container.slots do
        if not container.items[slot] then return slot end
    end
    return nil
end

--- Where `count` of `name` could go, without actually putting it there.
--- @return boolean fits, number weightAfter
function NyxInv.CanFit(container, name, count)
    local def = NyxInv.ItemDef(name)
    if not def then return false, 0 end

    local weightAfter = NyxInv.Weight(container) + (def.weight or 0) * count
    if weightAfter > container.maxWeight then return false, weightAfter end

    -- Stackables need one free slot at worst; non-stackables need one each.
    if def.stack then
        for slot = 1, container.slots do
            local entry = container.items[slot]
            if entry and entry.name == name then return true, weightAfter end
        end
        return firstFreeSlot(container) ~= nil, weightAfter
    end

    local free = 0
    for slot = 1, container.slots do
        if not container.items[slot] then free = free + 1 end
    end

    return free >= count, weightAfter
end

--- @return number the number actually added, which can be less than requested
function NyxInv.AddItem(container, name, count)
    local def = NyxInv.ItemDef(name)
    if not def then return 0 end

    count = Util.PositiveInt(count, 10000)
    if not count then return 0 end

    local added = 0

    if def.stack then
        -- Top up an existing stack first so a full inventory can still take more
        -- of something it already holds.
        for slot = 1, container.slots do
            local entry = container.items[slot]
            if entry and entry.name == name then
                entry.count = entry.count + count
                return count
            end
        end
    end

    while added < count do
        local slot = firstFreeSlot(container)
        if not slot then break end

        if def.stack then
            container.items[slot] = { name = name, count = count - added }
            added = count
        else
            container.items[slot] = { name = name, count = 1 }
            added = added + 1
        end
    end

    return added
end

--- @return number the number actually removed
function NyxInv.RemoveSlot(container, slot, count)
    local entry = container.items[slot]
    if not entry then return 0 end

    count = Util.PositiveInt(count, entry.count) or entry.count
    if count >= entry.count then
        local removed = entry.count
        container.items[slot] = nil
        return removed
    end

    entry.count = entry.count - count
    return count
end

--- Remove by name across every slot. Used by consumables and by anything
--- outside the resource calling the RemoveItem export.
--- @return boolean true only when the full amount was removed
function NyxInv.RemoveItem(container, name, count)
    count = Util.PositiveInt(count) or 1

    local have = 0
    for _, entry in pairs(container.items) do
        if entry.name == name then have = have + entry.count end
    end
    if have < count then return false end

    local left = count
    for slot = 1, container.slots do
        if left <= 0 then break end
        local entry = container.items[slot]
        if entry and entry.name == name then
            left = left - NyxInv.RemoveSlot(container, slot, math.min(left, entry.count))
        end
    end

    return left <= 0
end

function NyxInv.CountItem(container, name)
    local total = 0
    for _, entry in pairs(container.items) do
        if entry.name == name then total = total + entry.count end
    end
    return total
end

--- Move between containers, or within one. All the bounds checking lives here
--- so callers cannot skip it.
--- @return boolean ok, string|nil reason
function NyxInv.Move(from, fromSlot, to, toSlot, count)
    fromSlot = Util.PositiveInt(fromSlot, from.slots)
    if not fromSlot then return false, 'bad source slot' end

    local source = from.items[fromSlot]
    if not source then return false, 'empty slot' end

    count = Util.PositiveInt(count, source.count) or source.count

    local def = NyxInv.ItemDef(source.name)
    if not def then return false, 'unknown item' end

    -- Within one container a move is free; across containers it has to fit.
    if from ~= to then
        local fits = NyxInv.CanFit(to, source.name, count)
        if not fits then return false, 'no room' end
    end

    toSlot = Util.PositiveInt(toSlot, to.slots)

    if not toSlot then
        -- No target slot named: let AddItem find one.
        NyxInv.RemoveSlot(from, fromSlot, count)
        local added = NyxInv.AddItem(to, source.name, count)
        if added < count then
            -- Put back whatever would not fit rather than deleting it.
            NyxInv.AddItem(from, source.name, count - added)
        end
        return true
    end

    local target = to.items[toSlot]

    if not target then
        NyxInv.RemoveSlot(from, fromSlot, count)
        to.items[toSlot] = { name = source.name, count = count }
        return true
    end

    if target.name == source.name and def.stack then
        NyxInv.RemoveSlot(from, fromSlot, count)
        target.count = target.count + count
        return true
    end

    -- Different items: swap, but only when the whole stack is moving. A partial
    -- move onto an occupied slot has nowhere to put the remainder.
    if count ~= source.count then return false, 'slot occupied' end

    from.items[fromSlot] = target
    to.items[toSlot] = source
    return true
end

-- ---------------------------------------------------------------------------
-- Rendering for the page
-- ---------------------------------------------------------------------------

function NyxInv.View(container)
    local slots = {}

    for slot = 1, container.slots do
        local entry = container.items[slot]
        if entry then
            local def = NyxInv.ItemDef(entry.name) or {}
            slots[#slots + 1] = {
                slot = slot,
                name = entry.name,
                label = def.label or entry.name,
                count = entry.count,
                weight = (def.weight or 0) * entry.count,
                image = def.image,
                usable = def.use ~= nil or def.weapon ~= nil
            }
        end
    end

    local used = 0
    for _ in pairs(container.items) do used = used + 1 end

    return {
        key = container.key,
        label = container.label,
        slots = container.slots,
        used = used,
        weight = NyxInv.Weight(container),
        maxWeight = container.maxWeight,
        items = slots
    }
end

-- ---------------------------------------------------------------------------
-- Persistence
-- ---------------------------------------------------------------------------

--- Slot map -> flat list, so it survives a JSON round trip unchanged.
local function serialise(container)
    local list = {}
    for slot = 1, container.slots do
        local entry = container.items[slot]
        if entry then
            list[#list + 1] = { slot = slot, name = entry.name, count = entry.count }
        end
    end
    return list
end

local function deserialise(container, list)
    container.items = {}
    for _, entry in ipairs(list or {}) do
        local slot = Util.PositiveInt(entry.slot, container.slots)
        local count = Util.PositiveInt(entry.count, 100000)
        -- Drop anything whose item was deleted from config since the save.
        if slot and count and NyxInv.ItemDef(entry.name) then
            container.items[slot] = { name = entry.name, count = count }
        end
    end
end

function NyxInv.Save(container, bucket, key)
    exports['nyx_lib']:DbSet(bucket, key, serialise(container))
end

function NyxInv.Load(container, bucket, key)
    local stored = exports['nyx_lib']:DbGet(bucket, key)
    if stored then
        deserialise(container, stored)
        return true
    end
    return false
end
