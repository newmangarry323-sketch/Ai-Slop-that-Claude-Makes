--[[ ==========================================================================
     nyx_inventory — configuration
     --------------------------------------------------------------------------
     Read this before you build a server around it:

     This is a complete, working, server-authoritative slot inventory. It is not
     a drop-in replacement for ox_inventory or qb-inventory, and it does not try
     to be. It has no shops, no crafting, no clothing integration, no metadata
     editor and no migration path from another inventory's database tables. What
     it does have is items, slots, weight, stacking, ground drops, stashes,
     player-to-player transfers, weapon items and persistence — all validated on
     the server.

     If you already run ox_inventory, keep it. If you want an inventory that
     matches the rest of this UI and you are starting fresh, this is that.
     ========================================================================== ]]

NyxInv = {}

NyxInv.Command = 'inventory'
NyxInv.Key = 'F2'

--- Slots and weight. Weight is in grams to avoid floating point drift; the page
--- divides by 1000 for display.
NyxInv.Slots = 40
NyxInv.MaxWeight = 120000

--- How close another player has to be to receive an item.
NyxInv.GiveDistance = 3.0

--- How close a ground drop has to be to open it.
NyxInv.DropDistance = 2.0

--- Ground drops vanish after this many seconds. 0 keeps them until restart.
NyxInv.DropLifetime = 900

--- Dropped items get a small visible marker. Set false for an invisible drop.
NyxInv.DropMarker = true

--- Items. Every key here is an item name; anything not listed cannot exist.
---
---   label    what the slot shows
---   weight   grams per unit
---   stack    true to merge into one slot, false for one slot per unit
---   image    'img/name.png', else initials on a generated tile
---   weapon   giving this item hands over a GTA weapon
---   ammo     rounds handed over with a weapon item
---   use      'heal' | 'armour' | 'none', or leave nil for an inert item
---   value    amount the use handler applies
NyxInv.Items = {
    money        = { label = 'Money',        weight = 0,    stack = true },

    -- Weapons. Using one equips it; dropping it takes it away again.
    carbine      = { label = 'Carbine',      weight = 3500, stack = false, weapon = 'WEAPON_CARBINERIFLE', ammo = 250 },
    speccarbine  = { label = 'Special Carbine', weight = 3400, stack = false, weapon = 'WEAPON_SPECIALCARBINE', ammo = 250 },
    smg          = { label = 'SMG',          weight = 2600, stack = false, weapon = 'WEAPON_SMG', ammo = 200 },
    pumpshotgun  = { label = 'Pump Shotgun', weight = 3300, stack = false, weapon = 'WEAPON_PUMPSHOTGUN', ammo = 60 },
    heavysniper  = { label = 'Heavy Sniper', weight = 6000, stack = false, weapon = 'WEAPON_HEAVYSNIPER', ammo = 20 },
    combatpistol = { label = 'Combat Pistol', weight = 900, stack = false, weapon = 'WEAPON_COMBATPISTOL', ammo = 120 },
    appistol     = { label = 'AP Pistol',    weight = 950,  stack = false, weapon = 'WEAPON_APPISTOL', ammo = 150 },
    stungun      = { label = 'Stun Gun',     weight = 700,  stack = false, weapon = 'WEAPON_STUNGUN', ammo = 1 },
    nightstick   = { label = 'Night Stick',  weight = 800,  stack = false, weapon = 'WEAPON_NIGHTSTICK' },
    flashlight   = { label = 'Flash Light',  weight = 300,  stack = false, weapon = 'WEAPON_FLASHLIGHT' },
    knife        = { label = 'Knife',        weight = 300,  stack = false, weapon = 'WEAPON_KNIFE' },

    -- Consumables.
    medkit       = { label = 'Medic Kit',    weight = 1200, stack = true, use = 'heal',   value = 100 },
    bandage      = { label = 'Bandage',      weight = 200,  stack = true, use = 'heal',   value = 30 },
    armour       = { label = 'Body Armour',  weight = 4000, stack = true, use = 'armour', value = 100 },
    water        = { label = 'Water',        weight = 500,  stack = true, use = 'none' },
    burger       = { label = 'Burger',       weight = 400,  stack = true, use = 'none' },

    -- Inert.
    rarekey      = { label = 'Rare Key',     weight = 100,  stack = true },
    legendkey    = { label = 'Legend Key',   weight = 100,  stack = true },
    phone        = { label = 'Phone',        weight = 400,  stack = true },
    lockpick     = { label = 'Lockpick',     weight = 250,  stack = true }
}

--- Handed to every new character the first time their inventory is created.
NyxInv.StartingItems = {
    { name = 'phone', count = 1 },
    { name = 'water', count = 2 },
    { name = 'burger', count = 1 }
}

--- Fixed stashes. Open them with:
---     exports['nyx_inventory']:OpenStash('garage_a')
--- or stand inside `coords` and press E when `prompt` is true.
NyxInv.Stashes = {
    {
        id = 'legion_locker',
        label = 'Legion Locker',
        slots = 60,
        maxWeight = 250000,
        coords = vec3(213.0, -804.0, 31.0),
        prompt = true,
        --- Leave nil for everyone, or gate it:
        ace = nil
    }
}
