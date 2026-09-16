# Nyx

A five-resource FiveM interface suite: a locations/weapons menu, a HUD with a
crosshair editor, a vehicle dealership and a slot inventory — all drawing from
one stylesheet, so they look like one product instead of four scripts that
happen to be installed together.

Dark, near-black surfaces; a magenta bloom that bleeds down from the top of the
screen; glass panels with hairline borders; one accent colour that every tinted
pixel in the suite derives from, swappable at runtime from inside the game.

```
resources/[nyx]/
├── nyx_lib          the design system, framework bridge and storage
├── nyx_menu         Locations · Weapons · Miscellaneous          (F1)
├── nyx_hud          HUD, speedometer, crosshair, announcements
├── nyx_dealership   showroom with live preview and test drives
└── nyx_inventory    slots, ground drops, stashes, transfers      (F2)
```

## Install

Copy the `[nyx]` folder into your server's `resources/`, then add this to
`server.cfg` **in this order** — `nyx_lib` defines the globals the other four
compile against, so it has to start first:

```cfg
ensure nyx_lib
ensure nyx_hud
ensure nyx_menu
ensure nyx_dealership
ensure nyx_inventory
```

That is the whole installation. No database, no dependencies, no build step.
Start the server, press **F1**.

Full walkthrough, including what to do when you already run an inventory or a
HUD: **[docs/installation.md](docs/installation.md)**.

## What it runs on

Framework detection is automatic and happens once at startup — the console
prints what it found. **ESX**, **QBCore** and **Qbox** are each read through a
bridge that flattens them into one shape, and **standalone** servers get a
wallet Nyx keeps itself so the dealership and inventory still work with no
framework at all. Nothing outside `nyx_lib/client/bridge.lua` and
`nyx_lib/server/bridge.lua` knows which one you run.

Persistence is JSON files under `nyx_lib/data/`. That is a deliberate choice:
the suite has to work on a server with no database. If you already run oxmysql,
[docs/configuration.md](docs/configuration.md#swapping-the-store-for-mysql)
shows the two functions to replace, and nothing else in the suite changes,
because nothing else in the suite knows how persistence works.

## The design system

Every resource loads one file:

```html
<link rel="stylesheet" href="nui://nyx_lib/web/theme.css">
```

`nui://<resource>/<path>` reaches any file a running resource declares in its
manifest's `files` block. So there is one stylesheet, not five copies drifting
apart. Restyle the whole suite by editing it and restarting `nyx_lib`.

Colour comes from a single channel triplet:

```css
:root { --nyx-accent-rgb: 255, 43, 214; }
```

Everything tinted is `rgba(var(--nyx-accent-rgb), …)`. Changing those three
numbers repaints all four interfaces. The in-game colour picker does exactly
that, at runtime, per player — see [docs/theming.md](docs/theming.md).

The comma syntax is on purpose. `color-mix()` and the space-separated slash
syntax need Chromium 111+, and FiveM servers run on older CEF builds more often
than you would like.

## What each resource does

**nyx_menu** — three tabs. *Locations* is sections → groups → cards, each card
showing a live count of how many players are standing there. *Weapons* hands out
single weapons or preset kits, gated per item, group or section by ACE
permission. *Miscellaneous* configures the HUD and the crosshair. A K/D/ratio
capsule sits in the top bar. `/nyxcoords` prints a ready-to-paste config line
for wherever you are standing.

**nyx_hud** — health, armour and voice as a pill row or as rings; money, job and
gang as stacked bars or as pixel text; a speedometer with RPM, fuel and engine
bars; FPS; a watermark with a live player count; a centre-screen announcement
banner; and a crosshair with shape, length, thickness, gap, colour, opacity,
outline, centre dot and dynamic spread. Eight independent on/off toggles. Every
setting is stored per player, per identifier.

**nyx_dealership** — walk up, press E. A left rail of categories, search across
the whole catalogue, pagination, and a live in-world preview of the selected
vehicle on a slow turntable with the camera pointed at it. Performance meters
read the game's own handling figures. Test drive with a countdown that returns
you to the showroom; respray cycles a palette on the preview. Prices live on the
server.

**nyx_inventory** — forty slots, weight, stacking, ground drops that merge at
your feet and expire, fixed stashes, player-to-player transfers with a
server-checked distance, and weapon items that hand over real weapons. Drag
between panels, double-click to move a whole stack, drag onto the backdrop to
drop. See the honest scope note at the top of
`resources/[nyx]/nyx_inventory/config.lua`.

## Where the trust boundary is

This matters more than any feature, so it is stated plainly rather than buried.

**The client is never the authority.** Every action crosses the wire as an *id*,
never as a coordinate, a price, a weapon hash or an item count. The server looks
that id up in its own copy of `config.lua` — shared into both states by
`shared_script`, so the two can never disagree — checks whatever gate the config
attached to it, and only then acts. A modified client that invents an id, edits
a price or asks for a weapon it has no ACE for gets nothing back.

**Money is removed before the thing is delivered, and never by reading a balance
first.** `RemoveMoney` returns false when the player cannot afford it. Reading
the balance and then deducting is a race two concurrent purchases can win.

**Everything a player can reach is rate-limited** per player, per action.

**Anything player-authored is escaped before it reaches the DOM.** NUI is a
browser; a player named `<img src=x onerror=…>` is a real thing that happens.
Names, item labels and notification text all go through `Nyx.esc`. A crosshair
colour lands in a style attribute, so it is validated against a literal hex
pattern rather than trusted.

**Two things are genuinely client-authoritative, and you should know which.**
GTA has no server-side notion of "player A killed player B", so the victim's
client reports its own death and names the killer — the server verifies the
named killer is a real connected player and rate-limits the claim, which stops
the lazy abuse, but these are scoreboard numbers, not an audit log. And weapons
are client-side entities, so the server validates and authorises the request but
the client performs the give. Do not attach anything valuable to either without
your own checks.

## What this is not

Being clear up front saves you finding out in week three:

- **Not an ox_inventory replacement.** No shops, crafting, clothing integration,
  metadata editor, or migration from another inventory's tables. If you already
  run one, keep it — `nyx_inventory` is optional and the other four resources
  do not depend on it.
- **No art.** No fonts, no location screenshots, no item icons, no vehicle
  images. Font binaries and game captures carry licences this repository has no
  right to redistribute. Every one of them degrades to a generated tile or a
  system font, so the suite looks finished with all of those folders empty —
  each has a README explaining what to drop in.
- **Coordinates are a starting point, not a survey.** They are real spots on the
  base map, but use `/nyxcoords` to capture your own.
- **Not load-tested at scale.** It is written to be frugal — loops sleep when
  idle, occupancy reports only on change, broadcasts are coalesced — but "should
  be fine" is a prediction, not a measurement, and it has not been measured on a
  full server.

## Documentation

| | |
| --- | --- |
| [Installation](docs/installation.md) | Setup, load order, coexisting with what you already run |
| [Configuration](docs/configuration.md) | Every config file, ACE permissions, MySQL swap |
| [Theming](docs/theming.md) | Accents, fonts, restyling, adding your own screens |
| [API](docs/api.md) | Exports and events for talking to Nyx from your own scripts |
| [Troubleshooting](docs/troubleshooting.md) | Symptom-first, including the cursor-stuck fix |
| [tools/check.py](tools/README.md) | Static checks — run it after every edit |

## Licence

MIT, same as the rest of this repository. See [`../LICENSE`](../LICENSE).
