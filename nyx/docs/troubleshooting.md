# Troubleshooting

Symptom first. Most of these are the same handful of causes.

## My cursor is stuck and I cannot move

```
/nyxunstick
```

That releases NUI focus unconditionally. It is bound to nothing by default so it
can never be the cause of the problem it fixes.

Nyx should not let this happen: one focus broker owns the cursor, evicts any
other Nyx menu on open, and releases focus automatically if a resource stops
while holding it. If it happens repeatedly, the other resource is almost
certainly not a Nyx one — something else called `SetNuiFocus(true, true)` and
never released it. `/nyxunstick` still clears it.

If you are writing NUI yourself, take focus through
`exports['nyx_lib']:RequestFocus(true)` and you inherit all of that.

## Nothing loads — red errors about a nil value named `Nyx`

`nyx_lib` is not started, or started after the resource that needs it. It is
compiled into the others at load time via `@nyx_lib/shared/config.lua`, so it
has to exist first.

```cfg
ensure nyx_lib          # first
ensure nyx_hud
ensure nyx_menu
ensure nyx_dealership
ensure nyx_inventory
```

## Nyx says `standalone` but I run ESX or QBCore

Detection checks `GetResourceState` for `qbx_core`, `es_extended`, then
`qb-core`. It reports standalone when none is *started*, or when the framework
is started but does not answer for its core object within five seconds.

1. Confirm your framework starts **before** `nyx_lib` in `server.cfg`.
2. Pin it: `Nyx.Config.framework = 'esx'` in `nyx_lib/shared/config.lua`.
3. Set `Nyx.Config.debug = true` for bridge logging.

A non-standard framework fork may rename its export. `resolveCore()` in
`nyx_lib/client/bridge.lua` is four lines and is the only place that needs to
change.

## The UI is unstyled — plain text on black

The page could not load `nui://nyx_lib/web/theme.css`. Either `nyx_lib` is not
running, or it was renamed. The path is literal: rename the folder and every
`nui://nyx_lib/…` reference in all four other resources breaks.

If you must rename it, find and replace `nui://nyx_lib/` across the `web/`
folders and update the `@nyx_lib/` manifest lines too.

## Everything works but there are no images

That is the default. Nyx ships no art — see
[theming.md → Art](theming.md#art). Cards render generated gradient tiles with
the location's initials, items render their initials on the slot. Drop files
into `nyx_menu/web/img/` and `nyx_inventory/web/img/` and reference them from
the configs.

## The font looks wrong

Also the default — no font binaries ship. `theme.css` falls through to
`local('Poppins')`, `local('Outfit')`, `local('Segoe UI')` and then the generic
sans. Drop `sans.woff2` and `pixel.woff2` into `nyx_lib/web/fonts/`.

## I teleport into the void, or fall through the map

`NyxMenu.DoTeleport` already fades out, freezes, moves, and spins on
`HasCollisionLoadedAroundEntity` with a 15-second timeout before unfreezing. If
you still land badly the Z in your config is probably wrong.

Set `snapToGround = true` on that spot to probe for the real ground height, or
better, stand exactly where you want to land and re-capture with `/nyxcoords`.

## Location cards all show 0 players

- `NyxMenu.Occupancy.enabled` is false, or
- everyone is further than `radius` (default 60m) from any configured spot, or
- more than one spot shares an `id` — ids must be unique across the whole file.

Counts update at most once a second, coalesced, so give it a moment.

## A weapon or vehicle does not appear in the menu

It is gated. The tab, section, group or item has an `ace` the player lacks — and
the narrowest one wins, so an item can be hidden by its section's rule.

```cfg
add_ace group.admin nyx.weapons.sniper allow
```

After granting an ACE at runtime, run `nyxrefresh <serverId>`: permissions are
pushed once when the client connects, not re-evaluated per open.

A category with a count of zero is normal — those render greyed and unclickable
by design.

## A vehicle does not spawn in the showroom

The model failed to load. `RequestModel` checks `IsModelInCdimage` and
`IsModelAVehicle` first and warns in the console with the exact name:

```
[nyx:nyx_dealership] model "xyz" is not a loadable vehicle — check the spawn name
```

Either the spawn name is wrong, or it is an add-on whose resource is not
started. The bar shows the vehicle but disables Buy rather than selling you
something that cannot exist.

## Preferences or inventory reset on restart

Check `nyx_lib/data/`. If the JSON files are missing, the server process cannot
write there — check filesystem permissions on the resource folder.

If you see a `*.json.broken` file, a write was interrupted mid-save. The store
kept the damaged original rather than overwriting it, logged loudly, and started
that bucket empty. The `.broken` file is your recovery copy.

Writes flush every `Nyx.Config.autosaveInterval` seconds (default 120), on
player drop and on resource stop. A hard crash loses at most that interval — a
`stop`/`restart` loses nothing.

## The chat toggle does nothing

Expected. `DisplayRadar()` is a native so the map toggle works, but there is no
standard way to hide another resource's chat. Nyx fires `nyx_hud:chatToggle` and
leaves the wiring to you — snippet in
[configuration.md](configuration.md#making-the-chat-toggle-do-something).

## The K/D numbers look wrong

Read the caveat at the top of `nyx_menu/client/stats.lua`. GTA has no
server-side notion of who killed whom, so the victim's client reports it. The
server verifies the named killer is a real connected player and rate-limits the
claim, which stops casual abuse, but a modified client can still lie about its
own deaths. Do not attach payouts or bans to these numbers.

Deaths with no identifiable killer (falls, drowning, vehicles) still count
unless you set `NyxMenu.Stats.countEnvironmentDeaths = false`.

## Two HUDs are drawing at once

You are running another HUD. Stop one. To keep yours but still hide the *game's*
native elements, set `NyxHud.HideNativeComponents` and stop `nyx_hud` entirely —
the Miscellaneous tab detects its absence and says so rather than erroring.

## Turning on debug logging

```lua
Nyx.Config.debug = true   -- nyx_lib/shared/config.lua
```

`restart nyx_lib`, then restart the others so they pick up the recompiled shared
config. Warnings and errors always print regardless of this flag; it only adds
the routine chatter.
