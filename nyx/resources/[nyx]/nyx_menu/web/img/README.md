# Location and weapon art

Drop images here and reference them from `config.lua` as `image = 'img/name.jpg'`.

Nyx resolves an `image` value in this order:

| Value | Resolves to |
| --- | --- |
| `img/ap-hangar.jpg` | this folder, served from the resource |
| `nui://some_resource/web/shot.png` | a file another running resource declares in `files` |
| `https://…` | fetched over the network by the game's browser |
| *nothing* | a generated gradient tile carrying the location's initials |

Nothing here is required — the generated tiles are the default and the menu
looks finished without a single image file.

**Taking the screenshots.** The reference UI uses in-game captures. Stand where
you want the tile to show, hide the HUD, and take a screenshot; crop to 16:9 and
save around 640×360 — the tiles never render larger than about 320px wide, so
anything bigger is loaded and thrown away. `.jpg` at quality 80 keeps a full set
of forty tiles under a megabyte, which matters because every client downloads
all of them on join.
