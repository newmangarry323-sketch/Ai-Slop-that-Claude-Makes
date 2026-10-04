# Welder examples

Five small scripts that use the welder without changing it. Each one is a
**Script** for **ServerScriptService**. Install the welder first (see the
[main README](../README.md#install)), then add only the examples you want. Each
file explains itself at the top and ends with ideas to try.

They are not tested in Roblox Studio yet. If one fails, the Output window says which
line.

| # | File | What you get | What it teaches |
|---|---|---|---|
| 1 | [`01_WeldableParts`](01_WeldableParts.server.luau) | A gas bottle that refuses to weld, a painted steel beam that does, and every part tagged `Steel` made weldable | The `Weldable` attribute and CollectionService tags |
| 2 | [`02_WeldEvents`](02_WeldEvents.server.luau) | "Welds" and "Best" columns on the leaderboard, plus Output messages when welds are made or break | Following joints with tags and attribute signals |
| 3 | [`03_Grinder`](03_Grinder.server.luau) | An angle grinder tool that grinds beads off and weakens or frees the joint | Finding beads, and changing a joint's strength |
| 4 | [`04_ShelfChallenge`](04_ShelfChallenge.server.luau) | A mini-game: weld a shelf to a wall, then see how many crates it holds | Building a game on top of the welder's load checks |
| 5 | [`05_PreweldedDropTest`](05_PreweldedDropTest.server.luau) | Two blocks welded in code (a tack vs. full welds) dropped from 20 studs | Making welds in code; how impacts break welds |

## Where things appear

All positions are relative to the practice bench (`Config.DemoPosition`, 16 studs
in front of the world origin):

* Example 1: 20 studs left of the bench
* Example 4: 18 studs behind the bench
* Example 5: 20 studs left of and 18 behind the bench

## What to expect

These numbers come from `tests/test_math.py`. They assume Roblox's `Metal`
density is 7.85.

**Shelf challenge (example 4):**

| Weld along the back edge | Crates held (of 6) |
|---|---|
| Tack (0.3 studs) | none: the shelf falls when the prop goes |
| 0.6 studs, good | 1 |
| 1 stud, good | 3 |
| 1.5 studs, good | 4 |
| 2 studs or more, good | all 6 |

**Drop test (example 5):** the tack weld cracks and the full welds hold. The impact
force depends on how many physics frames the landing takes, so it varies a little
between drops.

## The joint format

Examples 2 to 5 rely on how the welder stores a joint. It's a `WeldConstraint`
inside its `Part0`, tagged `WelderJoint`, with these attributes:

| Attribute | Meaning |
|---|---|
| `BeadLength` | studs of bead on the seam |
| `Quality` | average weld quality, 0 to 1 |
| `ForceRating` | weight or impact force it can take before it breaks |
| `TorqueRating` | leverage it can take before it breaks |
| `LocalPoint` | centre of the bead, in `Part0`'s space |
| `WeldedBy` | UserId of the player who tacked it (missing on welds made in code) |

Beads are parts tagged `WelderBead` inside a folder named `WeldBeads` in the part
they sit on. Change those attributes and the welder uses the new values on its next
check (4 times a second). That's how the grinder weakens welds.

## Using Rojo

`default.project.json` only syncs the welder. To sync an example too, add an entry
under `ServerScriptService`, for example:

```json
"ShelfChallenge": { "$path": "examples/04_ShelfChallenge.server.luau" }
```
