--[[ ==========================================================================
     Nyx — flat-file key/value store
     --------------------------------------------------------------------------
     Every Nyx resource that needs to remember something writes it here. Buckets
     are JSON files under nyx_lib/data/, read once into memory on first touch
     and flushed on a timer, on player drop and on resource stop.

     Why files and not MySQL: the suite has to run on a fresh server with no
     database, no oxmysql and no schema migration. If you already run oxmysql,
     docs/configuration.md has a drop-in replacement for the two functions at
     the bottom of this file — nothing else in the suite has to change, because
     nothing else in the suite knows how persistence works.

     Not suitable for high-frequency writes (think per-tick position logging).
     Perfectly suitable for what it is used for: preferences, wallets, garages
     and inventories, which change a few times a minute at worst.
     ========================================================================== ]]

Nyx = Nyx or {}
Nyx.Store = {}

local Store = Nyx.Store
local Util = Nyx.Util

local cache = {}   -- bucket -> table
local dirty = {}   -- bucket -> true

--- Buckets become file names, so they must never be able to escape data/.
local function safeBucket(name)
    return (tostring(name or 'default'):gsub('[^%w_%-%.]', '_')):sub(1, 64)
end

local function pathFor(bucket)
    return ('data/%s.json'):format(bucket)
end

--- Read a bucket from disk once; subsequent calls hit the cache.
local function load(bucket)
    if cache[bucket] then return cache[bucket] end

    local raw = LoadResourceFile(GetCurrentResourceName(), pathFor(bucket))
    if not raw or raw == '' then
        cache[bucket] = {}
        return cache[bucket]
    end

    local ok, decoded = pcall(json.decode, raw)
    if not ok or type(decoded) ~= 'table' then
        -- A truncated write (server killed mid-save) should not wipe the data
        -- silently. Keep the broken file so it can be recovered by hand.
        Util.Error(('bucket "%s" is not valid JSON; starting empty. The old file is kept at %s.broken'):format(bucket, pathFor(bucket)))
        SaveResourceFile(GetCurrentResourceName(), pathFor(bucket) .. '.broken', raw, -1)
        cache[bucket] = {}
        return cache[bucket]
    end

    cache[bucket] = decoded
    return cache[bucket]
end

--- @param bucket string
--- @param key string
--- @param default any returned when the key is absent
function Store.Get(bucket, key, default)
    bucket = safeBucket(bucket)
    local value = load(bucket)[tostring(key)]
    if value == nil then return default end
    return value
end

--- Writing nil deletes the key.
function Store.Set(bucket, key, value)
    bucket = safeBucket(bucket)
    load(bucket)[tostring(key)] = value
    dirty[bucket] = true
    return value
end

--- The whole bucket, as a copy — callers must not hold a live reference into
--- the cache or their edits would bypass the dirty flag and never be saved.
function Store.All(bucket)
    return Util.DeepCopy(load(safeBucket(bucket)))
end

function Store.Delete(bucket, key)
    return Store.Set(bucket, key, nil)
end

--- Write one bucket to disk if it changed. Returns false when there was
--- nothing to do.
function Store.Flush(bucket)
    bucket = safeBucket(bucket)
    if not dirty[bucket] or not cache[bucket] then return false end

    local ok, encoded = pcall(json.encode, cache[bucket])
    if not ok then
        Util.Error(('bucket "%s" could not be encoded; skipping this flush'):format(bucket))
        return false
    end

    SaveResourceFile(GetCurrentResourceName(), pathFor(bucket), encoded, -1)
    dirty[bucket] = nil
    Util.Debug('flushed bucket', bucket)
    return true
end

function Store.FlushAll()
    local n = 0
    for bucket in pairs(dirty) do
        if Store.Flush(bucket) then n = n + 1 end
    end
    return n
end

-- Periodic flush. Buckets that did not change cost nothing here.
CreateThread(function()
    local interval = math.max(Nyx.Config.autosaveInterval or 120, 15) * 1000
    while true do
        Wait(interval)
        Store.FlushAll()
    end
end)

-- Covers both `stop nyx_lib` and a full server shutdown, which stops every
-- resource in turn.
AddEventHandler('onResourceStop', function(resource)
    if resource ~= GetCurrentResourceName() then return end
    local n = Store.FlushAll()
    if n > 0 then print(('^5[nyx]^7 flushed %d bucket(s) on stop'):format(n)) end
end)

-- ---------------------------------------------------------------------------
-- Exports — namespaced per calling resource so two resources cannot collide
-- on a bucket name by accident.
-- ---------------------------------------------------------------------------

local function namespaced(bucket)
    local caller = GetInvokingResource() or GetCurrentResourceName()
    return ('%s.%s'):format(caller, bucket or 'default')
end

exports('DbGet', function(bucket, key, default) return Store.Get(namespaced(bucket), key, default) end)
exports('DbSet', function(bucket, key, value) return Store.Set(namespaced(bucket), key, value) end)
exports('DbAll', function(bucket) return Store.All(namespaced(bucket)) end)
exports('DbDelete', function(bucket, key) return Store.Delete(namespaced(bucket), key) end)
exports('DbFlush', function(bucket) return Store.Flush(namespaced(bucket)) end)
