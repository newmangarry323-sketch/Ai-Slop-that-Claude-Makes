# tools

## check.py

```bash
python3 nyx/tools/check.py
```

Static checks for the suite. Run it after editing anything under
`resources/[nyx]/`. Exit code 0 when clean, 1 otherwise, so it drops straight
into CI or a pre-commit hook. No dependencies beyond Python 3.

It exists because **FiveM fails silently at every seam it covers**. A mistyped
export name is a nil call. A mistyped event name never fires — no error, the
event simply goes nowhere. A `Nyx.post()` to a callback that does not exist
leaves `fetch()` hanging forever with no console output. A web file missing from
a manifest's `files {}` block 404s over NUI and the page renders unstyled. None
of that surfaces until you are in game, usually with players watching.

### The four passes

**1 · Lua structure** — block and bracket balance, unterminated strings and long
brackets, across every `.lua` file.

This is *not* a Lua parser. It strips comments and strings, then checks that
`function`/`if`/`for`/`while`/`do` balance against `end` and that `()`, `[]`,
`{}` nest correctly. That catches the overwhelming majority of hand-editing
mistakes. It will not catch a semantic error, a typo in a native's name, or a
bad expression — load the resource and read the console for those.

**2 · Wiring** — the cross-resource seams:

- `exports['res']:Name()` has a matching `exports('Name', …)` on the same side
- `TriggerServerEvent` / `TriggerClientEvent` / `TriggerEvent` have handlers
- `Nyx.post('x')` has a `RegisterNUICallback('x')` in that resource
- `SendNUIMessage({ action = 'x' })` has a `Nyx.on('x')` in that resource's page
- every `.lua` and `web/` file appears in its `fxmanifest.lua`

Third-party exports (`qb-core`, `es_extended`, …) are skipped — they are not
ours to verify. Deliberate outbound hooks like `nyx_dealership:purchased` are
allowlisted, since the whole point is that *your* script handles them.

**3 · Docs** — every export named in `docs/api.md`, in prose or in a table row,
must actually be defined. A documented-but-missing export is worse than an
undocumented one: somebody writes code against it, gets a nil call, and has no
reason to suspect the documentation.

**4 · Theme** — no hardcoded `#ff2bd6` outside `theme.css`. The moment a
resource stylesheet hardcodes the accent, the in-game colour picker stops
repainting that element and the suite looks broken on every colour but magenta.

### Trusting it

A checker that always passes is worthless, so each pass was verified by
injecting a deliberate fault of its own kind and confirming it was caught —
including one round that exposed a genuine shadowing bug in the checker itself.
If you extend it, do the same: break something on purpose first and watch it
fail.
