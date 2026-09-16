--[[ ==========================================================================
     nyx_menu — server
     --------------------------------------------------------------------------
     The authority. Every client request names an id; this file looks the id up
     in config.lua, checks whatever gate the config attached to it, and only
     then acts. A client that invents an id, or asks for something it has no
     ACE for, gets nothing back.
     ========================================================================== ]]

local Util = Nyx.Util

local occupancy = {}      -- spotId -> count
local playerSpot = {}     -- src -> spotId
local occupancyDirty = false

local stats = {}          -- src -> { kills, deaths }

-- ---------------------------------------------------------------------------
-- Config lookups
-- ---------------------------------------------------------------------------

local function findSpot(spotId)
    if type(spotId) ~= 'string' then return nil end
    for _, section in ipairs(NyxMenu.Locations) do
        for _, group in ipairs(section.groups or {}) do
            for _, spot in ipairs(group.spots or {}) do
                if spot.id == spotId then return spot, group, section end
            end
        end
    end
    return nil
end

local function findWeapon(itemId)
    if type(itemId) ~= 'string' then return nil end
    for _, section in ipairs(NyxMenu.Weapons) do
        for _, group in ipairs(section.groups or {}) do
            for _, item in ipairs(group.items or {}) do
                if item.id == itemId then return item, group, section end
            end
        end
    end
    return nil
end

--- The narrowest ACE wins: item over group over section over the tab itself.
local function aceFor(item, group, section)
    return item.ace or (group and group.ace) or (section and section.ace) or NyxMenu.WeaponsTab.ace
end

local function allowed(src, ace)
    if not ace then return true end
    return IsPlayerAceAllowed(src, ace)
end

-- ---------------------------------------------------------------------------
-- Permissions push
-- ---------------------------------------------------------------------------

local function weaponIdsFor(src)
    local ids = {}
    if not NyxMenu.WeaponsTab.enabled then return ids end

    for _, section in ipairs(NyxMenu.Weapons) do
        for _, group in ipairs(section.groups or {}) do
            for _, item in ipairs(group.items or {}) do
                if allowed(src, aceFor(item, group, section)) then
                    ids[#ids + 1] = item.id
                end
            end
        end
    end

    return ids
end

local function pushPermissions(src)
    TriggerClientEvent('nyx_menu:permissions', src, weaponIdsFor(src))
end

-- ---------------------------------------------------------------------------
-- Stats
-- ---------------------------------------------------------------------------

local function statsFor(src)
    if stats[src] then return stats[src] end

    local id = exports['nyx_lib']:GetIdentifier(src)
    local stored = id and not NyxMenu.Stats.wipeOnRestart
        and exports['nyx_lib']:DbGet('stats', id)
        or nil

    stats[src] = {
        kills = stored and stored.kills or 0,
        deaths = stored and stored.deaths or 0
    }
    return stats[src]
end

local function saveStats(src)
    local record = stats[src]
    if not record then return end
    local id = exports['nyx_lib']:GetIdentifier(src)
    if id then exports['nyx_lib']:DbSet('stats', id, record) end
end

local function pushStats(src)
    TriggerClientEvent('nyx_menu:stats', src, statsFor(src))
end

RegisterNetEvent('nyx_menu:died', function(killerId)
    local src = source
    if not NyxMenu.Stats.enabled then return end

    -- One death per second per player is already generous; a client reporting
    -- faster than that is broken or lying.
    if not exports['nyx_lib']:RateLimit(src, 'died', 1000) then return end

    local victim = statsFor(src)
    victim.deaths = victim.deaths + 1
    saveStats(src)
    pushStats(src)

    -- The killer is a claim from the victim's client, so verify it points at a
    -- real connected player who is not the victim. See client/stats.lua for
    -- what this does and does not protect against.
    killerId = tonumber(killerId)
    if not killerId or killerId == src then return end
    if GetPlayerName(killerId) == nil then return end

    local killer = statsFor(killerId)
    killer.kills = killer.kills + 1
    saveStats(killerId)
    pushStats(killerId)
end)

-- ---------------------------------------------------------------------------
-- Teleport
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_menu:requestTeleport', function(spotId)
    local src = source

    if not exports['nyx_lib']:RateLimit(src, 'teleport', NyxMenu.Teleport.cooldownMs or 3000) then
        exports['nyx_lib']:Notify(src, { title = 'Slow down', description = 'Teleport is cooling down.', type = 'warn' })
        return
    end

    local spot, group, section = findSpot(spotId)
    if not spot then return end

    local ace = spot.ace or (group and group.ace) or (section and section.ace)
    if not allowed(src, ace) then
        exports['nyx_lib']:Notify(src, { title = 'Locked', description = 'You do not have access to that location.', type = 'error' })
        return
    end

    if spot.job then
        local job = exports['nyx_lib']:GetJob(src)
        if job ~= spot.job then
            exports['nyx_lib']:Notify(src, { title = 'Locked', description = 'Wrong job for that location.', type = 'error' })
            return
        end
    end

    TriggerClientEvent('nyx_menu:doTeleport', src, spot.id)
end)

-- ---------------------------------------------------------------------------
-- Weapons
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx_menu:requestWeapon', function(itemId)
    local src = source

    if not NyxMenu.WeaponsTab.enabled then return end
    if not exports['nyx_lib']:RateLimit(src, 'weapon', 400) then return end

    local item, group, section = findWeapon(itemId)
    if not item then return end

    if not allowed(src, aceFor(item, group, section)) then
        exports['nyx_lib']:Notify(src, { title = 'Locked', description = 'You do not have access to that.', type = 'error' })
        return
    end

    -- A kit is a list; a single weapon is a list of one. The client only ever
    -- receives names that came out of this config.
    local weapons = {}
    if item.kit then
        for _, entry in ipairs(item.kit) do
            weapons[#weapons + 1] = { weapon = entry.weapon, ammo = entry.ammo or 0 }
        end
    else
        weapons[1] = { weapon = item.weapon, ammo = item.ammo or 0 }
    end

    TriggerClientEvent('nyx_menu:doGiveWeapon', src, {
        label = item.label,
        weapons = weapons,
        clearFirst = item.kit ~= nil and NyxMenu.WeaponsTab.clearBeforeKit or false
    })
end)

-- ---------------------------------------------------------------------------
-- Occupancy
-- ---------------------------------------------------------------------------

local function setSpot(src, spotId)
    local previous = playerSpot[src]
    if previous == spotId then return end

    if previous and occupancy[previous] then
        occupancy[previous] = math.max(occupancy[previous] - 1, 0)
        if occupancy[previous] == 0 then occupancy[previous] = nil end
    end

    playerSpot[src] = spotId

    if spotId then
        occupancy[spotId] = (occupancy[spotId] or 0) + 1
    end

    occupancyDirty = true
end

RegisterNetEvent('nyx_menu:atSpot', function(spotId)
    local src = source
    if not NyxMenu.Occupancy.enabled then return end
    if not exports['nyx_lib']:RateLimit(src, 'atSpot', 1000) then return end

    if spotId ~= nil and not findSpot(spotId) then return end
    setSpot(src, spotId)
end)

if NyxMenu.Occupancy.enabled then
    -- Coalesce: a busy server would otherwise broadcast on every step anyone
    -- takes across a boundary.
    CreateThread(function()
        while true do
            Wait(1000)
            if occupancyDirty then
                occupancyDirty = false
                TriggerClientEvent('nyx_menu:occupancy', -1, occupancy)
            end
        end
    end)
end

-- ---------------------------------------------------------------------------
-- Lifecycle
-- ---------------------------------------------------------------------------

RegisterNetEvent('nyx:lib:clientReady', function()
    local src = source
    pushPermissions(src)
    if NyxMenu.Stats.enabled then pushStats(src) end
    TriggerClientEvent('nyx_menu:occupancy', src, occupancy)
end)

AddEventHandler('playerDropped', function()
    local src = source
    setSpot(src, nil)
    saveStats(src)
    stats[src] = nil
    playerSpot[src] = nil
end)

--- Re-evaluate a player's weapon access after you grant them an ACE at runtime.
RegisterCommand('nyxrefresh', function(src, args)
    if src ~= 0 and not IsPlayerAceAllowed(src, 'nyx.admin') then return end

    local target = tonumber(args[1]) or src
    if target == 0 then
        print('^3[nyx]^7 usage: nyxrefresh <serverId>')
        return
    end

    pushPermissions(target)
    print(('^5[nyx]^7 refreshed weapon permissions for %d'):format(target))
end, false)
