--[[ ==========================================================================
     Nyx — client API
     --------------------------------------------------------------------------
     Everything other resources call lives here:

         exports['nyx_lib']:Notify{ title = ..., description = ..., type = ... }
         exports['nyx_lib']:RequestFocus(cursor)   -- returns true if granted
         exports['nyx_lib']:ReleaseFocus()
         exports['nyx_lib']:IsAnyMenuOpen()
         exports['nyx_lib']:GetPlayerData()
         exports['nyx_lib']:GetFramework()
         exports['nyx_lib']:GetAccent()            -- { id, label, hex }
         exports['nyx_lib']:GetAccentRgb()         -- '255, 43, 214'
         exports['nyx_lib']:SetAccent(id)
         exports['nyx_lib']:GetAccents()           -- presets + unlock state
         exports['nyx_lib']:PlaySound(key)
     ========================================================================== ]]

local Util = Nyx.Util
local Bridge = Nyx.Bridge

-- ---------------------------------------------------------------------------
-- Notifications
-- ---------------------------------------------------------------------------

local TYPES = { success = true, error = true, warn = true, info = true }

local function notify(opts)
    if type(opts) ~= 'table' then opts = { title = tostring(opts) } end

    SendNUIMessage({
        action = 'notify',
        title = Util.SafeString(opts.title, 64),
        description = Util.SafeString(opts.description, 220),
        type = TYPES[opts.type] and opts.type or nil,
        duration = Util.Clamp(opts.duration or Nyx.Config.notifyDuration, 900, 20000)
    })
end

exports('Notify', notify)

RegisterNetEvent('nyx:notify', function(opts)
    -- Server-originated; the payload is still run through SafeString above.
    notify(opts)
end)

exports('ClearNotifications', function()
    SendNUIMessage({ action = 'notify:clear' })
end)

-- ---------------------------------------------------------------------------
-- Focus broker
-- ---------------------------------------------------------------------------
-- Two resources both calling SetNuiFocus(true, true) is the single most common
-- way a FiveM UI suite traps the player's cursor: whichever one closes last
-- wins, and if it never closes the player is stuck. One owner at a time, and
-- the incoming menu evicts the outgoing one.

local focusOwner = nil

local function releaseFocus(who)
    if focusOwner ~= who then return false end
    focusOwner = nil
    SetNuiFocus(false, false)
    SetNuiFocusKeepInput(false)
    return true
end

exports('RequestFocus', function(cursor)
    local caller = GetInvokingResource() or GetCurrentResourceName()

    if focusOwner and focusOwner ~= caller then
        -- Tell the incumbent to shut itself down cleanly, then take over.
        TriggerEvent('nyx:forceClose', focusOwner)
        focusOwner = nil
    end

    focusOwner = caller
    SetNuiFocus(true, cursor ~= false)
    SetNuiFocusKeepInput(false)
    Util.Debug('focus ->', caller)
    return true
end)

exports('ReleaseFocus', function()
    return releaseFocus(GetInvokingResource() or GetCurrentResourceName())
end)

exports('IsAnyMenuOpen', function()
    return focusOwner ~= nil
end)

exports('GetFocusOwner', function()
    return focusOwner
end)

-- A resource that dies while holding focus would otherwise leave the player
-- with a cursor and no keyboard. Hand focus back on its behalf.
AddEventHandler('onResourceStop', function(resource)
    if focusOwner == resource then
        focusOwner = nil
        SetNuiFocus(false, false)
        SetNuiFocusKeepInput(false)
    end
end)

--- Last-ditch unstick for the player, bound to nothing by default.
RegisterCommand('nyxunstick', function()
    focusOwner = nil
    SetNuiFocus(false, false)
    SetNuiFocusKeepInput(false)
    notify({ title = 'Nyx', description = 'NUI focus released.', type = 'info' })
end, false)

-- ---------------------------------------------------------------------------
-- Player data passthrough
-- ---------------------------------------------------------------------------

exports('GetPlayerData', function() return Bridge.GetPlayerData() end)
exports('GetFramework', function() return Bridge.GetFramework() end)
exports('IsPlayerLoaded', function() return Bridge.IsLoaded() end)
exports('RefreshPlayerData', function() return Bridge.Refresh() end)

-- ---------------------------------------------------------------------------
-- Accent
-- ---------------------------------------------------------------------------

local accentId = Nyx.DefaultAccent
local unlocked = {}

exports('GetAccent', function() return Util.Copy(Nyx.GetAccent(accentId)) end)
exports('GetAccentRgb', function() return Util.HexToRgb(Nyx.GetAccent(accentId).hex) end)

exports('GetAccents', function()
    local out = {}
    for i, preset in ipairs(Nyx.Accents) do
        out[i] = {
            id = preset.id,
            label = preset.label,
            hex = preset.hex,
            locked = preset.locked and not (Nyx.Config.unlockAllAccents or unlocked[preset.id]) or false
        }
    end
    return out
end)

--- Apply an accent and ask the server to remember it. Returns false when the
--- id is unknown or still locked for this player.
local function setAccent(id)
    local preset = nil
    for _, p in ipairs(Nyx.Accents) do
        if p.id == id then preset = p break end
    end
    if not preset then return false end

    if preset.locked and not Nyx.Config.unlockAllAccents and not unlocked[preset.id] then
        notify({ title = 'Locked', description = ('%s is not unlocked for you.'):format(preset.label), type = 'warn' })
        return false
    end

    accentId = preset.id
    TriggerServerEvent('nyx:lib:setAccent', preset.id)

    -- nyx_lib's own frame, then every other Nyx resource's.
    SendNUIMessage({ action = 'nyx:theme', accent = preset.hex })
    TriggerEvent('nyx:accentChanged', preset.id, preset.hex)
    return true
end

exports('SetAccent', setAccent)

-- The server answers with the stored accent plus whatever this player has ACE
-- access to, once per session.
RegisterNetEvent('nyx:lib:accentState', function(id, unlockedList)
    unlocked = {}
    for _, unlockedId in ipairs(unlockedList or {}) do unlocked[unlockedId] = true end

    local preset = Nyx.GetAccent(id)
    accentId = preset.id
    SendNUIMessage({ action = 'nyx:theme', accent = preset.hex })
    TriggerEvent('nyx:accentChanged', preset.id, preset.hex)
end)

-- ---------------------------------------------------------------------------
-- Sound
-- ---------------------------------------------------------------------------
-- Stock GTA front-end sound sets, so the suite ships no audio of its own.

local SOUNDS = {
    hover   = { 'NAV_UP_DOWN', 'HUD_FRONTEND_DEFAULT_SOUNDSET' },
    select  = { 'SELECT', 'HUD_FRONTEND_DEFAULT_SOUNDSET' },
    back    = { 'BACK', 'HUD_FRONTEND_DEFAULT_SOUNDSET' },
    open    = { 'Enter_1st', 'GTAO_Script_Doors_Faded_Screen_Sounds' },
    error   = { 'ERROR', 'HUD_FRONTEND_DEFAULT_SOUNDSET' },
    buy     = { 'PURCHASE', 'HUD_LIQUOR_STORE_SOUNDSET' },
    pickup  = { 'PICK_UP', 'HUD_FRONTEND_CUSTOM_SOUNDSET' }
}

exports('PlaySound', function(key)
    local sound = SOUNDS[key]
    if not sound then return false end
    PlaySoundFrontend(-1, sound[1], sound[2], true)
    return true
end)
