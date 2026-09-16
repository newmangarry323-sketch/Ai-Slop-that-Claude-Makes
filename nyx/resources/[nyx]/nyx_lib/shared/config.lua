--[[ ==========================================================================
     Nyx — shared configuration
     --------------------------------------------------------------------------
     Loaded into every Nyx resource's Lua state through the manifest line

         shared_script '@nyx_lib/shared/config.lua'

     which is how one resource's file gets compiled into another's state. That
     makes `Nyx.Config` and `Nyx.Accents` readable everywhere without an export
     round-trip. Runtime data still goes through exports — see client/api.lua.
     ========================================================================== ]]

Nyx = Nyx or {}

Nyx.Config = {
    -- 'auto' walks the detection order in client/bridge.lua and stops at the
    -- first framework that is actually started. Pin it if auto guesses wrong:
    -- 'esx' | 'qb' | 'qbx' | 'standalone'
    framework = 'auto',

    -- Standalone servers have no bank, so Nyx keeps a wallet of its own in
    -- nyx_lib/data/wallets.json. Ignored entirely when a framework is found.
    standaloneStartingCash = 25000,
    standaloneStartingBank = 100000,

    -- Seconds between autosaves of the JSON stores. Every store also flushes
    -- on resource stop and on player drop, so this is a crash cushion, not the
    -- primary save path.
    autosaveInterval = 120,

    -- Default notification lifetime in milliseconds.
    notifyDuration = 4000,

    -- Set true to hand every accent colour to every player. Left false, the
    -- entries flagged `locked` below need the ACE permission `nyx.accent.<id>`,
    -- granted in server.cfg like:
    --     add_ace group.admin nyx.accent.violet allow
    unlockAllAccents = false,

    -- Written into the top bar of every menu. Point `logo` at any image the
    -- NUI can reach: an https:// URL, or a file inside a resource addressed as
    -- nui://<resource>/<path>. Leave it nil to fall back to `name` as text.
    brand = {
        name = 'NYX',
        logo = nil
    },

    -- Debug prints from the bridge and the stores.
    debug = false
}

--[[ Accent presets. `hex` drives --nyx-accent-rgb in theme.css; everything
     tinted in the entire suite derives from it, so adding a colour here is the
     whole job of adding a colour. ]]
Nyx.Accents = {
    { id = 'magenta', label = 'Magenta', hex = '#ff2bd6' },
    { id = 'crimson', label = 'Crimson', hex = '#ff2b4d', locked = true },
    { id = 'emerald', label = 'Emerald', hex = '#12e05a' },
    { id = 'amber',   label = 'Amber',   hex = '#ffc41f' },
    { id = 'azure',   label = 'Azure',   hex = '#2bb4ff' },
    { id = 'lime',    label = 'Lime',    hex = '#8ce81d' },
    { id = 'orange',  label = 'Orange',  hex = '#ff7a1a' },
    { id = 'cobalt',  label = 'Cobalt',  hex = '#2b4dff' },
    { id = 'violet',  label = 'Violet',  hex = '#8a5cff', locked = true }
}

Nyx.DefaultAccent = 'magenta'

--- Look an accent up by id, falling back to the default rather than nil.
--- @param id string|nil
--- @return table accent
function Nyx.GetAccent(id)
    for i = 1, #Nyx.Accents do
        if Nyx.Accents[i].id == id then return Nyx.Accents[i] end
    end
    for i = 1, #Nyx.Accents do
        if Nyx.Accents[i].id == Nyx.DefaultAccent then return Nyx.Accents[i] end
    end
    return Nyx.Accents[1]
end
