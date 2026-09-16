# Fonts

Nyx references two faces and **ships neither** — font binaries carry licences
this repository has no right to redistribute.

| Drop a file here | Used for |
| --- | --- |
| `sans.woff2` | Everything: menus, buttons, cards |
| `pixel.woff2` | HUD styles 3 and 4, the `.nyx-pixel` class |

If a file is absent the `@font-face` rule in `theme.css` falls through to
`local()` lookups and then to the generic stack, so the UI renders correctly
either way — it just uses whatever the player already has installed.

The reference screenshots use a geometric grotesque for the sans (Poppins and
Outfit are both close and both SIL Open Font Licence) and a segmented LCD face
for the pixel role. Convert a `.ttf` to `.woff2` with `woff2_compress`, or any
web font converter, and drop it in under the name above. Nothing else changes.

## A note on the console

With no files here, the browser requests `sans.woff2` and `pixel.woff2`, gets a
404 and falls back. That logs two "Failed to load resource" lines in the NUI
console on every page. It is harmless, and it is the cost of making the files
drop-in: the alternative is a `local()`-only rule that would ignore anything you
put in this folder until you also edited `theme.css`. Drop the files in and the
messages stop.
