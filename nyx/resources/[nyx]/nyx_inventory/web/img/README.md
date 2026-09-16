# Item icons

Reference an icon from `config.lua` as `image = 'img/carbine.png'`.

Without one, Nyx draws the item's initials on the slot's gradient tile — which
is what the whole default catalogue does, so the inventory is fully usable with
this folder empty.

Transparent PNGs at 128×128 look right; the tile renders them at about 70px,
and anything much larger is bandwidth every player pays for on join.
