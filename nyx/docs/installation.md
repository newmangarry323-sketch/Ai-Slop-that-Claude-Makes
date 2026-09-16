# Installation

## The short version

1. Copy `resources/[nyx]/` into your server's `resources/` folder.
2. Add the five `ensure` lines below to `server.cfg`.
3. Restart. Press **F1**.

```cfg
## Nyx UI suite -------------------------------------------------------------
ensure nyx_lib          # must be first
ensure nyx_hud
ensure nyx_menu
ensure nyx_dealership
ensure nyx_inventory
```

No database, no dependencies, no build step.

## Why the order matters

`nyx_lib` is loaded into the other four resources at compile time:

```lua
shared_scripts {
    '@nyx_lib/shared/config.lua',
    '@nyx_lib/shared/util.lua',
    'config.lua'
}
```

The `@resource/file.lua` syntax compiles another resource's file into *this*
resource's Lua state. Each resource runs in its own state with its own globals,
so this is how `Nyx.Config`, `Nyx.Util` and `Nyx.Notify` exist everywhere
without an export call for every read.

It also means `nyx_lib` must be *startable* before the others start. If it is
not, the others fail at load with a nil index on `Nyx`, and the console will say
so in red. `dependencies { 'nyx_lib' }` in each manifest makes FiveM enforce it,
but keeping the `ensure` lines in order makes the intent obvious to the next
person reading your `server.cfg`.

## Verifying it worked

The server console prints the detected framework twice, once per side:

```
[nyx] server framework: standalone
[nyx] framework: standalone
```

If that says `standalone` and you *are* running ESX or QBCore, see
[Troubleshooting → wrong framework detected](troubleshooting.md#nyx-says-standalone-but-i-run-esx-or-qbcore).

Then in game:

| Check | Expected |
| --- | --- |
| Press **F1** | The menu opens, magenta accents, Locations tab |
| Press **F2** | The inventory opens with your starting items |
| `/nyxcoords` | Prints a config line to F8 and chat |
| `/menu` | Same as F1 — works even if the key is rebound |
| Server console: `nyx` | Framework, player count, accent count |

## Taking only part of it

The four feature resources depend on `nyx_lib` and on nothing else, including
each other. Any subset works:

| You want | Ensure |
| --- | --- |
| Just the menu | `nyx_lib`, `nyx_menu` |
| Just the HUD | `nyx_lib`, `nyx_hud` |
| Menu + HUD (the Misc tab configures the HUD) | `nyx_lib`, `nyx_hud`, `nyx_menu` |
| Everything but the inventory | all except `nyx_inventory` |

`nyx_menu` detects whether `nyx_hud` is running. Without it, the Miscellaneous
tab renders a panel saying so instead of erroring — it checks `GetResourceState`
and wraps the export call in `pcall`, so a stopped or crashed `nyx_hud` cannot
take the menu down with it.

## Coexisting with what you already run

### You already have an inventory

Do not start `nyx_inventory`. Delete its `ensure` line. Nothing else in the
suite touches inventory.

### You already have a HUD

Two HUDs will draw on top of each other. Either stop yours, or stop `nyx_hud` —
the Miscellaneous tab disables itself cleanly when it is absent.

If you want to keep yours but still hide the *game's* native HUD elements, that
job lives in `nyx_hud/config.lua`:

```lua
NyxHud.HideNativeComponents = { 1, 2, 3, 4, 6, 7, 9, 13, 20 }
```

Set it to `{}` to hide nothing.

### You already have a dealership or garage

`nyx_dealership` stores what it sells in its own list, but it also fires a
server event on every purchase so you can hand the vehicle to whatever garage
system you already run:

```lua
AddEventHandler('nyx_dealership:purchased', function(src, data)
    -- data = { identifier, vehicleId, model, label, plate, price }
    MySQL.insert('INSERT INTO owned_vehicles (citizenid, vehicle, plate) VALUES (?, ?, ?)',
        { data.identifier, data.model, data.plate })
end)
```

Set `NyxDealer.StoreOwnedVehicles = false` in its config to stop Nyx keeping its
own copy once your garage owns the record.

### Key conflicts

`F1` and `F2` are Nyx's defaults, not claims. `RegisterKeyMapping` only ever
sets a default — a player who has already bound that key keeps their binding,
and anyone can rebind under **Settings → Key Bindings → FiveM**.

To change the default, edit `NyxMenu.Key` / `NyxInv.Key`, or set either to `''`
to register no key at all and rely on `/menu` and `/inventory`.

> Changing a keybind default does not move a binding a player has already
> accepted. FiveM stores their choice client-side; they rebind it themselves, or
> they delete the binding to pick up your new default.

## Where data is written

```
nyx_lib/data/
├── nyx_lib.accents.json        chosen accent per identifier
├── nyx_lib.wallets.json        standalone-only wallets
├── nyx_hud.prefs.json          HUD preferences per identifier
├── nyx_menu.stats.json         kills and deaths
├── nyx_dealership.owned.json   purchased vehicles
├── nyx_inventory.inv.json      player inventories
└── nyx_inventory.stash.json    stash contents
```

Buckets are namespaced by the calling resource automatically, so two resources
cannot collide on a bucket name by accident.

Files are written on a timer (`Nyx.Config.autosaveInterval`, default 120s), on
player drop, and on resource stop — which also covers a clean server shutdown,
since that stops every resource in turn. A hard crash loses at most the
autosave interval.

Back up that folder. It is the only copy.

If a file is ever truncated by a kill mid-write, the store keeps the damaged
file as `*.json.broken`, logs loudly, and starts that bucket empty rather than
silently continuing with nothing.

## Updating

Preserve `nyx_lib/data/` and your five `config.lua` files. Everything else can
be replaced wholesale. Keeping your edits in `config.lua` — rather than in the
files around it — is what makes that true, and is the reason the configs are
separate files in the first place.
