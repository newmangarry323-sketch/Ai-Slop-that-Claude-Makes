--[[ ==========================================================================
     Nyx — shared helpers
     Loaded into every resource via `shared_script '@nyx_lib/shared/util.lua'`.
     ========================================================================== ]]

Nyx = Nyx or {}
Nyx.Util = {}

local Util = Nyx.Util

--- Printf-style log that respects Nyx.Config.debug, prefixed with the caller.
function Util.Debug(...)
    if not (Nyx.Config and Nyx.Config.debug) then return end
    print(('[nyx:%s] %s'):format(GetCurrentResourceName(), table.concat({ ... }, ' ')))
end

--- Always-on warning. Use for misconfiguration the owner needs to see.
function Util.Warn(msg)
    print(('^3[nyx:%s] %s^7'):format(GetCurrentResourceName(), msg))
end

function Util.Error(msg)
    print(('^1[nyx:%s] %s^7'):format(GetCurrentResourceName(), msg))
end

--- Clamp n into [lo, hi].
function Util.Clamp(n, lo, hi)
    n = tonumber(n) or lo
    if n < lo then return lo end
    if n > hi then return hi end
    return n
end

--- Round to a whole number, away from zero on .5 (Lua's // floors negatives).
function Util.Round(n)
    n = tonumber(n) or 0
    return n >= 0 and math.floor(n + 0.5) or -math.floor(-n + 0.5)
end

--- Shallow copy. Config tables get handed to callers a lot; this keeps a stray
--- caller-side mutation from editing the config for everyone.
function Util.Copy(t)
    if type(t) ~= 'table' then return t end
    local out = {}
    for k, v in pairs(t) do out[k] = v end
    return out
end

--- Deep copy, for nested config trees.
function Util.DeepCopy(t)
    if type(t) ~= 'table' then return t end
    local out = {}
    for k, v in pairs(t) do out[k] = Util.DeepCopy(v) end
    return out
end

--- Count entries in a non-sequential table (#t lies about those).
function Util.Size(t)
    local n = 0
    for _ in pairs(t) do n = n + 1 end
    return n
end

--- Trim, collapse whitespace and cap length. Everything a player types goes
--- through this before it is stored or echoed to another client.
--- @param value any
--- @param maxLen number|nil defaults to 128
--- @return string
function Util.SafeString(value, maxLen)
    if type(value) ~= 'string' then
        if type(value) == 'number' then value = tostring(value) else return '' end
    end
    value = value:gsub('%s+', ' '):gsub('^%s*(.-)%s*$', '%1')
    return value:sub(1, maxLen or 128)
end

--- Coerce to a positive whole number, or nil if it is not one. Quantities from
--- NUI arrive as JSON numbers and must never be trusted: floats, negatives and
--- strings all turn up in a crafted payload.
--- @return number|nil
function Util.PositiveInt(value, max)
    local n = tonumber(value)
    if not n or n ~= n or n == math.huge then return nil end
    n = math.floor(n)
    if n < 1 then return nil end
    if max and n > max then return nil end
    return n
end

--- Distance between two vec3-ish tables or vectors, squared comparisons left
--- to the caller when they care about the sqrt.
function Util.Dist(a, b)
    return #(vector3(a.x, a.y, a.z) - vector3(b.x, b.y, b.z))
end

--- Turn '#ff2bd6' into '255, 43, 214' for --nyx-accent-rgb.
--- @return string|nil
function Util.HexToRgb(hex)
    if type(hex) ~= 'string' then return nil end
    hex = hex:gsub('#', '')
    if #hex == 3 then
        hex = hex:sub(1, 1):rep(2) .. hex:sub(2, 2):rep(2) .. hex:sub(3, 3):rep(2)
    end
    if not hex:match('^%x%x%x%x%x%x$') then return nil end
    return ('%d, %d, %d'):format(
        tonumber(hex:sub(1, 2), 16),
        tonumber(hex:sub(3, 4), 16),
        tonumber(hex:sub(5, 6), 16)
    )
end

--[[ --------------------------------------------------------------------------
     Notification shorthand
     --------------------------------------------------------------------------
     Because this file is compiled into every Nyx resource, `Nyx.Notify` is
     available everywhere without anyone writing out the export by hand. The
     signature differs by side, which IsDuplicityVersion() sorts out: it is
     true only on the server.

         client: Nyx.Notify{ title = 'Saved', type = 'success' }
         server: Nyx.Notify(source, { title = 'Saved', type = 'success' })
     -------------------------------------------------------------------------- ]]
if IsDuplicityVersion() then
    function Nyx.Notify(target, opts)
        TriggerClientEvent('nyx:notify', target, opts or {})
    end
else
    function Nyx.Notify(opts)
        exports['nyx_lib']:Notify(opts or {})
    end

    --- Front-end sound by role. Keys map to stock GTA sound sets, so this costs
    --- no audio assets. Unknown keys are ignored rather than erroring.
    function Nyx.Sound(key)
        exports['nyx_lib']:PlaySound(key)
    end
end
