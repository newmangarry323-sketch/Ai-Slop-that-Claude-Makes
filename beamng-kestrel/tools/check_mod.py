#!/usr/bin/env python3
"""Checks the built mod the way the game will put it together.

    python3 tools/check_mod.py [--export physics.json]

For each configuration (.pc file) it assembles the vehicle from its parts,
following the slots, and then checks:

  * every node a beam, triangle, wheel, camera or engine setting names exists
  * powertrain devices connect from the engine down to the driven wheels
  * every flexbody mesh is in the .dae, every material is in the materials file
  * every $variable used is defined
  * masses, the centre of mass, and how close each node is to the limit where
    the physics step (1/2000 s) becomes unstable
  * a 3 second "drop test": the car is simulated settling onto the ground
    under gravity, to check it lands at the ride height it was drawn at and
    nothing collapses. Tyres are simplified to one spring per wheel.

It reads the generated files, not the generator, so it also catches mistakes
in the generator itself. It exits with status 1 if anything fails.
"""

import json
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VEH = os.path.join(HERE, "mod", "vehicles", "slop_kestrel")
EXTERNAL_PARTS = {"soundscape_horn_1"}   # parts that ship with the game itself
DT = 1 / 2000

failures = []
warnings = []


def fail(msg):
    failures.append(msg)


def warn(msg):
    warnings.append(msg)


def load_jsonish(path):
    text = open(path).read()
    text = re.sub(r"^\s*//.*$", "", text, flags=re.M)
    return json.loads(text)


def load_parts():
    parts = {}
    for name in sorted(os.listdir(VEH)):
        if name.endswith(".jbeam"):
            for k, v in load_jsonish(os.path.join(VEH, name)).items():
                if k in parts:
                    fail("part %s defined twice" % k)
                parts[k] = v
    return parts


def table_rows(section):
    """Yield (row dict, current modifiers) for a JBeam table, applying the
    {"key": value} modifier rows the way the game does."""
    header = [h.rstrip(":") for h in section[0]]
    mods = {}
    for row in section[1:]:
        if isinstance(row, dict):
            mods.update(row)
            continue
        entry = dict(zip(header, row))
        extra = row[len(header)] if len(row) > len(header) and isinstance(row[len(header)], dict) else {}
        props = dict(mods)
        props.update(extra)
        entry["_opts"] = extra
        yield entry, props


# ---------------------------------------------------------------- assemble ---

def assemble(parts, pc):
    chosen = []
    tree_vars = {}

    def visit(part_name, depth=0):
        if part_name in EXTERNAL_PARTS:
            return
        if part_name not in parts:
            fail("config %s uses unknown part %s" % (pc["_name"], part_name))
            return
        part = parts[part_name]
        chosen.append(part_name)
        for entry, _ in table_rows(part.get("slots", [["type", "default", "description"]])):
            slot = entry["type"]
            pick = pc["parts"].get(slot, entry["default"])
            if pick:
                if pick not in parts and pick not in EXTERNAL_PARTS:
                    fail("slot %s -> missing part %s" % (slot, pick))
                elif pick in parts and parts[pick]["slotType"] != slot:
                    fail("part %s has slotType %s but is used in slot %s"
                         % (pick, parts[pick]["slotType"], slot))
                visit(pick, depth + 1)

    visit(pc["mainPartName"])
    for name in chosen:
        for entry, _ in table_rows(parts[name].get("variables", [["name"]])):
            tree_vars[entry["name"]] = entry["default"]
    tree_vars.update(pc.get("vars", {}))
    return chosen, tree_vars


def evaluate(value, variables, where):
    if not isinstance(value, str) or not value.startswith("$"):
        return value
    if value.startswith("$="):
        expr = value[2:]
        for name in re.findall(r"\$[A-Za-z_][A-Za-z0-9_]*", expr):
            if name not in variables:
                fail("%s: undefined variable %s" % (where, name))
                return 0
        expr = re.sub(r"\$[A-Za-z_][A-Za-z0-9_]*", lambda m: repr(variables[m.group(0)]), expr)
        if not re.fullmatch(r"[0-9eE.+\-*/() ]+", expr):
            fail("%s: expression not understood: %s" % (where, value))
            return 0
        return eval(expr, {"__builtins__": {}})
    if value not in variables:
        fail("%s: undefined variable %s" % (where, value))
        return 0
    return variables[value]


def number(value, variables, where):
    v = evaluate(value, variables, where)
    if v == "FLT_MAX":
        return float("inf")
    return float(v)


class Vehicle:
    def __init__(self, parts, chosen, variables, name):
        self.name = name
        self.nodes = {}       # id -> dict(pos, mass, groups, props)
        self.beams = []       # dicts
        self.hydros = []
        self.triangles = []
        self.wheels = []
        self.flex = []
        self.powertrain = []
        self.parts = [parts[c] for c in chosen]
        self.variables = variables
        for part_name, part in zip(chosen, self.parts):
            self.read_part(part_name, part)

    def read_part(self, pname, part):
        V = self.variables
        if "nodes" in part:
            for e, p in table_rows(part["nodes"]):
                if e["id"] in self.nodes:
                    fail("%s: node %s defined twice (%s)" % (self.name, e["id"], pname))
                groups = p.get("group", "")
                groups = [g for g in (groups if isinstance(groups, list) else [groups]) if g]
                eg = p.get("engineGroup", "")
                eg = [g for g in (eg if isinstance(eg, list) else [eg]) if g]
                self.nodes[e["id"]] = {
                    "pos": (e["posX"], e["posY"], e["posZ"]),
                    "mass": number(p.get("nodeWeight", 25), V, pname),
                    "groups": groups, "engineGroups": eg, "part": pname,
                }
        for key, store in (("beams", self.beams), ("hydros", self.hydros)):
            if key in part:
                for e, p in table_rows(part[key]):
                    b = {"a": e["id1"], "b": e["id2"], "part": pname,
                         "type": p.get("beamType", "|NORMAL"),
                         "spring": number(p.get("beamSpring", 4300000), V, pname),
                         "damp": number(p.get("beamDamp", 580), V, pname),
                         "pre": number(p.get("beamPrecompression", 1), V, pname),
                         "short": number(p.get("beamShortBound", 1), V, pname),
                         "long": number(p.get("beamLongBound", 1), V, pname),
                         "limitSpring": number(p.get("beamLimitSpring", 0), V, pname),
                         "limitDamp": number(p.get("beamLimitDamp", 0), V, pname),
                         "name": p.get("name") if isinstance(p.get("name"), str) else None}
                    store.append(b)
        if "triangles" in part:
            for e, p in table_rows(part["triangles"]):
                self.triangles.append((e["id1"], e["id2"], e["id3"]))
        if "pressureWheels" in part:
            for e, p in table_rows(part["pressureWheels"]):
                w = dict(e)
                w["props"] = p
                self.wheels.append(w)
        if "flexbodies" in part:
            for e, _ in table_rows(part["flexbodies"]):
                self.flex.append((e["mesh"], e["[group]"], pname))
        if "powertrain" in part:
            for e, p in table_rows(part["powertrain"]):
                self.powertrain.append(dict(e, props=p, part=pname))

    def merged_section(self, key):
        out = {}
        for part in self.parts:
            if isinstance(part.get(key), dict):
                out.update(part[key])
        return out


# ------------------------------------------------------------------ checks ---

def check_vehicle(v, dae_meshes, dae_materials, material_defs):
    nodes = v.nodes

    def need(node, what):
        if node not in nodes:
            fail("%s: %s refers to missing node %s" % (v.name, what, node))

    named_beams = set()
    for b in v.beams + v.hydros:
        need(b["a"], "beam in " + b["part"])
        need(b["b"], "beam in " + b["part"])
        if b["a"] in nodes and b["b"] in nodes and math.dist(nodes[b["a"]]["pos"], nodes[b["b"]]["pos"]) < 1e-3:
            fail("%s: zero-length beam %s-%s" % (v.name, b["a"], b["b"]))
        if b["name"]:
            named_beams.add(b["name"])
    pairs = {}
    for b in v.beams:
        key = tuple(sorted((b["a"], b["b"])))
        pairs[key] = pairs.get(key, 0) + 1
    for key, count in pairs.items():
        if count > 1 and not (key[0].endswith("_lo") and key[1].endswith("_st")):
            warn("%s: %d beams between %s and %s" % (v.name, count, *key))
    for t in v.triangles:
        for n in t:
            need(n, "triangle")
    for part in v.parts:
        if "refNodes" in part:
            for n in part["refNodes"][1]:
                need(n, "refNodes")
        if "camerasInternal" in part:
            for e, _ in table_rows(part["camerasInternal"]):
                for k in ("id1", "id2", "id3", "id4", "id5", "id6"):
                    need(e[k], "camera")

    # wheels
    wheel_groups = set()
    wheel_names = set()
    for w in v.wheels:
        for k in ("node1", "node2", "nodeArm"):
            need(w[k], "wheel " + w["name"])
        if not any(k.endswith(":") for k in w["_opts"]):
            fail("%s: wheel %s has no torqueArm / steerAxis options" % (v.name, w["name"]))
        for k, n in w["_opts"].items():
            if k.endswith(":"):
                need(n, "wheel %s option %s" % (w["name"], k))
        for beam in w["props"].get("axleBeams", []):
            if beam not in named_beams:
                fail("%s: wheel %s axleBeam %s does not exist" % (v.name, w["name"], beam))
        if not w["props"].get("hasTire"):
            fail("%s: wheel %s has no tyre" % (v.name, w["name"]))
        if w["name"] in wheel_names:
            fail("%s: wheel %s defined twice" % (v.name, w["name"]))
        wheel_names.add(w["name"])
        wheel_groups.update((w["hubGroup"], w["group"]))
        # the tyre must sit between the axle nodes
        a, b = nodes[w["node1"]]["pos"], nodes[w["node2"]]["pos"]
        span = math.dist(a, b)
        width = number(w["props"]["tireWidth"], v.variables, "wheel")
        if span + 1e-6 < width * 0.9:
            warn("%s: wheel %s axle nodes %.3f m apart for a %.3f m tyre" % (v.name, w["name"], span, width))
    if len(wheel_names) != 4:
        fail("%s: expected 4 wheels, found %s" % (v.name, sorted(wheel_names)))

    # flexbodies
    groups = {g for n in nodes.values() for g in n["groups"]} | wheel_groups
    for mesh, gs, pname in v.flex:
        if mesh not in dae_meshes:
            fail("%s: flexbody mesh %s (%s) is not in the .dae" % (v.name, mesh, pname))
        for g in gs:
            if g not in groups:
                fail("%s: flexbody %s uses unknown group %s" % (v.name, mesh, g))
    for m in dae_materials:
        if m not in material_defs:
            fail("material %s is used by the .dae but not defined" % m)
        elif material_defs[m].get("mapTo") != m:
            fail("material %s: mapTo must equal the material name in the .dae" % m)

    # powertrain
    devices = {d["name"]: d for d in v.powertrain}
    for d in v.powertrain:
        if d["inputName"] != "dummy" and d["inputName"] not in devices:
            fail("%s: powertrain %s takes input from missing device %s" % (v.name, d["name"], d["inputName"]))
        cw = d["props"].get("connectedWheel")
        if cw and cw not in wheel_names:
            fail("%s: %s drives missing wheel %s" % (v.name, d["name"], cw))

    def reaches_engine(name, seen=()):
        if name == "mainEngine":
            return True
        d = devices.get(name)
        return bool(d) and name not in seen and reaches_engine(d["inputName"], seen + (name,))
    driven = [d["props"]["connectedWheel"] for d in v.powertrain
              if d["props"].get("connectedWheel") and reaches_engine(d["name"])]
    if sorted(driven) != ["RL", "RR"]:
        fail("%s: driven wheels are %s, expected RL and RR" % (v.name, driven))
    engine = v.merged_section("mainEngine")
    for n in engine.get("torqueReactionNodes:", []):
        need(n, "engine torqueReactionNodes")
    engine_groups = {g for n in nodes.values() for g in n["engineGroups"]}
    tank = v.merged_section("mainTank")
    for section, data in (("mainEngine", engine), ("mainTank", tank)):
        for key, val in data.items():
            if isinstance(val, dict) and "[engineGroup]:" in val:
                for g in val["[engineGroup]:"]:
                    if g not in engine_groups:
                        fail("%s: %s.%s uses missing engineGroup %s" % (v.name, section, key, g))
        trig = data.get("breakTriggerBeam")
        if trig and trig not in named_beams:
            fail("%s: %s breakTriggerBeam %s does not exist" % (v.name, section, trig))
    storages = [e["name"] for p in v.parts if "energyStorage" in p for e, _ in table_rows(p["energyStorage"])]
    if engine.get("energyStorage") not in storages:
        fail("%s: engine energyStorage %s missing" % (v.name, engine.get("energyStorage")))
    if engine.get("requiredEnergyType") != tank.get("energyType"):
        fail("%s: engine needs %s but the tank holds %s" % (v.name, engine.get("requiredEnergyType"), tank.get("energyType")))
    gearbox = v.merged_section("gearbox")
    for n in gearbox.get("gearboxNode:", []):
        need(n, "gearboxNode")

    # stability: a spring k on a mass m oscillates at w = sqrt(k/m); the
    # simple integrators used for these simulations misbehave once w*dt
    # gets near 2. Summing every spring on a node gives an upper bound.
    k_sum = {n: 0.0 for n in nodes}
    for b in v.beams + v.hydros:
        if b["a"] in k_sum and b["b"] in k_sum:
            k = b["spring"] + (b["limitSpring"] if b["type"] == "|BOUNDED" else 0)
            k_sum[b["a"]] += k
            k_sum[b["b"]] += k
    worst = max(nodes, key=lambda n: math.sqrt(k_sum[n] / nodes[n]["mass"]))
    wdt = math.sqrt(k_sum[worst] / nodes[worst]["mass"]) * DT
    if wdt > 1.6:
        fail("%s: node %s too light for its springs (w*dt = %.2f)" % (v.name, worst, wdt))

    # Floppy nodes: if all the stiff beams on a node lie in one plane (or one
    # line), nothing holds it in the other direction, like the middle of a
    # drum skin. Find each node's weakest direction with the other nodes held
    # still, and report its natural frequency that way. This only sees single
    # nodes; two loose nodes joined to each other (a pair that wobbles
    # together) pass it, and are left to the drop test below to find.
    stiff = {n: [[0.0] * 3 for _ in range(3)] for n in nodes}
    for b in v.beams + v.hydros:
        if b["spring"] < 1e6:
            continue   # suspension springs are meant to be soft
        pa, pb = nodes[b["a"]]["pos"], nodes[b["b"]]["pos"]
        d = [pb[k] - pa[k] for k in range(3)]
        L = math.sqrt(sum(x * x for x in d))
        u = [x / L for x in d]
        for n in (b["a"], b["b"]):
            for i in range(3):
                for j in range(3):
                    stiff[n][i][j] += b["spring"] * u[i] * u[j]
    weakest = []
    for n, K in stiff.items():
        hz = math.sqrt(max(min_eigenvalue(K), 0.0) / nodes[n]["mass"]) / (2 * math.pi)
        weakest.append((hz, n))
    weakest.sort()
    for hz, n in weakest:
        if hz < 20:
            fail("%s: node %s is barely held in one direction (%.1f Hz)" % (v.name, n, hz))
    return {"w_dt_worst": wdt, "w_dt_node": worst, "weakest": weakest[:3]}


def min_eigenvalue(K):
    """Smallest eigenvalue of a symmetric 3x3 matrix (closed form)."""
    a, b, c = K[0][0], K[1][1], K[2][2]
    d, e, f = K[0][1], K[1][2], K[0][2]
    p1 = d * d + e * e + f * f
    q = (a + b + c) / 3
    if p1 < 1e-12 * max(1.0, q * q):
        return min(a, b, c)
    p2 = (a - q) ** 2 + (b - q) ** 2 + (c - q) ** 2 + 2 * p1
    p = math.sqrt(p2 / 6)
    B = [[(K[i][j] - (q if i == j else 0)) / p for j in range(3)] for i in range(3)]
    detB = (B[0][0] * (B[1][1] * B[2][2] - B[1][2] * B[2][1])
            - B[0][1] * (B[1][0] * B[2][2] - B[1][2] * B[2][0])
            + B[0][2] * (B[1][0] * B[2][1] - B[1][1] * B[2][0]))
    phi = math.acos(max(-1.0, min(1.0, detB / 2))) / 3
    return q + 2 * p * math.cos(phi + 2 * math.pi / 3)


def read_dae(path):
    ns = {"c": "http://www.collada.org/2005/11/COLLADASchema"}
    root = ET.parse(path).getroot()
    meshes = {}
    materials = set()
    for geom in root.iterfind(".//c:geometry", ns):
        name = geom.get("name")
        src = {s.get("id"): [float(x) for x in s.find("c:float_array", ns).text.split()]
               for s in geom.iterfind(".//c:source", ns)}
        pos = src[name + "-mesh-positions"]
        nrm = src[name + "-mesh-normals"]
        tris = []
        for t in geom.iterfind(".//c:triangles", ns):
            mat = t.get("material").replace("-material", "")
            materials.add(mat)
            idx = [int(x) for x in t.find("c:p", ns).text.split()]
            for k in range(0, len(idx), 9):
                corners = [tuple(pos[3 * idx[k + 3 * c]:3 * idx[k + 3 * c] + 3]) for c in range(3)]
                normals = [tuple(nrm[3 * idx[k + 3 * c + 1]:3 * idx[k + 3 * c + 1] + 3]) for c in range(3)]
                tris.append((mat, corners, normals))
        meshes[name] = tris
    scene_nodes = {n.get("name") for n in root.iterfind(".//c:visual_scene/c:node", ns)}
    for m in meshes:
        if m not in scene_nodes:
            fail("mesh %s has no scene node with the same name" % m)
    return meshes, materials


def check_meshes(meshes, vehicle):
    """Faces point outwards, normals agree with winding, and every vertex has
    physics nodes near it to follow."""
    out_ok = out_bad = agree_bad = 0
    for name, tris in meshes.items():
        for mat, (a, b, c), ns in tris:
            n = cross(sub(b, a), sub(c, a))
            ln = math.sqrt(sum(x * x for x in n))
            if ln < 1e-12:
                continue
            avg = [sum(x[k] for x in ns) for k in range(3)]
            if sum(n[k] * avg[k] for k in range(3)) < 0:
                agree_bad += 1
            if name == "kestrel_body" and mat in ("kestrel_paint", "kestrel_glass"):
                centre = [(a[k] + b[k] + c[k]) / 3 for k in range(3)]
                ref = (0.0, max(-1.6, min(1.6, centre[1])), 0.55)
                if sum(n[k] * (centre[k] - ref[k]) for k in range(3)) > 0:
                    out_ok += 1
                else:
                    out_bad += 1
    if agree_bad:
        fail("%d triangles whose normals point against their winding" % agree_bad)
    if out_bad > 0.02 * (out_ok + out_bad):
        fail("%d of %d body triangles face inwards" % (out_bad, out_ok + out_bad))
    # vertices far from every node of their flexbody groups deform badly
    group_nodes = {}
    for nid, n in vehicle.nodes.items():
        for g in n["groups"]:
            group_nodes.setdefault(g, []).append(n["pos"])
    worst = 0.0
    for mesh, gs, _ in vehicle.flex:
        pts = [p for g in gs for p in group_nodes.get(g, [])]
        if not pts:
            continue   # wheel groups are made by the game
        for _mat, corners, _ns in meshes[mesh]:
            for c in corners:
                worst = max(worst, min(math.dist(c, p) for p in pts))
    if worst > 0.75:
        fail("a body vertex is %.2f m from the nearest body node" % worst)
    return {"body_faces_outward": out_ok, "body_faces_inward": out_bad,
            "max_vertex_to_node_m": worst}


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


# ---------------------------------------------------------------- drop test ---

def drop_test(v, seconds=3.0):
    """Let the car settle on flat ground and report where it ends up."""
    ids = list(v.nodes)
    index = {n: i for i, n in enumerate(ids)}
    pos = [list(v.nodes[n]["pos"]) for n in ids]
    mass = [v.nodes[n]["mass"] for n in ids]
    # Each wheel: its mass goes onto the two axle nodes, and the tyre is a
    # vertical spring between the wheel centre and the ground.
    tyres = []
    for w in v.wheels:
        i1, i2 = index[w["node1"]], index[w["node2"]]
        rays = w["props"]["numRays"]
        wm = 2 * rays * (w["props"]["hubNodeWeight"] + w["props"]["nodeWeight"])
        mass[i1] += wm / 2
        mass[i2] += wm / 2
        radius = w["props"]["radius"]
        tyres.append((i1, i2, radius))
    springs = []
    for b in v.beams + v.hydros:
        i, j = index[b["a"]], index[b["b"]]
        rest = math.dist(pos[i], pos[j])
        springs.append((i, j, rest * b["pre"], b["spring"], b["damp"], b["type"],
                        rest * (1 - b["short"]), rest * (1 + b["long"]), b["limitSpring"], b["limitDamp"], rest))
    n = len(ids)
    vel = [[0.0, 0.0, 0.0] for _ in range(n)]
    lift = 0.02   # start a little in the air
    for p in pos:
        p[2] += lift
    k_tyre = 180000.0     # N/m, about right for a 195/55 R15 at 30 psi
    c_tyre = 900.0
    c_grip = 3000.0       # N s/m, sideways and fore-aft, per axle node
    g = -9.81
    steps = int(seconds / DT)
    max_strain = 0.0
    for step in range(steps):
        force = [[0.0, 0.0, mass[i] * g] for i in range(n)]
        for (i, j, rest, k, c, typ, lo, hi, kl, cl, _) in springs:
            pi, pj = pos[i], pos[j]
            dx, dy, dz = pj[0] - pi[0], pj[1] - pi[1], pj[2] - pi[2]
            L = math.sqrt(dx * dx + dy * dy + dz * dz)
            ux, uy, uz = dx / L, dy / L, dz / L
            vi, vj = vel[i], vel[j]
            vrel = (vj[0] - vi[0]) * ux + (vj[1] - vi[1]) * uy + (vj[2] - vi[2]) * uz
            f = k * (L - rest) + c * vrel
            if typ == "|BOUNDED":
                if L < lo:
                    f += kl * (L - lo) + cl * vrel
                elif L > hi:
                    f += kl * (L - hi) + cl * vrel
            fi = force[i]
            fj = force[j]
            fi[0] += f * ux
            fi[1] += f * uy
            fi[2] += f * uz
            fj[0] -= f * ux
            fj[1] -= f * uy
            fj[2] -= f * uz
        for (i1, i2, radius) in tyres:
            zc = (pos[i1][2] + pos[i2][2]) / 2
            vz = (vel[i1][2] + vel[i2][2]) / 2
            pen = radius - zc
            if pen > 0:
                f = k_tyre * pen - c_tyre * vz
                force[i1][2] += f / 2
                force[i2][2] += f / 2
                # grip: without it the car could slide about on the ground
                for i in (i1, i2):
                    force[i][0] -= c_grip * vel[i][0]
                    force[i][1] -= c_grip * vel[i][1]
        for i in range(n):
            if pos[i][2] < 0:     # anything else touching the ground
                force[i][2] += -2e5 * pos[i][2] - 500 * vel[i][2]
            m = mass[i]
            vi = vel[i]
            fi = force[i]
            vi[0] += fi[0] / m * DT
            vi[1] += fi[1] / m * DT
            vi[2] += fi[2] / m * DT
            p = pos[i]
            p[0] += vi[0] * DT
            p[1] += vi[1] * DT
            p[2] += vi[2] * DT
        if any(abs(x) > 50 for p in pos[:5] for x in p):
            fail("%s: drop test blew up after %.3f s" % (v.name, step * DT))
            return {}
    for (i, j, rest, k, c, typ, lo, hi, kl, cl, drawn) in springs:
        if k > 1e6:
            max_strain = max(max_strain, abs(math.dist(pos[i], pos[j]) / drawn - 1))
    speed = max(math.sqrt(sum(x * x for x in vv)) for vv in vel)

    def z(name):
        return pos[index[name]][2]
    report = {"settle_speed_m_s": speed, "max_structure_strain": max_strain}
    for w in v.wheels:
        report["wheel_centre_z_" + w["name"]] = (z(w["node1"]) + z(w["node2"])) / 2
    drawn_body = {k: v.nodes[k]["pos"][2] for k in ("b2l", "b6l", "s0l", "s8l")}
    for k, z0 in drawn_body.items():
        report["body_%s_sag_mm" % k] = (z0 - z(k)) * 1000
    tyre_squash = [(radius - (pos[i1][2] + pos[i2][2]) / 2) * 1000 for (i1, i2, radius) in tyres]
    report["tyre_squash_mm_avg"] = sum(tyre_squash) / len(tyre_squash)
    if speed > 0.03:
        fail("%s: still moving at %.3f m/s after %.1f s" % (v.name, speed, seconds))
    if max_strain > 0.01:
        fail("%s: a stiff beam is stretched or squashed by %.1f%%" % (v.name, max_strain * 100))
    for k, sag in report.items():
        if k.startswith("body_") and abs(sag - report["tyre_squash_mm_avg"]) > 25:
            fail("%s: %s is %.0f mm off the drawn ride height" % (v.name, k, sag))
    return report


def export_physics(v, path):
    """Nodes and beams of one configuration, for tools/preview."""
    def kind(b):
        names = (b["a"], b["b"])
        if b in v.hydros:
            return "hydro"
        if b["spring"] < 1e6:
            return "spring"
        if any(n[:3] in ("fl_", "fr_", "rl_", "rr_") for n in names):
            return "suspension"
        if any(n[0] in "eg" and n[1].isdigit() for n in names) or "diff_r" in names:
            return "powertrain"
        return "body"
    data = {"nodes": {n: d["pos"] for n, d in v.nodes.items()},
            "beams": [[b["a"], b["b"], kind(b)] for b in v.beams + v.hydros],
            "wheels": [[w["node1"], w["node2"], w["props"]["radius"]] for w in v.wheels]}
    with open(path, "w") as f:
        json.dump(data, f)


def main():
    export = sys.argv[sys.argv.index("--export") + 1] if "--export" in sys.argv else None
    parts = load_parts()
    material_defs = json.load(open(os.path.join(VEH, "main.materials.json")))
    meshes, dae_materials = read_dae(os.path.join(VEH, "kestrel.dae"))
    configs = sorted(f for f in os.listdir(VEH) if f.endswith(".pc"))
    info = json.load(open(os.path.join(VEH, "info.json")))
    if info.get("default_pc") + ".pc" not in configs:
        fail("info.json default_pc %s has no .pc file" % info.get("default_pc"))
    for cfg in configs:
        pc = json.load(open(os.path.join(VEH, cfg)))
        pc["_name"] = cfg
        if not os.path.exists(os.path.join(VEH, "info_" + cfg[:-3] + ".json")):
            warn("no info file for " + cfg)
        chosen, variables = assemble(parts, pc)
        vehicle = Vehicle(parts, chosen, variables, cfg)
        if export and cfg == configs[0]:
            export_physics(vehicle, export)
        stats = check_vehicle(vehicle, meshes, dae_materials, material_defs)
        total = sum(n["mass"] for n in vehicle.nodes.values())
        wheels = sum(2 * w["props"]["numRays"] * (w["props"]["hubNodeWeight"] + w["props"]["nodeWeight"])
                     for w in vehicle.wheels)
        print("== %s: %d parts, %d nodes, %d beams, %d hydros, %d triangles, %d wheels"
              % (cfg, len(chosen), len(vehicle.nodes), len(vehicle.beams), len(vehicle.hydros),
                 len(vehicle.triangles), len(vehicle.wheels)))
        print("   mass %.0f kg (+ %.0f kg of wheels made by the game) = %.0f kg"
              % (total, wheels, total + wheels))
        print("   stability: worst node %s, w*dt = %.2f (must stay well under 2)"
              % (stats["w_dt_node"], stats["w_dt_worst"]))
        print("   weakest-held nodes: " + ", ".join("%s %.0f Hz" % (n, hz) for hz, n in stats["weakest"]))
        if cfg == configs[0]:
            for k, val in check_meshes(meshes, vehicle).items():
                print("   mesh %s = %s" % (k, round(val, 3) if isinstance(val, float) else val))
        print("   drop test:")
        for k, val in drop_test(vehicle).items():
            print("     %-28s %.4f" % (k, val))
    for w in warnings:
        print("warning:", w)
    for f in failures:
        print("FAIL:", f)
    print("%d failures, %d warnings" % (len(failures), len(warnings)))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
