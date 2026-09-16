# Theming

Everything visual in all four interfaces comes from one file:

```
resources/[nyx]/nyx_lib/web/theme.css
```

Each page loads it over NUI:

```html
<link rel="stylesheet" href="nui://nyx_lib/web/theme.css">
```

`nui://<resource>/<path>` reaches any file a running resource declares in its
manifest's `files {}` block. That is the mechanism that lets five resources
share one stylesheet instead of keeping five copies that drift apart. Edit it,
`restart nyx_lib`, and every Nyx screen changes.

## The accent

One custom property drives every tinted pixel:

```css
:root { --nyx-accent-rgb: 255, 43, 214; }
```

Nothing hardcodes magenta. Borders, glows, fills, gradients, the crosshair
default, the slot tiles and the button gradient are all
`rgba(var(--nyx-accent-rgb), …)`.

**Why the comma syntax.** `color-mix()` and the modern `rgb(R G B / A)` slash
form both need Chromium 111+. FiveM servers run on older CEF builds more often
than you would like, and a theme that silently renders every accent as
transparent black on a third of your players is worse than a slightly
old-fashioned stylesheet. `rgba(var(--x), .2)` works everywhere.

### Changing the default for everyone

Edit `Nyx.DefaultAccent` in `nyx_lib/shared/config.lua`:

```lua
Nyx.DefaultAccent = 'azure'
```

### Adding a colour

```lua
Nyx.Accents = {
    { id = 'magenta', label = 'Magenta', hex = '#ff2bd6' },
    { id = 'toxic',   label = 'Toxic',   hex = '#a3ff12' },   -- new
}
```

That is the entire job. The picker renders the list, the client validates the
hex, `Nyx.setAccent` writes the triplet, and all four interfaces repaint live —
including any open menu, because `nyx:accentChanged` is broadcast and every
resource forwards it to its own page.

### Changing it at runtime from your own script

```lua
exports['nyx_lib']:SetAccent('emerald')
```

Returns `false` if the id is unknown or still locked for that player. The choice
is persisted per identifier, so it survives a reconnect.

## The rest of the tokens

Everything in `:root`, grouped as it appears in the file:

| Group | What it sets |
| --- | --- |
| `--nyx-void`, `--nyx-surface*`, `--nyx-raise*` | Backgrounds, darkest to lightest |
| `--nyx-line*` | The three border weights |
| `--nyx-text`, `--nyx-text-dim`, `--nyx-text-mute` | The three text weights |
| `--nyx-good/warn/bad/info` | Semantic colours, independent of the accent |
| `--nyx-r-*` | The radius scale, `xs` through `pill` |
| `--nyx-shadow`, `--nyx-glow` | Elevation |
| `--nyx-fast/mid/slow` | Motion durations and easing |

### Making it light

The suite is built dark and is not a light theme in disguise, but the tokens are
all in one place, so it is a contained job:

```css
:root {
    --nyx-void: #f4f4f7;
    --nyx-surface: #ffffff;
    --nyx-raise: rgba(0, 0, 0, .04);
    --nyx-raise-2: rgba(0, 0, 0, .07);
    --nyx-line: rgba(0, 0, 0, .10);
    --nyx-line-hard: rgba(0, 0, 0, .18);
    --nyx-text: #111114;
    --nyx-text-dim: #55555f;
    --nyx-text-mute: #8a8a95;
    --nyx-shadow: 0 18px 44px rgba(0, 0, 0, .12);
}
```

You will also want to soften `.nyx-backdrop`, whose two radial gradients assume
a dark base.

## Fonts

Two faces are referenced; **neither ships**. Font binaries carry licences this
repository has no right to redistribute.

```
nyx_lib/web/fonts/
├── sans.woff2     everything: menus, buttons, cards
└── pixel.woff2    HUD styles 3 and 4, and the .nyx-pixel class
```

Drop files in under those exact names and they are picked up with no other
change. Until then the `@font-face` rules fall through to `local()` lookups
(Poppins, Outfit, Segoe UI; Consolas, DejaVu Sans Mono) and then to the generic
stack — so the UI renders correctly either way, just with whatever the player
already has installed.

The reference screenshots use a geometric grotesque for the sans role. Poppins
and Outfit are both close and both SIL Open Font Licence. Convert with
`woff2_compress` or any web font converter.

`web/fonts/*.woff2` is already declared in `nyx_lib`'s manifest, so a dropped-in
file is servable immediately.

## Art

Nothing is required. Every image slot degrades to a generated tile.

| Folder | For | Fallback |
| --- | --- | --- |
| `nyx_menu/web/img/` | Location and weapon cards | Gradient tile with the initials |
| `nyx_inventory/web/img/` | Item icons | Initials on the slot's gradient |

The generated tile derives its hue from the item's id, so a given card is always
the same colour rather than changing on every render — it reads as deliberate.

Reference art as `img/name.jpg` from the relevant `config.lua`. Values are also
accepted as `https://…` or `nui://resource/path`.

Crop location shots to 16:9 at about 640×360. The tiles never render wider than
roughly 320px, and every client downloads all of them on join, so a set of forty
oversized PNGs is bandwidth every player pays for to see a thumbnail.

## The component classes

`theme.css` is a small component library, not just variables. Reuse these in
your own NUI and it will match without copying any CSS:

| Class | Component |
| --- | --- |
| `.nyx-backdrop`, `.nyx-backdrop--sheer` | Full-screen background; `--sheer` for overlays on live gameplay |
| `.nyx-topbar`, `.nyx-tab`, `.nyx-brand-text` | The floating top capsule |
| `.nyx-panel`, `.nyx-strip` | Glass panel, section label strip |
| `.nyx-rail`, `.nyx-rail-item` | Left-hand navigation |
| `.nyx-grid`, `.nyx-card`, `.nyx-badge` | Image card grid |
| `.nyx-btn` + `--primary` `--ghost` `--danger` `--block` `--stack` | Buttons |
| `.nyx-field`, `.nyx-range`, `.nyx-toggle`, `.nyx-swatch` | Inputs |
| `.nyx-meter` | Labelled 0–100 bar |
| `.nyx-slot`, `.nyx-slots` | Inventory slots |
| `.nyx-notify` | Notification card |
| `.nyx-scroll` | Thin dark scrollbar, accent on hover |
| `.nyx-empty` | Centred empty state |
| `.nyx-pixel` | The pixel face |

## Building your own Nyx screen

A new resource joins the design system with two lines of HTML:

```html
<link rel="stylesheet" href="nui://nyx_lib/web/theme.css">
<script src="nui://nyx_lib/web/nyx.js"></script>
```

`nyx.js` gives you:

```js
Nyx.icon('car')                  // inline SVG string; '' for an unknown name
Nyx.esc(value)                   // escape before ANY innerHTML — see below
Nyx.escUrl(value)                // escape + scheme-check before src/url()
Nyx.post('callback', { … })      // → RegisterNUICallback, resolves with the cb payload
Nyx.on('action', fn)             // ← SendNUIMessage({ action = 'action' })
Nyx.onEscape(fn)                 // Esc and Backspace close
Nyx.setAccent('#ff2bd6')         // repaint
Nyx.money(n) / Nyx.compact(n)    // '$1,250' / '1.3K'
Nyx.debounce(fn, ms)
Nyx.crosshair(el, settings)      // the shared crosshair renderer
```

On the Lua side:

```lua
exports['nyx_lib']:RequestFocus(true)   -- evicts whatever else had focus
exports['nyx_lib']:ReleaseFocus()
exports['nyx_lib']:Notify{ title = 'Saved', type = 'success' }
```

Always take focus through `RequestFocus`. Two resources both calling
`SetNuiFocus(true, true)` is the single most common way a FiveM UI suite traps
the player's cursor: whichever closes last wins, and if it never closes the
player is stuck with a mouse and no keyboard. The broker keeps one owner at a
time, tells the outgoing one to close via `nyx:forceClose`, and releases focus
on your behalf if your resource stops while holding it.

### Escape everything

NUI is a browser. Anything that came from a player — names, item labels, chat,
a vehicle name typed into a config by a junior admin — must go through
`Nyx.esc()` before it touches `innerHTML`. A player called
`<img src=x onerror=…>` is a real thing that happens, and the page will run it.

```js
node.innerHTML = '<b>' + Nyx.esc(player.name) + '</b>';   // safe
node.innerHTML = '<b>' + player.name + '</b>';            // not
```

For anything landing in a `src` or `url()`, use `Nyx.escUrl`, which also
rejects schemes that are not http(s), `nui:`, `data:image/` or a relative path.
