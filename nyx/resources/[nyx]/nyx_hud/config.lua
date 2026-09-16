--[[ ==========================================================================
     nyx_hud — configuration
     --------------------------------------------------------------------------
     Defaults only. Anything a player changes through the menu is stored per
     identifier on the server and layered over the top of these at spawn, so
     editing this file changes the experience for new players without stamping
     over the preferences existing ones already set.
     ========================================================================== ]]

NyxHud = {}

--- Refresh rates in milliseconds. The vehicle loop is the expensive one because
--- it runs natives every tick it is awake; it sleeps entirely on foot.
NyxHud.Tick = {
    status = 250,     -- health, armour, voice
    money = 500,      -- wallet and job text
    vehicle = 100     -- speed, fuel, vehicle health
}

--- Bottom-right watermark. Set `text` to nil to leave it off by default.
NyxHud.Watermark = {
    text = 'DISCORD.GG/YOURSERVER',
    --- Show the live player count next to it.
    showPlayerCount = true
}

--- Speedometer units: 'mph' or 'kmh'.
NyxHud.SpeedUnits = 'mph'

--- GTA's own HUD components to hide while nyx_hud is running. Numbers are from
--- https://docs.fivem.net/docs/game-references/hud-colors/ — the component list
--- lives alongside it in the game references section.
--- 1 WANTED_STARS, 2 WEAPON_ICON, 3 CASH, 4 MP_CASH, 6 VEHICLE_NAME,
--- 7 AREA_NAME, 9 STREET_NAME, 13 CASH_CHANGE, 20 WEAPON_STATS.
NyxHud.HideNativeComponents = { 1, 2, 3, 4, 6, 7, 9, 13, 20 }

--- Starting point for every player. The menu overrides these per person.
NyxHud.Defaults = {
    --- Status cluster: 1 = pill row, 2 = separate rings.
    style = 1,

    --- Money panel: 3 = stacked bars, 4 = pixel text.
    moneyStyle = 4,

    toggles = {
        health = true,
        announcements = true,
        fps = true,
        speedometer = true,
        watermark = true,
        money = true,
        crosshair = false,
        chat = true,
        map = true
    },

    crosshair = {
        style = 'cross',      -- cross | tshape | circle | dot
        size = 8,
        thickness = 2,
        gap = 5,
        colour = '#ff2bd6',
        opacity = 90,
        outline = true,
        dot = false,
        dynamic = false,        -- widen the gap while moving or shooting
        hideInVehicle = true,
        onlyWhenArmed = false
    }
}

--- Clamps applied to whatever comes back from the client. A preference arrives
--- over NUI, which means it arrives from somewhere a player can reach, so it
--- gets bounded before it is stored or drawn.
NyxHud.Limits = {
    size = { 0, 30 },
    thickness = { 1, 10 },
    gap = { 0, 30 },
    opacity = { 10, 100 }
}

NyxHud.CrosshairStyles = { cross = true, tshape = true, circle = true, dot = true }
