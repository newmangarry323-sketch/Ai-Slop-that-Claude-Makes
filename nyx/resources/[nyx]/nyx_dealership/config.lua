--[[ ==========================================================================
     nyx_dealership — configuration
     --------------------------------------------------------------------------
     Shared, so the server can price every purchase from the same table the
     client renders. The page is only ever told a vehicle's id; the server looks
     the price up here. A client that edits its own copy of this file changes
     what it sees and nothing else.
     ========================================================================== ]]

NyxDealer = {}

--- Where the showroom is. `preview` is where the display vehicle is placed,
--- `camera` is where the camera sits, and `spawn` is where a bought or
--- test-driven vehicle is delivered.
---
--- The defaults below are the Premium Deluxe Motorsport showroom on the base
--- map. Capture your own with /nyxcoords from nyx_menu.
NyxDealer.Locations = {
    {
        id = 'pdm',
        label = 'Premium Deluxe Motorsport',
        ped = vec4(-44.7, -1098.0, 26.42, 70.0),   -- where the salesperson stands
        preview = vec4(-46.5, -1096.5, 26.42, 300.0),
        camera = vec4(-50.0, -1093.0, 27.4, 0.0),
        spawn = vec4(-25.0, -1085.0, 26.6, 240.0),
        pedModel = 's_m_y_dealer_01',
        blip = { sprite = 225, colour = 6, scale = 0.8 }
    }
}

--- Interaction distance for the showroom prompt.
NyxDealer.InteractDistance = 2.5

--- Which account pays. 'bank' | 'cash' | 'black'
NyxDealer.PayFrom = 'bank'

--- Test drives.
NyxDealer.TestDrive = {
    enabled = true,
    seconds = 60,
    --- Return the player to the showroom when the clock runs out.
    returnOnEnd = true
}

--- Respray in the showroom. Prices are per application, 0 for free.
NyxDealer.Respray = {
    enabled = true,
    price = 0,
    --- GTA paint indices. https://docs.fivem.net/docs/game-references/vehicle-colors/
    colours = {
        { label = 'Black', primary = 0, secondary = 0 },
        { label = 'Graphite', primary = 1, secondary = 1 },
        { label = 'White', primary = 111, secondary = 111 },
        { label = 'Red', primary = 27, secondary = 27 },
        { label = 'Magenta', primary = 71, secondary = 71 },
        { label = 'Blue', primary = 64, secondary = 64 },
        { label = 'Green', primary = 55, secondary = 55 },
        { label = 'Gold', primary = 90, secondary = 90 }
    }
}

--- How many cards the page pages through at a time.
NyxDealer.PerPage = 12

--- Categories, in the order the rail shows them. A category with no vehicles is
--- still listed, with a count of zero, exactly like the reference.
NyxDealer.Categories = {
    { id = 'new',        label = 'New' },
    { id = 'booster',    label = 'Booster' },
    { id = 'drift',      label = 'Drift Cars' },
    { id = 'motorcycle', label = 'Motorcycles' },
    { id = 'luxury',     label = 'Luxury' },
    { id = 'muscle',     label = 'Muscle' },
    { id = 'sliders',    label = 'Sliders' },
    { id = 'sports',     label = 'Sports' },
    { id = 'starter',    label = 'Starter' },
    { id = 'supporter',  label = 'Supporter' },
    { id = 'suv',        label = 'SUV' },
    { id = 'toys',       label = 'Toys' }
}

--- Vehicles. `model` is a spawn name; anything in the base game works out of
--- the box, and add-on vehicles work once their resource is started.
---
--- `ace` gates a vehicle behind a permission:
---     add_ace group.donator nyx.vehicle.supporter allow
NyxDealer.Vehicles = {
    -- Starter
    { id = 'blista',   model = 'blista',   label = 'Blista',    category = 'starter', price = 9000 },
    { id = 'panto',    model = 'panto',    label = 'Panto',     category = 'starter', price = 7500 },
    { id = 'issi2',    model = 'issi2',    label = 'Issi',      category = 'starter', price = 12000 },
    { id = 'futo',     model = 'futo',     label = 'Futo',      category = 'starter', price = 15000 },
    { id = 'prairie',  model = 'prairie',  label = 'Prairie',   category = 'starter', price = 11000 },

    -- Sports
    { id = 'sultan',   model = 'sultan',   label = 'Sultan',    category = 'sports', price = 42000 },
    { id = 'kuruma',   model = 'kuruma',   label = 'Kuruma',    category = 'sports', price = 68000 },
    { id = 'banshee',  model = 'banshee',  label = 'Banshee',   category = 'sports', price = 96000 },
    { id = 'comet2',   model = 'comet2',   label = 'Comet',     category = 'sports', price = 105000 },
    { id = 'elegy2',   model = 'elegy2',   label = 'Elegy RH8', category = 'sports', price = 88000 },
    { id = 'jester',   model = 'jester',   label = 'Jester',    category = 'sports', price = 145000 },

    -- Muscle
    { id = 'dominator', model = 'dominator', label = 'Dominator', category = 'muscle', price = 52000 },
    { id = 'gauntlet',  model = 'gauntlet',  label = 'Gauntlet',  category = 'muscle', price = 48000 },
    { id = 'sabregt',   model = 'sabregt',   label = 'Sabre GT',  category = 'muscle', price = 38000 },
    { id = 'ruiner',    model = 'ruiner',    label = 'Ruiner',    category = 'muscle', price = 36000 },
    { id = 'voodoo',    model = 'voodoo',    label = 'Voodoo',    category = 'muscle', price = 24000 },

    -- Drift
    { id = 'futo2',    model = 'futo',     label = 'Futo | Drift',  category = 'drift', price = 46000, drift = true },
    { id = 'elegydr',  model = 'elegy2',   label = 'Elegy | Drift', category = 'drift', price = 92000, drift = true },
    { id = 'tampa',    model = 'tampa',    label = 'Tampa',         category = 'drift', price = 58000, drift = true },

    -- Luxury
    { id = 'zion',     model = 'zion',     label = 'Zion',      category = 'luxury', price = 72000 },
    { id = 'felon',    model = 'felon',    label = 'Felon',     category = 'luxury', price = 86000 },
    { id = 'windsor',  model = 'windsor',  label = 'Windsor',   category = 'luxury', price = 210000 },
    { id = 'cognoscenti', model = 'cognoscenti', label = 'Cognoscenti', category = 'luxury', price = 175000 },

    -- SUV
    { id = 'dubsta',   model = 'dubsta',   label = 'Dubsta',    category = 'suv', price = 64000 },
    { id = 'baller',   model = 'baller',   label = 'Baller',    category = 'suv', price = 72000 },
    { id = 'granger',  model = 'granger',  label = 'Granger',   category = 'suv', price = 58000 },
    { id = 'radi',     model = 'radi',     label = 'Radius',    category = 'suv', price = 44000 },

    -- Motorcycles
    { id = 'bati',     model = 'bati',     label = 'Bati 801',  category = 'motorcycle', price = 38000 },
    { id = 'akuma',    model = 'akuma',    label = 'Akuma',     category = 'motorcycle', price = 32000 },
    { id = 'sanchez',  model = 'sanchez',  label = 'Sanchez',   category = 'motorcycle', price = 14000 },
    { id = 'faggio',   model = 'faggio',   label = 'Faggio',    category = 'motorcycle', price = 5000 },

    -- Toys
    { id = 'blazer',   model = 'blazer',   label = 'Blazer',    category = 'toys', price = 18000 },
    { id = 'bfinjection', model = 'bfinjection', label = 'Injection', category = 'toys', price = 26000 },

    -- Supporter — gated
    { id = 'adder',    model = 'adder',    label = 'Adder',     category = 'supporter', price = 1000000, ace = 'nyx.vehicle.supporter' },
    { id = 'zentorno', model = 'zentorno', label = 'Zentorno',  category = 'supporter', price = 725000,  ace = 'nyx.vehicle.supporter' },

    -- New — the showcase row
    { id = 'dubsta_new', model = 'dubsta', label = 'New | Dubsta', category = 'new', price = 1 },
    { id = 't20',        model = 't20',    label = 'T20',          category = 'new', price = 900000 }
}

--- Where bought vehicles go. Nyx stores them in its own list and fires
--- `nyx_dealership:purchased` on the server so you can hand them to whatever
--- garage system you already run. See nyx/docs/api.md.
NyxDealer.StoreOwnedVehicles = true
