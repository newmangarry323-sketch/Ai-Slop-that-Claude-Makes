# Configuration

Five config files, one per resource. Everything you are meant to edit is in
them; everything around them is machinery.

| File | Controls |
| --- | --- |
| `nyx_lib/shared/config.lua` | Framework pin, accents, branding, autosave |
| `nyx_menu/config.lua` | Locations, weapons, keybind, occupancy, stats |
| `nyx_hud/config.lua` | HUD defaults, watermark, speed units, native HUD |
| `nyx_dealership/config.lua` | Showrooms, vehicles, prices, test drive, respray |
| `nyx_inventory/config.lua` | Items, slots, weight, stashes, drops |

## nyx_lib

### Pinning the framework

Detection runs in this order and stops at the first started resource:
`qbx_core` → `es_extended` → `qb-core` → standalone.

Qbox is checked first deliberately: it ships a `qb-core` compatibility shim, so
checking `qb-core` first would misidentify every Qbox server as legacy QBCore.

If it still guesses wrong, pin it:

```lua
Nyx.Config.framework = 'esx'   -- 'auto' | 'esx' | 'qb' | 'qbx' | 'standalone'
```

### Branding

```lua
Nyx.Config.brand = {
    name = 'NYX',
    logo = nil   -- 'https://…', or 'nui://your_resource/web/logo.png'
}
```

With `logo` nil, the menu renders `name` as gradient text. A `nui://` path needs
that file declared in the owning resource's `files {}` block, or NUI cannot
reach it.

### Accents

```lua
Nyx.Accents = {
    { id = 'magenta', label = 'Magenta', hex = '#ff2bd6' },
    { id = 'violet',  label = 'Violet',  hex = '#8a5cff', locked = true }
}
```

Add, remove and reorder freely — the picker renders whatever is in the list.
`locked` entries render greyed with a padlock until the player has the matching
ACE:

```cfg
add_ace group.donator nyx.accent.violet allow
```

The lock is checked on the client *and* re-checked on the server before the
choice is stored. The client check exists so the UI can grey the swatch; the
server check is the one that matters.

`Nyx.Config.unlockAllAccents = true` opens everything to everyone.

### Swapping the store for MySQL

The flat-file store is a deliberate default so the suite runs on a server with
no database. If you already run [oxmysql](https://github.com/overextended/oxmysql),
replace the two functions at the bottom of `nyx_lib/server/store.lua`:

```lua
local function load(bucket)
    if cache[bucket] then return cache[bucket] end
    local row = MySQL.single.await('SELECT data FROM nyx_store WHERE bucket = ?', { bucket })
    cache[bucket] = row and json.decode(row.data) or {}
    return cache[bucket]
end

function Store.Flush(bucket)
    bucket = safeBucket(bucket)
    if not dirty[bucket] or not cache[bucket] then return false end
    MySQL.prepare.await(
        'INSERT INTO nyx_store (bucket, data) VALUES (?, ?) ON DUPLICATE KEY UPDATE data = VALUES(data)',
        { bucket, json.encode(cache[bucket]) }
    )
    dirty[bucket] = nil
    return true
end
```

```sql
CREATE TABLE nyx_store (
    bucket VARCHAR(96) NOT NULL PRIMARY KEY,
    data   LONGTEXT NOT NULL
);
```

Add `'@oxmysql/lib/MySQL.lua'` to `nyx_lib`'s `server_scripts`, above the other
entries. Nothing else in the suite changes, because nothing else in the suite
knows how persistence works — that is the entire point of routing every write
through `DbGet`/`DbSet`.

## nyx_menu

### Adding a location

Stand exactly where you want players to land, facing the right way, and run
`/nyxcoords`. It prints a complete config line to F8 and to chat:

```lua
{ id = 'newspot', label = 'New Spot', coords = vec4(215.04, -810.12, 30.73, 340.0) },
```

Paste it into a group's `spots` list. Ids must be unique across the whole file —
they are the only thing that crosses the wire, and the server resolves them
against this table.

Per-spot options:

```lua
{
    id = 'hq', label = 'Police HQ',
    coords = vec4(441.0, -982.0, 30.69, 180.0),
    image = 'img/hq.jpg',        -- else a generated tile with the initials
    ace = 'nyx.location.police', -- ACE gate
    job = 'police',              -- framework job gate
    heal = true,                 -- full health and armour on arrival
    clearWanted = true,
    snapToGround = true          -- probe for ground Z; for rooftops and hills
}
```

`ace` also works on a group and on a section; the narrowest one wins.

### Adding a weapon

```lua
{ id = 'carbine', label = 'Carbine', weapon = 'WEAPON_CARBINERIFLE', ammo = 250 }
```

`weapon` is a name from the
[weapon models list](https://docs.fivem.net/docs/game-references/weapon-models/);
Nyx hashes it at runtime. A kit is a list instead:

```lua
{
    id = 'kit_patrol', label = 'Patrol',
    kit = {
        { weapon = 'WEAPON_COMBATPISTOL', ammo = 120 },
        { weapon = 'WEAPON_STUNGUN', ammo = 1 }
    }
}
```

`NyxMenu.WeaponsTab.clearBeforeKit` wipes the loadout first, so a kit is a kit
rather than an addition.

To lock the whole tab down: `NyxMenu.WeaponsTab.ace = 'nyx.weapons'`. A player
with access to nothing in it does not see the tab at all — an empty tab is worse
than no tab.

After granting an ACE at runtime, run `nyxrefresh <serverId>` to re-push that
player's permissions without a reconnect.

### Occupancy counters

The badge on each card. Clients work out locally which spot they are at and tell
the server **only when that answer changes**, so standing still costs nothing
and an idle server broadcasts nothing.

```lua
NyxMenu.Occupancy = { enabled = true, radius = 60.0, checkInterval = 3000 }
```

`checkInterval` is how often a client checks its own position, not a network
interval. Broadcasts are coalesced to at most one per second.

## nyx_hud

`NyxHud.Defaults` is the starting point for **new** players only. Anything a
player changes through the menu is stored against their identifier and layered
on top, so editing defaults never stamps over existing preferences. `/hudreset`
puts one player back to the defaults.

```lua
NyxHud.SpeedUnits = 'mph'       -- or 'kmh'
NyxHud.Watermark = { text = 'DISCORD.GG/YOURSERVER', showPlayerCount = true }
NyxHud.Tick = { status = 250, money = 500, vehicle = 100 }
```

The vehicle loop sleeps entirely on foot, so its rate only costs while driving.

### Making the chat toggle do something

`DisplayRadar()` is a native, so the map toggle just works. Chat is not: there
is no standard way to hide another resource's chat. Nyx announces the intent and
leaves the wiring to you.

In your chat resource's client script:

```lua
AddEventHandler('nyx_hud:chatToggle', function(visible)
    SendNUIMessage({ type = 'ON_SCREEN_STATE_CHANGE', shouldHide = not visible })
end)
```

The exact message depends on which chat you run; the event fires either way.

### Fuel

`GetVehicleFuelLevel` reads the game's own tank. Most fuel resources write to
the same native, so it usually just works. If yours does not, replace that one
line in `nyx_hud/client/main.lua` with your resource's export.

## nyx_dealership

```lua
NyxDealer.PayFrom = 'bank'    -- 'bank' | 'cash' | 'black'
NyxDealer.PerPage = 12
NyxDealer.TestDrive = { enabled = true, seconds = 60, returnOnEnd = true }
```

A vehicle:

```lua
{ id = 'adder', model = 'adder', label = 'Adder', category = 'supporter',
  price = 1000000, ace = 'nyx.vehicle.supporter' }
```

`id` is Nyx's key and must be unique — note that two entries can share a `model`
(a stock and a drift-tuned Futo) as long as their ids differ. `model` is a spawn
name; base-game vehicles work immediately, add-ons work once their resource is
started. Prices live here, on the server, and are never sent to the client for
it to agree with.

A category with no vehicles still lists, at zero, and is not clickable.

Showroom placement takes four points — `ped` (where the prompt appears),
`preview` (where the display car stands), `camera` (where you look from) and
`spawn` (where a bought or test-driven car is delivered). Capture all four with
`/nyxcoords`.

## nyx_inventory

```lua
NyxInv.Slots = 40
NyxInv.MaxWeight = 120000     -- grams; integers avoid float drift
NyxInv.GiveDistance = 3.0
NyxInv.DropLifetime = 900     -- seconds; 0 keeps drops until restart
```

An item:

```lua
medkit = { label = 'Medic Kit', weight = 1200, stack = true, use = 'heal', value = 100 },
carbine = { label = 'Carbine', weight = 3500, stack = false,
            weapon = 'WEAPON_CARBINERIFLE', ammo = 250 },
```

The table key is the item name. Anything not listed here cannot exist — items
loaded from disk whose definition has since been deleted are discarded at load
rather than resurrected as a broken slot.

`use` is `'heal'`, `'armour'` or `'none'`. To add your own effect, extend the
`nyx_inventory:consume` handler in `client/main.lua` — the server has already
removed the item by the time it fires.

A stash:

```lua
{ id = 'legion_locker', label = 'Legion Locker', slots = 60, maxWeight = 250000,
  coords = vec3(213.0, -804.0, 31.0), prompt = true, ace = nil }
```

`prompt = true` puts a press-E prompt at `coords`. Without coords, open it from
your own script: `exports['nyx_inventory']:OpenStash('legion_locker')`.

## ACE permissions

All of them, in one place:

```cfg
add_ace group.admin nyx.admin allow                 # nyx, nyxrefresh
add_ace group.admin nyx.announce allow              # /announce
add_ace group.admin nyx.weapons.sniper allow        # the Marksman section
add_ace group.donator nyx.vehicle.supporter allow   # gated vehicles
add_ace group.donator nyx.accent.violet allow       # locked accent
```

ACE objects are yours to name — the strings above are whatever you put in the
config. The mechanism is FiveM's, documented under
[server commands](https://docs.fivem.net/docs/server-manual/server-commands/).
