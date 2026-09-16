--[[ ==========================================================================
     nyx_menu — configuration
     --------------------------------------------------------------------------
     This file is a shared_script: the client renders from it and the server
     validates against it, so the two can never disagree about what exists.
     That is the whole security model of this resource. The client sends an id;
     the server looks that id up here and refuses anything it does not find.
     Coordinates are never sent over the wire.

     Shape, from the outside in:

         Locations -> sections (the left rail)
                   -> groups   (the labelled strips)
                   -> spots    (the cards)

     The reference layout used sections like "Airport" and "Sky Ramps" with
     groups called "Section One" and "Section Two". The shape below is the same;
     only the names differ. Rename freely.
     ========================================================================== ]]

NyxMenu = {}

-- ---------------------------------------------------------------------------
-- Opening the menu
-- ---------------------------------------------------------------------------

--- Chat command. Always available even if the key is rebound or unbound.
NyxMenu.Command = 'menu'

--- Default key. Players rebind it in Settings > Key Bindings > FiveM, and
--- RegisterKeyMapping only ever sets the default — it cannot steal a key the
--- player has already assigned to something else.
--- Use 'F1'..'F12', 'GRAVE', a letter, or '' to register no default at all.
NyxMenu.Key = 'F1'

--- Play a stock GTA front-end click on open/close and on card selection.
NyxMenu.Sounds = true

-- ---------------------------------------------------------------------------
-- Occupancy counters
-- ---------------------------------------------------------------------------
-- The little "0 [person]" badge on each card. Clients work out which spot they
-- are standing at and tell the server only when the answer changes, so an idle
-- server costs no traffic at all.

NyxMenu.Occupancy = {
    enabled = true,
    radius = 60.0,      -- metres from the spot to count as "at" it
    checkInterval = 3000 -- ms between local checks; not a network interval
}

-- ---------------------------------------------------------------------------
-- Teleporting
-- ---------------------------------------------------------------------------

NyxMenu.Teleport = {
    fadeMs = 350,          -- screen fade either side of the jump
    healOnArrive = false,  -- top up health/armour on landing (practice servers)
    clearWantedOnArrive = false,
    keepVehicle = true,    -- take the car with you when driving

    --- Blocked while any of these is true. Stops the menu being used to escape
    --- a fight, a chase or a ragdoll.
    blockWhenDead = true,
    blockInCombat = false, -- needs a combat flag from your own scripts to matter
    cooldownMs = 3000      -- also enforced server-side; the client value is UX
}

-- ---------------------------------------------------------------------------
-- Weapons tab
-- ---------------------------------------------------------------------------

NyxMenu.WeaponsTab = {
    enabled = true,

    --- Leave nil for everyone. Set to an ACE object to restrict the whole tab:
    ---     add_ace group.admin nyx.weapons allow
    ace = nil,

    --- Give the weapon with a full clip on top of the configured ammo.
    equipOnGive = true,

    --- Wipe the player's current loadout before handing over a kit.
    clearBeforeKit = true
}

-- ---------------------------------------------------------------------------
-- Locations
-- ---------------------------------------------------------------------------
-- Every `coords` below is a vector4: x, y, z, heading.
--
-- These are real spots on the base map, but they are a starting point, not a
-- survey. Stand exactly where you want to land and run /nyxcoords in game —
-- it prints a ready-to-paste config line with your current position and
-- heading, to the console and to chat.

NyxMenu.Locations = {
    {
        id = 'city',
        label = 'City',
        groups = {
            {
                label = 'Downtown',
                spots = {
                    { id = 'legion',   label = 'Legion Square',  coords = vec4(215.0, -810.0, 30.73, 340.0) },
                    { id = 'pillbox',  label = 'Pillbox Hill',   coords = vec4(298.5, -584.5, 43.26, 70.0) },
                    { id = 'mazeroof', label = 'Maze Bank Roof', coords = vec4(-75.0, -818.6, 326.18, 180.0) },
                    { id = 'vinewood', label = 'Vinewood Blvd',  coords = vec4(297.0, 180.0, 104.4, 160.0) },
                    { id = 'mirror',   label = 'Mirror Park',    coords = vec4(1070.0, -720.0, 57.5, 270.0) }
                }
            },
            {
                label = 'Outskirts',
                spots = {
                    { id = 'observatory', label = 'Observatory', coords = vec4(-438.0, 1076.0, 352.4, 180.0) },
                    { id = 'burton',      label = 'LS Customs',  coords = vec4(-337.0, -136.0, 39.0, 70.0) },
                    { id = 'vespucci',    label = 'Vespucci',    coords = vec4(-1183.0, -1508.0, 4.4, 300.0) },
                    { id = 'delperro',    label = 'Del Perro Pier', coords = vec4(-1850.0, -1231.0, 13.02, 130.0) }
                }
            }
        }
    },
    {
        id = 'airport',
        label = 'Airport',
        groups = {
            {
                label = 'Terminal',
                spots = {
                    { id = 'lsia_front',  label = 'LSIA Front',   coords = vec4(-1037.0, -2737.0, 20.17, 330.0) },
                    { id = 'lsia_hangar', label = 'LSIA Hangar',  coords = vec4(-1141.0, -2876.0, 13.95, 330.0) },
                    { id = 'lsia_runway', label = 'LSIA Runway',  coords = vec4(-1336.0, -3044.0, 13.94, 330.0) },
                    { id = 'lsia_tower',  label = 'Control Tower', coords = vec4(-1000.0, -2640.0, 20.17, 240.0) }
                }
            },
            {
                label = 'Military',
                spots = {
                    { id = 'zancudo_gate', label = 'Zancudo Gate', coords = vec4(-2360.0, 3245.0, 32.81, 60.0) },
                    { id = 'zancudo_pad',  label = 'Zancudo Pad',  coords = vec4(-2360.0, 3020.0, 32.81, 150.0) },
                    { id = 'sandy_strip',  label = 'Sandy Airfield', coords = vec4(1700.0, 3280.0, 41.13, 105.0) }
                }
            }
        }
    },
    {
        id = 'coast',
        label = 'Coast & Docks',
        groups = {
            {
                label = 'Docks',
                spots = {
                    { id = 'elysian',   label = 'Elysian Island', coords = vec4(270.0, -3105.0, 5.79, 270.0) },
                    { id = 'cranes',    label = 'Dock Cranes',    coords = vec4(-220.0, -2540.0, 6.0, 55.0) },
                    { id = 'terminal',  label = 'Terminal Yard',  coords = vec4(920.0, -2960.0, 5.9, 180.0) }
                }
            },
            {
                label = 'Beaches',
                spots = {
                    { id = 'chumash',  label = 'Chumash',    coords = vec4(-3200.0, 1050.0, 20.0, 180.0) },
                    { id = 'paleto',   label = 'Paleto Bay', coords = vec4(-140.0, 6370.0, 31.48, 220.0) },
                    { id = 'catfish',  label = 'Catfish View', coords = vec4(3880.0, 4470.0, 5.5, 20.0) }
                }
            }
        }
    },
    {
        id = 'high',
        label = 'High Ground',
        groups = {
            {
                label = 'Peaks',
                spots = {
                    { id = 'chiliad',  label = 'Mt. Chiliad',   coords = vec4(450.0, 5566.0, 806.18, 180.0) },
                    { id = 'gordo',    label = 'Mt. Gordo',     coords = vec4(2860.0, 5900.0, 370.0, 200.0) },
                    { id = 'sign',     label = 'Vinewood Sign', coords = vec4(711.0, 1198.0, 348.0, 180.0) }
                }
            },
            {
                label = 'Rooftops',
                spots = {
                    { id = 'arcadius', label = 'Arcadius Roof', coords = vec4(-141.0, -620.0, 168.82, 250.0) },
                    { id = 'ferris',   label = 'Ferris Wheel',  coords = vec4(-1670.0, -1125.0, 50.0, 320.0) }
                }
            }
        }
    },
    {
        id = 'country',
        label = 'Countryside',
        groups = {
            {
                label = 'Blaine County',
                spots = {
                    { id = 'sandy',     label = 'Sandy Shores', coords = vec4(1961.0, 3740.0, 32.34, 210.0) },
                    { id = 'grapeseed', label = 'Grapeseed',    coords = vec4(1698.0, 4924.0, 42.06, 235.0) },
                    { id = 'alamo',     label = 'Alamo Sea',    coords = vec4(1290.0, 4320.0, 33.9, 200.0) },
                    { id = 'quarry',    label = 'Davis Quarry', coords = vec4(2950.0, 2790.0, 41.0, 60.0) }
                }
            }
        }
    }
}

-- ---------------------------------------------------------------------------
-- Weapons
-- ---------------------------------------------------------------------------
-- `weapon` is a GTA weapon name; Nyx hashes it at runtime. Full list:
-- https://docs.fivem.net/docs/game-references/weapon-models/
--
-- Set `ace` on a section, group or item to gate it. Items inherit whatever
-- their group and section set, so the narrowest rule wins.

NyxMenu.Weapons = {
    {
        id = 'sidearms',
        label = 'Sidearms',
        groups = {
            {
                label = 'Pistols',
                items = {
                    { id = 'pistol',     label = 'Pistol',        weapon = 'WEAPON_PISTOL',       ammo = 120 },
                    { id = 'combat',     label = 'Combat Pistol', weapon = 'WEAPON_COMBATPISTOL', ammo = 120 },
                    { id = 'appistol',   label = 'AP Pistol',     weapon = 'WEAPON_APPISTOL',     ammo = 150 },
                    { id = 'heavy',      label = 'Heavy Pistol',  weapon = 'WEAPON_HEAVYPISTOL',  ammo = 100 },
                    { id = 'pistol50',   label = 'Pistol .50',    weapon = 'WEAPON_PISTOL50',     ammo = 80 }
                }
            },
            {
                label = 'Less Lethal',
                items = {
                    { id = 'stungun',   label = 'Stun Gun',   weapon = 'WEAPON_STUNGUN',   ammo = 1 },
                    { id = 'nightstick', label = 'Night Stick', weapon = 'WEAPON_NIGHTSTICK' },
                    { id = 'flashlight', label = 'Flashlight', weapon = 'WEAPON_FLASHLIGHT' }
                }
            }
        }
    },
    {
        id = 'primaries',
        label = 'Primaries',
        groups = {
            {
                label = 'Rifles',
                items = {
                    { id = 'carbine',    label = 'Carbine',         weapon = 'WEAPON_CARBINERIFLE',   ammo = 250 },
                    { id = 'speccarbine', label = 'Special Carbine', weapon = 'WEAPON_SPECIALCARBINE', ammo = 250 },
                    { id = 'assault',    label = 'Assault Rifle',   weapon = 'WEAPON_ASSAULTRIFLE',   ammo = 250 },
                    { id = 'bullpup',    label = 'Bullpup Rifle',   weapon = 'WEAPON_BULLPUPRIFLE',   ammo = 250 }
                }
            },
            {
                label = 'SMGs',
                items = {
                    { id = 'smg',      label = 'SMG',       weapon = 'WEAPON_SMG',      ammo = 200 },
                    { id = 'microsmg', label = 'Micro SMG', weapon = 'WEAPON_MICROSMG', ammo = 200 },
                    { id = 'mp',       label = 'MP',        weapon = 'WEAPON_ASSAULTSMG', ammo = 200 }
                }
            },
            {
                label = 'Shotguns',
                items = {
                    { id = 'pump',    label = 'Pump Shotgun',    weapon = 'WEAPON_PUMPSHOTGUN',    ammo = 60 },
                    { id = 'assaultsg', label = 'Assault Shotgun', weapon = 'WEAPON_ASSAULTSHOTGUN', ammo = 80 },
                    { id = 'sawnoff', label = 'Sawn-Off',        weapon = 'WEAPON_SAWNOFFSHOTGUN', ammo = 40 }
                }
            }
        }
    },
    {
        id = 'marksman',
        label = 'Marksman',
        -- Everything in this section needs: add_ace group.admin nyx.weapons.sniper allow
        ace = 'nyx.weapons.sniper',
        groups = {
            {
                label = 'Snipers',
                items = {
                    { id = 'sniper',      label = 'Sniper Rifle', weapon = 'WEAPON_SNIPERRIFLE', ammo = 30 },
                    { id = 'heavysniper', label = 'Heavy Sniper', weapon = 'WEAPON_HEAVYSNIPER', ammo = 20 },
                    { id = 'marksman',    label = 'Marksman',     weapon = 'WEAPON_MARKSMANRIFLE', ammo = 60 }
                }
            }
        }
    },
    {
        id = 'melee',
        label = 'Melee',
        groups = {
            {
                label = 'Close Quarters',
                items = {
                    { id = 'knife',   label = 'Knife',   weapon = 'WEAPON_KNIFE' },
                    { id = 'bat',     label = 'Bat',     weapon = 'WEAPON_BAT' },
                    { id = 'crowbar', label = 'Crowbar', weapon = 'WEAPON_CROWBAR' },
                    { id = 'machete', label = 'Machete', weapon = 'WEAPON_MACHETE' }
                }
            }
        }
    },
    {
        id = 'kits',
        label = 'Loadouts',
        groups = {
            {
                label = 'Preset Kits',
                items = {
                    {
                        id = 'kit_patrol',
                        label = 'Patrol',
                        kit = {
                            { weapon = 'WEAPON_COMBATPISTOL', ammo = 120 },
                            { weapon = 'WEAPON_STUNGUN', ammo = 1 },
                            { weapon = 'WEAPON_NIGHTSTICK' },
                            { weapon = 'WEAPON_FLASHLIGHT' }
                        }
                    },
                    {
                        id = 'kit_assault',
                        label = 'Assault',
                        kit = {
                            { weapon = 'WEAPON_CARBINERIFLE', ammo = 250 },
                            { weapon = 'WEAPON_COMBATPISTOL', ammo = 120 },
                            { weapon = 'WEAPON_KNIFE' }
                        }
                    },
                    {
                        id = 'kit_cqc',
                        label = 'CQC',
                        kit = {
                            { weapon = 'WEAPON_ASSAULTSMG', ammo = 200 },
                            { weapon = 'WEAPON_PUMPSHOTGUN', ammo = 60 },
                            { weapon = 'WEAPON_APPISTOL', ammo = 150 }
                        }
                    }
                }
            }
        }
    }
}

-- ---------------------------------------------------------------------------
-- Kill / death tracking
-- ---------------------------------------------------------------------------
-- Feeds the K / D / ratio capsule in the top bar.

NyxMenu.Stats = {
    enabled = true,
    -- A death with no identifiable killer (fall, drown, vehicle) still counts
    -- as a death. Set false to only count deaths caused by another player.
    countEnvironmentDeaths = true,
    -- Reset everyone's stats when the server restarts.
    wipeOnRestart = false
}
