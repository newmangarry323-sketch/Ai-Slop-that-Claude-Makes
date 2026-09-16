--[[ ==========================================================================
     nyx_hud — preferences
     --------------------------------------------------------------------------
     Validates, stores and broadcasts the per-player HUD settings. Everything
     that arrives from NUI is clamped here before it can reach the page or the
     server, so no other file has to be suspicious of its inputs.
     ========================================================================== ]]

local Util = Nyx.Util

local prefs = Util.DeepCopy(NyxHud.Defaults)
local ready = false

-- ---------------------------------------------------------------------------
-- Validation
-- ---------------------------------------------------------------------------

local function clampNumber(key, value, fallback)
    local limit = NyxHud.Limits[key]
    local n = tonumber(value)
    if not n or n ~= n then return fallback end
    if not limit then return n end
    return Util.Clamp(math.floor(n), limit[1], limit[2])
end

--- Rebuild a crosshair table from scratch out of known-good fields. Anything
--- the caller sends that is not in this list is dropped rather than merged,
--- which is what keeps a crafted payload from smuggling a key through to the
--- page's style attributes.
local function sanitiseCrosshair(input, base)
    base = base or NyxHud.Defaults.crosshair
    input = type(input) == 'table' and input or {}

    local out = {}

    out.style = NyxHud.CrosshairStyles[input.style] and input.style or base.style
    out.size = clampNumber('size', input.size, base.size)
    out.thickness = clampNumber('thickness', input.thickness, base.thickness)
    out.gap = clampNumber('gap', input.gap, base.gap)
    out.opacity = clampNumber('opacity', input.opacity, base.opacity)

    -- A colour lands in a CSS style attribute, so only a literal hex passes.
    local colour = type(input.colour) == 'string' and input.colour or base.colour
    out.colour = colour:match('^#%x%x%x%x%x%x$') and colour or NyxHud.Defaults.crosshair.colour

    for _, flag in ipairs({ 'outline', 'dot', 'dynamic', 'hideInVehicle', 'onlyWhenArmed' }) do
        if input[flag] ~= nil then out[flag] = input[flag] == true
        else out[flag] = base[flag] == true end
    end

    return out
end

local function sanitise(input)
    local defaults = NyxHud.Defaults
    input = type(input) == 'table' and input or {}

    local out = {}

    out.style = (input.style == 1 or input.style == 2) and input.style or defaults.style
    out.moneyStyle = (input.moneyStyle == 3 or input.moneyStyle == 4) and input.moneyStyle or defaults.moneyStyle

    out.toggles = {}
    for key, fallback in pairs(defaults.toggles) do
        local value = input.toggles and input.toggles[key]
        if value == nil then out.toggles[key] = fallback else out.toggles[key] = value == true end
    end

    out.crosshair = sanitiseCrosshair(input.crosshair, defaults.crosshair)
    return out
end

-- ---------------------------------------------------------------------------
-- Access
-- ---------------------------------------------------------------------------

function NyxHud.GetPrefs()
    return Util.DeepCopy(prefs)
end

--- Push the whole preference set to the page and tell the rest of the client
--- something changed. Called after every mutation; the page is cheap to
--- re-render because it is a handful of DOM nodes.
local function publish(save)
    SendNUIMessage({ action = 'prefs', prefs = prefs })
    TriggerEvent('nyx_hud:prefsChanged', NyxHud.GetPrefs())

    if save and ready then
        TriggerServerEvent('nyx_hud:save', prefs)
    end
end

NyxHud.Publish = publish

--- @param field 'style'|'moneyStyle'
function NyxHud.SetStyle(field, value)
    value = tonumber(value)

    if field == 'style' and (value == 1 or value == 2) then
        prefs.style = value
    elseif field == 'moneyStyle' and (value == 3 or value == 4) then
        prefs.moneyStyle = value
    else
        return false
    end

    publish(true)
    return true
end

function NyxHud.SetToggle(key, value)
    if prefs.toggles[key] == nil then return false end
    prefs.toggles[key] = value == true
    publish(true)
    return true
end

function NyxHud.SetCrosshair(patch)
    -- Merge over the current values so the menu can send one changed field.
    local merged = Util.DeepCopy(prefs.crosshair)
    for k, v in pairs(type(patch) == 'table' and patch or {}) do merged[k] = v end

    prefs.crosshair = sanitiseCrosshair(merged, prefs.crosshair)
    publish(true)
    return true
end

function NyxHud.ResetPrefs()
    prefs = Util.DeepCopy(NyxHud.Defaults)
    publish(true)
    return true
end

-- The server's stored copy, applied once at spawn.
RegisterNetEvent('nyx_hud:load', function(stored)
    prefs = sanitise(stored)
    ready = true
    publish(false)
end)

-- ---------------------------------------------------------------------------
-- Exports — this is the surface nyx_menu's Miscellaneous tab drives.
-- ---------------------------------------------------------------------------

exports('GetPrefs', function() return NyxHud.GetPrefs() end)
exports('SetStyle', function(field, value) return NyxHud.SetStyle(field, value) end)
exports('SetToggle', function(key, value) return NyxHud.SetToggle(key, value) end)
exports('SetCrosshair', function(patch) return NyxHud.SetCrosshair(patch) end)
exports('ResetPrefs', function() return NyxHud.ResetPrefs() end)
exports('GetDefaults', function() return Util.DeepCopy(NyxHud.Defaults) end)
