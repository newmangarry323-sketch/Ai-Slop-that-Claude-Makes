# API

Everything below is verified against the source — each name here has a matching
`exports('Name', …)` in the resource it is listed under, and
[`tools/check.py`](../tools/README.md) fails if this page and the code ever
diverge.

Side matters: a client export cannot be called from a server script or the other
way round.

---

## nyx_lib — client

```lua
exports['nyx_lib']:Notify{ title = 'Saved', description = '…', type = 'success', duration = 4000 }
```

`type` is `'success' | 'error' | 'warn' | 'info'`, or omit it for the accent-
coloured default. `duration` is clamped to 900–20000 ms. Title and description
are truncated and escaped on the way out.

Because `nyx_lib/shared/util.lua` is compiled into every Nyx resource, there is
a shorthand available anywhere in the suite:

```lua
Nyx.Notify{ title = 'Saved', type = 'success' }   -- client
Nyx.Sound('select')                               -- client
```

| Export | Returns |
| --- | --- |
| `ClearNotifications()` | — |
| `RequestFocus(cursor)` | `true`. Evicts any other Nyx menu first |
| `ReleaseFocus()` | `true` if you held it |
| `IsAnyMenuOpen()` | `boolean` |
| `GetFocusOwner()` | resource name, or `nil` |
| `GetPlayerData()` | normalised player table (below) |
| `RefreshPlayerData()` | same, re-pulled from the framework |
| `GetFramework()` | `'esx'｜'qb'｜'qbx'｜'standalone'` |
| `IsPlayerLoaded()` | `boolean` |
| `GetAccent()` | `{ id, label, hex }` |
| `GetAccentRgb()` | `'255, 43, 214'` |
| `GetAccents()` | list with a per-player `locked` flag |
| `SetAccent(id)` | `boolean` — false if unknown or locked |
| `PlaySound(key)` | `boolean`. `hover｜select｜back｜open｜error｜buy｜pickup` |

`GetPlayerData()`:

```lua
{
    name = 'John Doe',
    job = 'police', jobLabel = 'Police', grade = 3, gradeLabel = 'Lieutenant',
    gang = 'none', gangLabel = '', gangGrade = 0, gangGradeLabel = '',
    cash = 0, bank = 0, black = 0,
    loaded = true
}
```

Always this shape, whichever framework is underneath. It is a copy — mutating it
does not affect the cache.

## nyx_lib — server

```lua
local ok = exports['nyx_lib']:RemoveMoney(src, 'bank', 5000)
if not ok then return end   -- they could not afford it
```

| Export | Returns |
| --- | --- |
| `GetIdentifier(src)` | stable key, or **`nil`** if not resolved yet |
| `GetName(src)` | character name |
| `GetFramework()` | as above |
| `GetMoney(src, account)` | `number`. `'cash'｜'bank'｜'black'` |
| `AddMoney(src, account, amount)` | `boolean` |
| `RemoveMoney(src, account, amount)` | `boolean` — **false means they were broke** |
| `GetJob(src)` | `name, grade` |
| `HasPermission(src, ace)` | `boolean` |
| `RateLimit(src, key, ms)` | `false` if called again too soon |
| `Notify(src, opts)` | — |

Two things worth stating plainly:

**`GetIdentifier` can return nil.** A player can be mid-connection with no
licence resolved. Handle it, or you will write to a store key of `"nil"` and
every such player will share one record.

**Never read a balance and then deduct.** `RemoveMoney` returning `false` is the
check. Two concurrent purchases can both pass a `GetMoney() >= price` test before
either deducts.

### Storage

Buckets are namespaced by the calling resource automatically, so two resources
cannot collide on a name.

```lua
exports['nyx_lib']:DbSet('prefs', identifier, { style = 2 })
local prefs = exports['nyx_lib']:DbGet('prefs', identifier, {})   -- 3rd arg = default
exports['nyx_lib']:DbAll('prefs')       -- whole bucket, as a copy
exports['nyx_lib']:DbDelete('prefs', identifier)
exports['nyx_lib']:DbFlush('prefs')     -- force to disk now
```

Writes are cached and flushed on a timer, on player drop and on resource stop.
`DbAll` returns a deep copy on purpose — a live reference into the cache would
let you mutate data without setting the dirty flag, and your change would never
be saved.

---

## nyx_hud

**Client**

| Export | Notes |
| --- | --- |
| `GetPrefs()` | the whole preference tree |
| `SetStyle(field, value)` | `field` is `'style'` (1–2) or `'moneyStyle'` (3–4) |
| `SetToggle(key, bool)` | `health｜announcements｜speedometer｜watermark｜money｜crosshair｜chat｜map` |
| `SetCrosshair(patch)` | merged over current, then clamped |
| `ResetPrefs()` | back to `NyxHud.Defaults` |
| `GetDefaults()` | the config defaults |

Every setter persists and re-renders. Values are clamped in
`nyx_hud/client/prefs.lua`, and the crosshair table is rebuilt field by field
from a known-good list rather than merged — so an unexpected key cannot reach
the page's style attributes.

**Server**

```lua
exports['nyx_hud']:Announce(-1, 'Restart', 'Server restarting in 5 minutes.', 10000)
```

`-1` for everyone, or a server id. Also `/announce <message>` with
`add_ace group.admin nyx.announce allow`.

---

## nyx_menu

**Client:** `GetStats()` → `{ kills, deaths, ratio }` (`ratio` is a formatted
string).

See the caveat at the top of `nyx_menu/client/stats.lua`: the victim's client is
the only thing that knows who killed it, so these numbers are reportable by a
modified client. Scoreboard, not audit log.

---

## nyx_dealership

**Server**

| Export | Returns |
| --- | --- |
| `GetOwnedVehicles(src)` | `{ [vehicleId] = { plate, model, boughtAt } }` |
| `GiveVehicle(src, vehicleId)` | `ok, plate` — grants without charging |

**Purchase hook.** Fired server-side on every successful purchase:

```lua
AddEventHandler('nyx_dealership:purchased', function(src, data)
    -- data = { identifier, vehicleId, model, label, plate, price }
end)
```

**Keys hook.** Fired client-side when a bought vehicle is handed over, for key
systems:

```lua
AddEventHandler('nyx_dealership:keysGranted', function(netId, plate) end)
```

---

## nyx_inventory

**Server**

```lua
if exports['nyx_inventory']:HasItem(src, 'lockpick', 1) then
    exports['nyx_inventory']:RemoveItem(src, 'lockpick', 1)
end
```

| Export | Returns |
| --- | --- |
| `AddItem(src, name, count)` | `ok, added` — false if it will not fit |
| `RemoveItem(src, name, count)` | `boolean` — **false unless the full amount was removed** |
| `GetItemCount(src, name)` | `number` |
| `HasItem(src, name, count)` | `boolean` |
| `GetInventory(src)` | rendered view |

`RemoveItem` is all-or-nothing: it checks the total first and removes nothing
if the player is short, so you never end up having taken half a payment.

**Client:** `OpenStash(id)`, `Close()`.

---

## Events you can listen to

| Event | Side | Payload |
| --- | --- | --- |
| `nyx:playerDataChanged` | client | the normalised player table |
| `nyx:accentChanged` | client | `accentId, hex` |
| `nyx:forceClose` | client | resource name being evicted from focus |
| `nyx_hud:prefsChanged` | client | the whole preference tree |
| `nyx_hud:chatToggle` | client | `visible` — wire this to your chat |
| `nyx_dealership:purchased` | server | see above |
| `nyx_dealership:keysGranted` | client | `netId, plate` |

## Commands

| Command | Who | Does |
| --- | --- | --- |
| `/menu` | everyone | opens the menu (F1) |
| `/inventory` | everyone | opens the inventory (F2) |
| `/nyxcoords` | everyone | prints a paste-ready config line |
| `/hudreset` | everyone | HUD back to defaults |
| `/nyxunstick` | everyone | releases NUI focus if a cursor is ever stuck |
| `/announce <msg>` | `nyx.announce` | banner to everyone |
| `nyxrefresh <id>` | `nyx.admin` | re-push weapon permissions after an ACE change |
| `nyx` | `nyx.admin` / console | framework, player count, accent count |
