# Roblox MIG Welder

A welding tool for Roblox that only works on metal. Hold the trigger on a metal
part and it lays a real-looking bead (overlapping ripples that glow white-hot and
cool through orange and red to grey). If the bead runs along the seam between two
metal parts, it welds them together. The weld gets stronger the longer and cleaner
it is, and a weak weld snaps under load.

> **Status: not tested in Roblox Studio yet.** It was written without a Luau
> runtime or Studio. The maths is tested in Python (`tests/test_math.py`) and the
> block structure is checked (`tests/check_structure.py`), but expect a first run in
> Studio to turn up a few things. Open **View → Output** and **View → Script
> Analysis** to see any errors.

## What it does

| Feature | How it works in the game | The real thing it's based on |
|---|---|---|
| Metal only | Arc strikes only on `Metal`, `DiamondPlate`, `CorrodedMetal`, `Foil` | Arc welding needs a conductive work piece to close the circuit |
| Rust welds badly | `CorrodedMetal` quality × 0.7 | Rust contaminates the pool and causes porosity |
| Foil / thin sheet | Quality × 0.35 or × 0.5; bead can push out the back | Burn-through on thin metal |
| Travel speed | Too fast = thin bead, weak. Too slow = wide, piled-up bead | Fast = narrow, convex bead, poor fusion. Slow = too much heat, bead too wide |
| Arc length | Welding from far away = flatter, wider bead, more spatter, weaker | A long arc gives more spatter, a flatter bead and porosity |
| Holding still | The puddle keeps growing | Dwelling puts more heat into one spot |
| Bead shape | Overlapping discs sunk into the metal; only a rounded cap shows | The "stacked dimes" look of a MIG/TIG bead |
| Fillet welds | Inside corners (T joints, lap joints) get a corner bead | Fillet weld |
| Glow and cooling | Newton's law of cooling; glow stops at 525 °C | Draper point; blacksmith heat-colour charts |
| Tack, then strength | Parts join after 0.3 studs of bead; strength grows with length × quality | Tack welds hold parts in place but carry little load |
| Snapping | A weld breaks if a hanging part's weight or leverage (torque) is too much for it, or on a hard impact | Weld strength scales with weld length |
| Welding helmet | Screen tints green and goes dark when an arc is visible; press **H** to lift it | Auto-darkening helmets: about shade 3 when light, shade 9–13 when dark |
| Arc flash | Looking at an arc without the helmet makes the screen flash | Arc eye |

## Install

You need three scripts. Pick **one** of the two ways below.

### A. Copy and paste (no extra tools)

1. In Studio, open your place.
2. **ReplicatedStorage** → Insert → **ModuleScript**, name it `WelderShared`,
   paste in [`src/shared/WelderShared.luau`](src/shared/WelderShared.luau).
3. **ServerScriptService** → Insert → **Script**, name it `WelderServer`,
   paste in [`src/server/WelderServer.server.luau`](src/server/WelderServer.server.luau).
4. **StarterPlayer → StarterPlayerScripts** → Insert → **LocalScript**, name it
   `WelderClient`, paste in [`src/client/WelderClient.client.luau`](src/client/WelderClient.client.luau).
5. Press **Play**. You get a **Welder** tool. A practice bench appears 16 studs in front
   of the origin (turn it off with `SpawnDemo = false` in `WelderShared`).

The name `WelderShared` matters: both scripts look it up by that name.

### B. Rojo

If you use [Rojo](https://rojo.space/): `rojo serve` in this folder and connect from the
Studio plugin, or `rojo build -o welder.rbxlx` to build a place file.

## How to weld

* Equip the **Welder** and aim at metal. The marker turns
  **green** on metal, **cyan** on a seam it will join, **orange** if you're too far away,
  and **red** on anything that isn't metal.
* **Hold click** (tap on a phone, right trigger on a gamepad) and move along the seam at a
  steady speed. The panel at the bottom shows your travel speed and weld quality.
* **H** (or gamepad **Y**) lifts or lowers the helmet.
* On the bench: weld **Steel Plate A** to **Steel Plate B** (butt joint), run a bead in the
  corner between **Steel Base** and **Steel Upright** (fillet), and try the **Wood Plank**
  and **Plastic Block** to see the arc refuse to strike.
* **Strength test:** weld **Steel Beam** to **Steel Post**, then hold **E** on the
  **Beam Support** to remove it. A tack weld or a short, sloppy weld snaps. Around a stud
  of good weld holds the beam out sideways. **Reset Bench** (hold E) starts over.

You can force any part to be weldable or not with a boolean attribute named `Weldable`.

## How it works

Reading this alongside the code is the quickest way to learn it.

* **Who does what.** The client (`WelderClient`) only *asks*: about 20 times a second it
  sends "I'm aiming at this part, here". The server (`WelderServer`) checks everything before
  doing anything: the part is metal, it's within reach, and the point really is on that part's
  surface (it does its own raycast). So a modified client can't weld wood or weld from across
  the map. Everything only for looks (glow, flicker, helmet, HUD) runs on the client, so it
  costs no network traffic.
* **The bead.** Each ripple is a thin `Cylinder` part (a disc). The disc is sunk into
  the metal so only a cap sticks out. For a cap of width *w* and height *h*, the circle's
  radius is *R* = (*w*²/4 + *h*²) / 2*h* and its centre sits *R* − *h* below the surface
  (`discForCap`). The hidden part reaches *w*²/4*h* deep, so on thin plates the bead is made
  rounder to stay inside (`fitCapToThickness`). On sheet thinner than 0.15 studs it's allowed to
  poke out the back, which is what burn-through looks like.
  Beads have `CanQuery = false`, so rays and overlap checks pass straight through them.
* **Finding the seam.** `findSeam` casts 8 short rays sideways from just above the surface.
  If one hits a metal wall facing back, it's an inside corner (fillet weld). Then it checks for
  metal parts within `MaxGap` of the point (butt joints) and slides the bead onto the seam.
* **Joining.** Bead laid on a seam is added up per pair of parts. At `TackLength` the two
  parts get a `WeldConstraint`. After that, more bead raises the joint's `ForceRating` and
  `TorqueRating` attributes. You can see them on the `WeldJoint` in the Explorer.
* **Breaking.** Roblox constraints can't break by themselves. RigidConstraint had
  "destruction" properties for a while, but Roblox removed them. So the server checks loads
  itself, 4 times a second:
  if one side of a joint is held by an anchored part and the other side hangs free (nothing
  under it), the joint must carry weight = *m g* and torque = *m g* × sideways distance
  to the centre of mass. Loose parts resting on top (a crate on a shelf) add to that load. It also checks impacts every frame: (mass of the lighter side) ×
  (sudden acceleration beyond `ImpactGrace` g). These are approximations. They only follow
  welds made by this tool, and they ignore spin.
* **Heat.** Temperature follows *T*(*t*) = *T*air + (*T*0 − *T*air) e^(−*kt*).
  Above 525 °C the bead is `Neon` and coloured from a heat-colour table. Below that it
  fades from a blue-grey oxide tint to its final colour.

## Tuning

Everything is in the `Config` table at the top of `WelderShared`. The usual ones:

* `StrengthPerStud`: how strong welds are. The strength test assumes Roblox's
  `Metal` density is 7.85. Check the beam's **Mass** in the Properties window, and if it's
  different, scale this setting by the same amount.
* `MinGoodSpeed` / `IdealSpeed` / `MaxGoodSpeed`: the travel speed window.
* `MetalMaterials`: which materials count as metal, and how well they weld.
* `ArcSoundId`: empty on purpose (no audio ships with this). Put in the id of a looping
  sound you have the rights to.

## Examples

[`examples/`](examples/) has five extra scripts that build on the welder:
choosing what's weldable, leaderboard stats for welds, an angle grinder, a "how much can
your shelf hold?" mini-game, and welding in code with a drop test. See
[`examples/README.md`](examples/README.md).

## Things to try changing yourself

1. Make `Foil` refuse to weld at all instead of welding badly.
2. Show a weld's strength above it when you look at it (a `BillboardGui` at the joint's
   `LocalPoint`).
3. Add slag for stick welding: a dark crust over the bead that you chip off.
4. Make torque use the real direction of the load instead of only the sideways distance.

## Checks

```
python3 tests/check_structure.py   # block/bracket structure of the .luau files (src and examples)
python3 tests/test_math.py         # bead geometry, cooling, quality, strength, example numbers
```

## Sources

* Draper point (525 °C): [Wikipedia: Draper point](https://en.wikipedia.org/wiki/Draper_point)
* Heat colours of steel: [Wikipedia: Red heat](https://en.wikipedia.org/wiki/Red_heat)
* Travel speed and bead shape: [Hobart Brothers: Weld bead contour and penetration](https://www.hobartbrothers.com/resources/technical-guides/aluminum-welding-guide/problem-solving-weld-bead-contour-and-penetration/), [Arc Labs: Arc length, speed and angle](https://arclabs.edu/arc-length-travel-speed-welding-variables/)
* Arc length, spatter, porosity: [Megmeet: How arc length affects a weld](https://www.megmeet-welding.com/en/news/How-Does-Arc-Length-Affect-a-Weld)
* Rust and porosity: [YesWelder: Can you weld rusty metal?](https://yeswelder.com/blogs/welding-101/can-you-weld-rusty-metal-when-it-s-safe-and-how-to-do-it-properly)
* Burn-through: [UNIMIG: Welding burn through](https://unimig.com.au/blog/welding-burn-through-what-is-it-what-causes-it-and-how-to-prevent-it/)
* Auto-darkening helmets: [SimpleWeld: How do auto-darkening helmets work?](https://simpleweld.com/blogs/weldipedia/how-do-auto-darkening-welding-helmets-work)
* RigidConstraint destruction properties removed: [Roblox DevForum: New Physics Class: RigidConstraint](https://devforum.roblox.com/t/new-physics-class-rigidconstraint/1575670), [Roblox Wiki (Fandom): RigidConstraint/Broken](https://roblox.fandom.com/wiki/Class:RigidConstraint/Broken)
* SpecialMesh spheres no longer stretch, which is why beads use cylinders: [Roblox DevForum: Size controls on special meshes break in new experiences](https://devforum.roblox.com/t/size-controls-on-special-meshes-break-in-new-experiences-spheres/4602629)
* Mouse position and `ViewportPointToRay`: [Roblox DevForum: GetMouseLocation documentation issue](https://devforum.roblox.com/t/userinputservicegetmouselocation/946239)
