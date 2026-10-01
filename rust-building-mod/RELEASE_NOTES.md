Rust-style building for Minecraft **26.3** on **Fabric**.

Drop `rust-building-1.1.0.jar` into your `mods` folder together with
[Fabric API](https://modrinth.com/mod/fabric-api) (0.161.0+26.3 or newer) and start the game with
Fabric Loader 0.19.5 or newer. It has to be installed on the server and on every client.

**New in 1.1.0**

- **Wall frames and garage doors.** A wall frame is a wall with a 3 x 2 opening; a garage door fills
  it, rolls up when opened, takes a code lock and has 600 health (3 TNT).
- **Upkeep.** The tool cupboard has storage. It pays Rust's upkeep from it - a share of what the base
  cost to build, in each grade's own material - and its screen shows how long the base is protected.
- **Decay.** Pieces that no stocked tool cupboard covers lose health and finally fall: a day for twig
  up to 12 days for armored (Rust's hours, as Minecraft days).
- Fixed: the block over a window, doorway or frame opening is now part of its wall (it used to be
  destroyed outright by explosions), and a door goes with its wall instead of floating in the air.

**Updating from 1.0.0:** bases start decaying as soon as you load them. Put sticks, planks,
cobblestone, iron nuggets or iron ingots (whatever your base is built from) in the storage of a
tool cupboard that covers it.

See [rust-building-mod/README.md](https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/blob/main/rust-building-mod/README.md)
for how to play and how to build it yourself.
