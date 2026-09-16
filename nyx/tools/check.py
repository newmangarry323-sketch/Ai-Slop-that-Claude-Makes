#!/usr/bin/env python3
"""Nyx — static checks.

    python3 nyx/tools/check.py

Run this after editing anything under resources/[nyx]/. FiveM fails silently at
every seam this covers: a mistyped export is a nil call, a mistyped event never
fires, a NUI callback that does not exist leaves fetch() hanging forever, and a
web file missing from a manifest 404s. None of it surfaces until you are in game
with players watching.

Four passes:

  1. Lua structure   block and bracket balance, unterminated strings
  2. Wiring          exports, net events, NUI callbacks, NUI messages, manifests
  3. Docs            every export named in docs/api.md must actually exist
  4. Theme           no hardcoded accent colours outside theme.css

Pass 1 is NOT a Lua parser - there is no Lua interpreter assumed present. It
catches structural mistakes, which is the overwhelming majority of hand-editing
errors, but it will not catch a semantic or expression-level bug. Load the
resource and read the console for those.

Exit code is 0 when everything passes, 1 otherwise, so it drops straight into
CI or a pre-commit hook.
"""
import re
import sys
import pathlib
import collections

HERE = pathlib.Path(__file__).resolve().parent
NYX = HERE.parent
RESOURCES = NYX / "resources" / "[nyx]"
DOCS = NYX / "docs"

problems = []


def fail(message):
    problems.append(message)


def rel(path):
    try:
        return path.relative_to(NYX).as_posix()
    except ValueError:
        return path.as_posix()



# ===========================================================================
# Pass 1 - Lua structure
# ===========================================================================
OPEN = {"function", "if", "for", "while", "do"}          # `do` after for/while is consumed below
KW = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")

def _strip(src, path, errs):
    out, i, n = [], 0, len(src)
    line = 1
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1; out.append("\n"); i += 1; continue
        # long bracket comment or string: [[ ]] / [=[ ]=]
        m = re.match(r"(--)?\[(=*)\[", src[i:])
        if m:
            eq = m.group(2)
            close = "]" + eq + "]"
            j = src.find(close, i + len(m.group(0)))
            if j == -1:
                out.append(f"{path}:{line}: unterminated long bracket [{eq}[")
                return "".join(out)
            chunk = src[i:j+len(close)]
            out.append("\n" * chunk.count("\n"))
            line += chunk.count("\n")
            i = j + len(close); continue
        if src.startswith("--", i):
            j = src.find("\n", i)
            i = n if j == -1 else j; continue
        if c in "\"'":
            j = i + 1
            while j < n:
                if src[j] == "\\": j += 2; continue
                if src[j] == "\n":
                    out.append(f"{path}:{line}: unterminated string"); break
                if src[j] == c: j += 1; break
                j += 1
            out.append('""'); i = j; continue
        out.append(c); i += 1
    return "".join(out)

def _lua_structure(path):
    out = []
    errs = out
    src = pathlib.Path(path).read_text(encoding="utf-8")
    code = _strip(src, path, errs)

    # bracket balance, with line numbers
    stack = []
    pairs = {")": "(", "]": "[", "}": "{"}
    line = 1
    for ch in code:
        if ch == "\n": line += 1
        elif ch in "([{": stack.append((ch, line))
        elif ch in ")]}":
            if not stack or stack[-1][0] != pairs[ch]:
                out.append(f"{path}:{line}: unmatched '{ch}'"); break
            stack.pop()
    if stack:
        ch, ln = stack[-1]
        out.append(f"{path}:{ln}: unclosed '{ch}'")

    # block balance
    depth, line, prev = 0, 1, None
    opens = []
    for m in re.finditer(r"\n|\b[A-Za-z_][A-Za-z0-9_]*\b", code):
        t = m.group(0)
        if t == "\n": line += 1; continue
        if t in ("for", "while"): opens.append((t, line)); depth += 1
        elif t == "if": opens.append((t, line)); depth += 1
        elif t == "function": opens.append((t, line)); depth += 1
        elif t == "do":
            if prev not in ("for", "while", "close_for", "close_while"):
                # standalone `do` block (for/while `do` is already counted)
                if not opens or opens[-1][0] not in ("for", "while"):
                    opens.append(("do", line)); depth += 1
                else:
                    opens[-1] = ("do", opens[-1][1])
            else:
                opens[-1] = ("do", opens[-1][1])
        elif t == "end":
            depth -= 1
            if depth < 0:
                out.append(f"{path}:{line}: extra 'end'"); depth = 0
            elif opens: opens.pop()
        prev = t
    if depth > 0:
        where = opens[0] if opens else ("?", "?")
        out.append(f"{path}: {depth} unclosed block(s); first opened by '{where[0]}' at line {where[1]}")
    return out



def pass_lua_structure(lua_files):
    for path in lua_files:
        for message in _lua_structure(path):
            # _lua_structure builds messages from the path it was handed, which
            # is absolute; rewrite the prefix so every finding reads the same.
            fail(message.replace(str(path), rel(path), 1))


# ===========================================================================
# Pass 2 - runtime wiring
# ===========================================================================
def pass_wiring(lua_files, js_files):
    lua = lua_files
    js = js_files
    def read(paths):
        return {p: p.read_text(encoding='utf-8') for p in paths}

    luasrc = read(lua)
    jssrc = read(js)


    def resource_of(path):
        parts = path.relative_to(RESOURCES).parts
        return parts[0] if parts else '?'

    # ---------------------------------------------------------------- exports ----
    # exports('Name', ...)  defines;  exports['res']:Name(  calls
    defined = collections.defaultdict(set)
    for p, s in luasrc.items():
        side = 'server' if '/server/' in p.as_posix() else ('client' if '/client/' in p.as_posix() else 'shared')
        for m in re.finditer(r"""exports\(\s*['"]([A-Za-z_]\w*)['"]""", s):
            defined[(resource_of(p), side)].add(m.group(1))

    for p, s in luasrc.items():
        caller_side = 'server' if '/server/' in p.as_posix() else ('client' if '/client/' in p.as_posix() else None)
        for m in re.finditer(r"""exports\[['"]([\w-]+)['"]\]:(\w+)\(""", s):
            res, name = m.group(1), m.group(2)
            if not (RESOURCES / res).is_dir():
                continue  # third-party (qb-core, es_extended, ...) - not ours to check
            sides = [caller_side] if caller_side else ['client', 'server']
            if not any(name in defined[(res, side)] for side in sides):
                line = s[:m.start()].count('\n') + 1
                fail(f"{rel(p)}:{line}: exports['{res}']:{name}() has no matching exports('{name}', ...) on the {caller_side or 'shared'} side")
        # dotted form: exports.qbx_core:Foo() - skipped, always third-party

    # ----------------------------------------------------------------- events ----
    # TriggerServerEvent('x') needs a server RegisterNetEvent('x')
    # TriggerClientEvent('x', ...) needs a client RegisterNetEvent('x')
    # TriggerEvent('x') needs any AddEventHandler/RegisterNetEvent('x')
    reg_client, reg_server, reg_any = set(), set(), set()
    for p, s in luasrc.items():
        posix = p.as_posix()
        for m in re.finditer(r"""(?:RegisterNetEvent|AddEventHandler)\(\s*['"]([\w:.\-]+)['"]""", s):
            name = m.group(1)
            reg_any.add(name)
            if '/server/' in posix:
                reg_server.add(name)
            elif '/client/' in posix:
                reg_client.add(name)

    # Events the game engine raises; nobody registers them in this repo.
    BUILTIN = {
        'onResourceStop', 'onResourceStart', 'onClientResourceStart', 'playerDropped',
        'playerJoining', 'playerConnecting', 'onServerResourceStart', 'gameEventTriggered',
        'esx:getSharedObject',
    }
    # Hooks Nyx fires on purpose for OTHER resources to consume. Deliberately unhandled here.
    OUTBOUND = {
        'chat:addMessage', 'chat:addSuggestion', 'nyx_hud:chatToggle',
        'nyx_dealership:purchased', 'nyx_dealership:keysGranted',
        'esx:getSharedObject',
    }

    for p, s in luasrc.items():
        for kind, pattern in (
            ('server', r"""TriggerServerEvent\(\s*['"]([\w:.\-]+)['"]"""),
            ('client', r"""TriggerClientEvent\(\s*['"]([\w:.\-]+)['"]"""),
            ('any',    r"""(?<!Server)(?<!Client)TriggerEvent\(\s*['"]([\w:.\-]+)['"]"""),
        ):
            for m in re.finditer(pattern, s):
                name = m.group(1)
                if name in BUILTIN or name in OUTBOUND:
                    continue
                pool = {'server': reg_server, 'client': reg_client, 'any': reg_any}[kind]
                if name not in pool:
                    line = s[:m.start()].count('\n') + 1
                    fail(f"{rel(p)}:{line}: event '{name}' is triggered but no {kind}-side handler registers it")

    # --------------------------------------------------------- NUI callbacks -----
    # Nyx.post('name') in a resource's web/ needs RegisterNUICallback('name') in it
    callbacks = collections.defaultdict(set)
    for p, s in luasrc.items():
        for m in re.finditer(r"""RegisterNUICallback\(\s*['"]([\w:.\-]+)['"]""", s):
            callbacks[resource_of(p)].add(m.group(1))

    for p, s in jssrc.items():
        res = resource_of(p)
        if res == 'nyx_lib':
            continue  # the shared runtime posts on behalf of whoever loads it
        for m in re.finditer(r"""Nyx\.post\(\s*['"]([\w:.\-]+)['"]""", s):
            name = m.group(1)
            if name not in callbacks[res]:
                line = s[:m.start()].count('\n') + 1
                fail(f"{rel(p)}:{line}: Nyx.post('{name}') has no RegisterNUICallback('{name}') in {res}")

    # --------------------------------------------------- NUI messages -> page ----
    # SendNUIMessage({ action = 'x' }) needs Nyx.on('x') in the same resource's page
    sent = collections.defaultdict(set)
    for p, s in luasrc.items():
        for m in re.finditer(r"""SendNUIMessage\(\s*\{\s*action\s*=\s*['"]([\w:.\-]+)['"]""", s):
            sent[resource_of(p)].add((m.group(1), p, s[:m.start()].count('\n') + 1))

    listened = collections.defaultdict(set)
    for p, s in jssrc.items():
        for m in re.finditer(r"""Nyx\.on\(\s*['"]([\w:.\-]+)['"]""", s):
            listened[resource_of(p)].add(m.group(1))

    # nyx.js handles this one centrally for every page.
    CENTRAL = {'nyx:theme'}

    for res, entries in sent.items():
        for name, p, line in entries:
            if name in CENTRAL:
                continue
            if name not in listened[res]:
                fail(f"{rel(p)}:{line}: SendNUIMessage action '{name}' has no Nyx.on('{name}') in {res}'s page")

    # ------------------------------------------------- manifest file coverage ----
    for manifest in sorted(RESOURCES.glob('*/fxmanifest.lua')):
        res = manifest.parent
        s = manifest.read_text()
        declared = set(re.findall(r"""['"]([\w./*\[\]@-]+\.(?:lua|html|css|js))['"]""", s))
        # every .lua under client/server/shared must appear in the manifest
        for f in sorted(res.rglob('*.lua')):
            name = f.relative_to(res).as_posix()
            if name == 'fxmanifest.lua':
                continue
            if name not in declared:
                fail(f"{rel(f)}: not referenced by {rel(manifest)}")
        for f in sorted(res.rglob('web/*.html')) + sorted(res.rglob('web/*.css')) + sorted(res.rglob('web/*.js')):
            name = f.relative_to(res).as_posix()
            if name not in declared:
                fail(f"{rel(f)}: not listed in {rel(manifest)}'s files{{}} block - NUI will 404 it")



# ===========================================================================
# Pass 3 - docs/api.md must not name an export that does not exist
# ===========================================================================
def pass_docs(lua_files):
    """docs/api.md is the contract other people write scripts against.

    An export documented but not defined is worse than an undocumented one:
    somebody writes code against it, gets a nil call in production, and has no
    reason to suspect the documentation.
    """
    api = DOCS / "api.md"
    if not api.is_file():
        fail("docs/api.md is missing")
        return

    defined = collections.defaultdict(set)
    for path in lua_files:
        parts = path.relative_to(RESOURCES).parts
        resource = parts[0]
        for match in re.finditer(r"""exports\(\s*['"]([A-Za-z_]\w*)['"]""", path.read_text(encoding="utf-8")):
            defined[resource].add(match.group(1))

    text = api.read_text(encoding="utf-8")

    # exports['res']:Name( in a fenced block or inline
    for match in re.finditer(r"""exports\[['"]([\w-]+)['"]\]:(\w+)\(""", text):
        resource, name = match.group(1), match.group(2)
        if resource not in defined:
            fail(f"docs/api.md: references unknown resource '{resource}'")
        elif name not in defined[resource]:
            fail(f"docs/api.md: documents exports['{resource}']:{name}() which is not defined anywhere")

    # | `Name(args)` | ... | rows, attributed to the nearest preceding heading
    resource = None
    for line in text.splitlines():
        heading = re.match(r"^##\s+(nyx_\w+)", line)
        if heading:
            resource = heading.group(1)
            continue
        if resource is None:
            continue
        row = re.match(r"^\|\s*`([A-Za-z_]\w*)\(", line)
        if row and row.group(1) not in defined.get(resource, set()):
            fail(f"docs/api.md: table row documents {resource}:{row.group(1)}() which is not defined")


# ===========================================================================
# Pass 4 - the accent must stay swappable
# ===========================================================================
def pass_theme(css_files):
    """Every tinted pixel has to derive from --nyx-accent-rgb.

    The moment someone hardcodes #ff2bd6 into a resource stylesheet, the
    in-game colour picker stops repainting that element and the suite looks
    broken on any accent but magenta. theme.css owns the literal; nothing else
    may repeat it.
    """
    default_accent = re.compile(r"#ff2bd6", re.IGNORECASE)

    for path in css_files:
        if path.name == "theme.css":
            continue  # the one legitimate home for the literal
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if default_accent.search(line):
                fail(f"{rel(path)}:{number}: hardcoded accent colour - "
                     f"use rgba(var(--nyx-accent-rgb), a) so the colour picker still works")


# ===========================================================================
def main():
    if not RESOURCES.is_dir():
        print(f"cannot find {RESOURCES}", file=sys.stderr)
        return 2

    lua_files = sorted(RESOURCES.rglob("*.lua"))
    js_files = sorted(RESOURCES.rglob("*.js"))
    css_files = sorted(RESOURCES.rglob("*.css"))
    manifests = sorted(RESOURCES.glob("*/fxmanifest.lua"))

    pass_lua_structure(lua_files)
    pass_wiring(lua_files, js_files)
    pass_docs(lua_files)
    pass_theme(css_files)

    print(f"nyx: {len(lua_files)} lua, {len(js_files)} js, {len(css_files)} css "
          f"across {len(manifests)} resources\n")

    if problems:
        for problem in problems:
            print("FAIL " + problem)
        print(f"\n{len(problems)} problem(s)")
        return 1

    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
