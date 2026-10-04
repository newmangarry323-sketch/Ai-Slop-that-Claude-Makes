#!/usr/bin/env python3
"""Builds the Slopworks Kestrel, a car mod for BeamNG.drive.

    python3 tools/build_mod.py

writes the mod into mod/vehicles/slop_kestrel/ and packs it as
slop_kestrel.zip. Everything (physics, meshes, materials, configs) comes from
this script and the two modules next to it, so change a number here and
rebuild rather than editing the generated files.
"""

import json
import math
import os
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mesh as meshlib  # noqa: E402
from jbeam_writer import Comment as C  # noqa: E402
from jbeam_writer import dumps  # noqa: E402
from shape import (AXLE_Z, AXLES, HUB_R, HUB_W, TIRE_W, WHEEL_R,  # noqa: E402
                   section)

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL = "slop_kestrel"
MOD_ROOT = os.path.join(HERE, "mod")
VEH = os.path.join(MOD_ROOT, "vehicles", MODEL)
ZIP_PATH = os.path.join(HERE, MODEL + ".zip")
AUTHORS = "newmangarry323-sketch & Claude"

# Every node we define, so later parts can measure distances.
POS = {}
MASS = {}


def r(v):
    return round(v, 4)


def dist(a, b):
    return math.dist(POS[a], POS[b])


def add_nodes(rows, names_pos, weight):
    rows.append({"nodeWeight": weight})
    for name, p in names_pos:
        # Alternative parts for one slot (e.g. comfort and sport suspension)
        # may define the same node, but only in the same place.
        if name in POS and math.dist(POS[name], p) > 1e-9:
            raise ValueError("node %s defined twice in different places" % name)
        POS[name] = p
        MASS[name] = weight
        rows.append([name, r(p[0]), r(p[1]), r(p[2])])


def lr(side):
    return "l" if side > 0 else "r"


SIDES = (1, -1)

# ===================================================================== body ===
#
# The body is a cage of nodes on 9 cross-sections ("stations"). On every
# station there is a node at the underbody edge (b), the sill (s) and the
# shoulder (t) on each side, and the three stations under the roof also get a
# roof-edge node (r). Beams join them into triangulated boxes, which is what
# makes the cage stiff: a box with diagonals on every face cannot fold.

STATIONS = [-2.00, -1.70, -0.90, -0.50, -0.05, 0.40, 0.70, 1.50, 1.96]
ROOF_STATIONS = {4, 5, 6}
FRONT_CRUMPLE = {0, 1}   # stations of the soft front end
REAR_CRUMPLE = {7, 8}
PROFILE_INDEX = {"b": 1, "s": 2, "t": 3, "r": 5}
BODY_WEIGHT = {"b": 8.5, "s": 8.0, "t": 6.5, "r": 6.5, "c": 5.0, "f": 5.5}
# Two more nodes sit on the centreline of every station: the crown of the
# bonnet / roof / boot (c) and the floor tunnel (f). They have no side.
CENTRE_KINDS = "cf"


def body_name(kind, i, side=0):
    if kind in CENTRE_KINDS:
        return "%s%d" % (kind, i)
    return "%s%d%s" % (kind, i, lr(side))


def body_pos(kind, i, side=0):
    p = section(STATIONS[i])
    if kind == "c":
        return (0.0, STATIONS[i], p[6][1] + 0.03)   # + the crown the mesh has
    if kind == "f":
        return (0.0, STATIONS[i], p[0][1])
    x, z = p[PROFILE_INDEX[kind]]
    return (side * x, STATIONS[i], z)


def body_kinds(i):
    return "bstr" if i in ROOF_STATIONS else "bst"


def body_beam_pairs():
    pairs = []

    def add(a, b):
        if (a, b) not in pairs and (b, a) not in pairs:
            pairs.append((a, b))

    n = len(STATIONS)
    for i in range(n):
        L = {k: body_name(k, i, 1) for k in body_kinds(i)}
        R = {k: body_name(k, i, -1) for k in body_kinds(i)}
        # across the car
        for k in body_kinds(i):
            add(L[k], R[k])
        add(L["b"], R["t"])
        add(R["b"], L["t"])
        add(L["b"], R["s"])
        add(R["b"], L["s"])
        for S in (L, R):
            add(S["b"], S["s"])
            add(S["s"], S["t"])
            add(S["b"], S["t"])
        if "r" in L:
            add(L["r"], R["t"])
            add(R["r"], L["t"])
            add(L["r"], L["t"])
            add(R["r"], R["t"])
        top = "r" if "r" in L else "t"
        c, f = body_name("c", i), body_name("f", i)
        add(c, L[top])
        add(c, R[top])
        add(f, L["b"])
        add(f, R["b"])
        add(c, f)
        # Beams lying flat in a plane can't hold a node up or down (like the
        # middle of a drum skin), so brace the centre nodes diagonally too.
        add(f, L["t"])
        add(f, R["t"])
        add(c, L["s"])
        add(c, R["s"])
    for i in range(n - 1):
        top_i = "r" if i in ROOF_STATIONS else "t"
        top_j = "r" if i + 1 in ROOF_STATIONS else "t"
        c0, c1 = body_name("c", i), body_name("c", i + 1)
        f0, f1 = body_name("f", i), body_name("f", i + 1)
        add(c0, c1)
        add(f0, f1)
        for s in SIDES:
            add(c0, body_name(top_j, i + 1, s))
            add(body_name(top_i, i, s), c1)
            add(f0, body_name("b", i + 1, s))
            add(body_name("b", i, s), f1)
    for i in range(n - 1):
        for s in SIDES:
            o = -s
            for k in "bst":
                add(body_name(k, i, s), body_name(k, i + 1, s))
            add(body_name("b", i, s), body_name("s", i + 1, s))
            add(body_name("s", i, s), body_name("b", i + 1, s))
            add(body_name("s", i, s), body_name("t", i + 1, s))
            add(body_name("t", i, s), body_name("s", i + 1, s))
            # floor and deck diagonals
            add(body_name("b", i, s), body_name("b", i + 1, o))
            add(body_name("t", i, s), body_name("t", i + 1, o))
    # roof and pillars
    roof = sorted(ROOF_STATIONS)
    for i in roof:
        for s in SIDES:
            add(body_name("r", i, s), body_name("t", i - 1, s))
            add(body_name("r", i, s), body_name("t", i + 1, s))
            add(body_name("r", i, s), body_name("t", i - 1, -s))
            add(body_name("r", i, s), body_name("t", i + 1, -s))
    for i, j in zip(roof, roof[1:]):
        for s in SIDES:
            add(body_name("r", i, s), body_name("r", j, s))
            add(body_name("r", i, s), body_name("r", j, -s))
    return pairs


def body_triangles():
    """Outer skin as triangles: used for aerodynamics and for collisions."""
    quads = []
    n = len(STATIONS)
    for i in range(n - 1):
        for s in SIDES:
            quads.append((body_name("b", i, s), body_name("b", i + 1, s),
                          body_name("s", i + 1, s), body_name("s", i, s)))
            quads.append((body_name("s", i, s), body_name("s", i + 1, s),
                          body_name("t", i + 1, s), body_name("t", i, s)))
        # floor, and top: bonnet, windscreen, roof, rear window, boot lid
        top_i = "r" if i in ROOF_STATIONS else "t"
        top_j = "r" if i + 1 in ROOF_STATIONS else "t"
        for s in SIDES:
            quads.append((body_name("b", i, s), body_name("b", i + 1, s),
                          body_name("f", i + 1), body_name("f", i)))
            quads.append((body_name(top_i, i, s), body_name(top_j, i + 1, s),
                          body_name("c", i + 1), body_name("c", i)))
        if top_i == "r" or top_j == "r":
            for s in SIDES:   # side windows / pillars
                if top_i == "r" and top_j == "r":
                    quads.append((body_name("t", i, s), body_name("t", i + 1, s),
                                  body_name("r", i + 1, s), body_name("r", i, s)))
                elif top_j == "r":
                    quads.append((body_name("t", i, s), body_name("t", i + 1, s),
                                  body_name("r", i + 1, s)))
                else:
                    quads.append((body_name("t", i, s), body_name("t", i + 1, s),
                                  body_name("r", i, s)))
    for i in (0, n - 1):   # nose and tail
        ring = ([body_name("f", i)] + [body_name(k, i, 1) for k in "bst"] + [body_name("c", i)]
                + [body_name(k, i, -1) for k in "tsb"])
        quads.append(tuple(ring))
    tris = []
    for q in quads:
        for k in range(1, len(q) - 1):
            tris.append(orient((q[0], q[k], q[k + 1])))
    return tris


def orient(tri):
    """Wind a triangle so its normal points out of the car."""
    a, b, c = (POS[t] for t in tri)
    nrm = meshlib.cross(meshlib.sub(b, a), meshlib.sub(c, a))
    centre = [(a[k] + b[k] + c[k]) / 3 for k in range(3)]
    ref = (0.0, max(-1.5, min(1.5, centre[1])), 0.55)
    out = meshlib.sub(centre, ref)
    if sum(nrm[k] * out[k] for k in range(3)) < 0:
        return (tri[0], tri[2], tri[1])
    return tri


def part_body():
    nodes = [["id", "posX", "posY", "posZ"],
             C("The body cage. Lower nodes are heavier: the floor carries most of the mass."),
             {"nodeMaterial": "|NM_METAL"}, {"frictionCoef": 0.6},
             {"collision": True}, {"selfCollision": True},
             {"group": "kestrel_body"}]
    for kind in "bstrcf":
        nodes.append(C({"b": "underbody edge", "s": "sill", "t": "shoulder", "r": "roof edge",
                        "c": "centre of bonnet / roof / boot", "f": "floor tunnel"}[kind]))
        if kind in CENTRE_KINDS:
            names = [(body_name(kind, i), body_pos(kind, i)) for i in range(len(STATIONS))]
        else:
            names = [(body_name(kind, i, s), body_pos(kind, i, s))
                     for i in range(len(STATIONS)) if kind in body_kinds(i) for s in SIDES]
        add_nodes(nodes, names, BODY_WEIGHT[kind])
    nodes.append({"group": ""})

    pairs = body_beam_pairs()

    def station(n):
        return int("".join(ch for ch in n if ch.isdigit()))

    def crumple(a, b):
        return ({station(a), station(b)} <= FRONT_CRUMPLE) or ({station(a), station(b)} <= REAR_CRUMPLE)

    beams = [["id1:", "id2:"],
             {"beamType": "|NORMAL", "beamPrecompression": 1, "beamLongBound": 1, "beamShortBound": 1},
             {"beamSpring": 3001000, "beamDamp": 150},
             {"deformLimitExpansion": 1.2},
             C("Main cage. beamDeform is the force (N) where a beam starts to bend for good,"),
             C("beamStrength the force where it snaps."),
             {"beamDeform": 110000, "beamStrength": 400000}]
    beams += [[a, b] for a, b in pairs if not crumple(a, b)]
    beams += [C("Crumple zones: the nose and tail give way first and soak up a crash."),
              {"beamDeform": 45000, "beamStrength": 200000}]
    beams += [[a, b] for a, b in pairs if crumple(a, b)]
    beams += [{"deformLimitExpansion": ""}]

    triangles = [["id1:", "id2:", "id3:"],
                 C("The skin: air drag and collisions with other vehicles use these."),
                 {"dragCoef": 6}, {"group": ""}]
    triangles += [list(t) for t in body_triangles()]

    eye = (0.36, 0.30, 1.05)  # driver sits on the left (+X)
    return {
        "information": {"authors": AUTHORS, "name": "Kestrel Coupe Body", "value": 6000},
        "slotType": "kestrel_body",
        "refNodes": [["ref:", "back:", "left:", "up:", "leftCorner:", "rightCorner:"],
                     [body_name("b", 5, -1), body_name("b", 6, -1), body_name("b", 5, 1),
                      body_name("t", 5, -1), body_name("s", 0, 1), body_name("s", 0, -1)]],
        "cameraExternal": {"distance": 5.2, "distanceMin": 2.5,
                           "offset": {"x": 0, "y": 0.3, "z": 0.45}, "fov": 65},
        "camerasInternal": [
            ["type", "x", "y", "z", "fov", "id1:", "id2:", "id3:", "id4:", "id5:", "id6:"],
            {"nodeWeight": 1.5}, {"beamSpring": 50000, "beamDamp": 100},
            {"selfCollision": False}, {"collision": False},
            ["dash", eye[0], eye[1], eye[2], 65,
             body_name("t", 4, 1), body_name("t", 4, -1), body_name("t", 5, 1),
             body_name("t", 5, -1), body_name("b", 5, 1), body_name("b", 5, -1),
             {"beamDeform": 5001000, "beamStrength": "FLT_MAX"}],
        ],
        "flexbodies": [["mesh", "[group]:", "nonFlexMaterials"],
                       ["kestrel_body", ["kestrel_body"]]],
        "nodes": nodes,
        "beams": beams,
        "triangles": triangles,
    }


# =============================================================== suspension ===
#
# Double wishbones at every corner. The upright (the part the wheel bolts to)
# is 6 nodes held rigid by beams. Five links tie it to the body: two for the
# lower wishbone, two for the upper, and a tie rod. A rigid body has 6 ways
# to move; 5 links leave exactly one, up and down, and the spring and damper
# control that one.

def corner_layout(axle, side):
    ya, s = AXLES[axle], side
    return {
        # mounting points on the body ("pickups")
        "laf": (s * 0.28, ya - 0.20, 0.17), "lar": (s * 0.28, ya + 0.20, 0.17),
        "uaf": (s * 0.38, ya - 0.15, 0.50), "uar": (s * 0.38, ya + 0.15, 0.50),
        "tr": (s * 0.30, ya + 0.14, 0.27),  # inner end of the tie rod
        "st": (s * 0.56, ya, 0.64),         # top of the spring and damper
        # the upright
        "ai": (s * 0.62, ya, AXLE_Z),       # inner axle node
        "ao": (s * 0.82, ya, AXLE_Z),       # outer axle node
        "u": (s * 0.60, ya, 0.49),          # upper ball joint
        "lo": (s * 0.645, ya, 0.13),        # lower ball joint
        "sa": (s * 0.62, ya + 0.14, AXLE_Z),  # steering arm, behind the axle
        "fh": (s * 0.62, ya - 0.12, AXLE_Z),  # front of the hub
    }


PICKUPS = ("laf", "lar", "uaf", "uar", "tr", "st")
UPRIGHT = ("ai", "ao", "u", "lo", "sa", "fh")
UPRIGHT_WEIGHT = {"ai": 5.0, "ao": 4.0, "u": 3.0, "lo": 4.0, "sa": 3.0, "fh": 3.0}
PICKUP_WEIGHT = 5.0
DIFF_POS, DIFF_MASS = (0.0, AXLES["R"], 0.30), 28.0


def cn(axle, side, key):
    return "%s%s_%s" % (axle.lower(), lr(side), key)


def bracketing_stations(axle):
    ya = AXLES[axle]
    before = max(i for i, y in enumerate(STATIONS) if y < ya)
    return before, before + 1


# Filled in by static_loads(): the spring force each corner must hold up.
SPRING = {}

# Suspension tunes: (spring N/m, damper N s/m) front and rear.
TUNES = {
    "": {"F": (40000, 2300), "R": (36000, 2100), "name": "Comfort"},
    "_sport": {"F": (56000, 3000), "R": (50000, 2800), "name": "Sport"},
}
BUMP_TRAVEL = 0.08   # m of wheel travel up from the resting height
DROOP_TRAVEL = 0.11  # m of wheel travel down


def motion_ratio(axle, side=1):
    """How far the spring compresses per metre the wheel moves up."""
    p = corner_layout(axle, side)
    axis_x = p["laf"][0]
    wheel_x = side * (abs(p["ai"][0]) + abs(p["ao"][0])) / 2
    lever = (p["lo"][0] - axis_x) / (wheel_x - axis_x)
    d = meshlib.norm(meshlib.sub(p["st"], p["lo"]))
    return lever * d[2]


def part_suspension(axle, tune):
    spring, damp = TUNES[tune][axle]
    a = axle
    sv, dv = "$spring_" + a, "$damp_" + a
    nodes = [["id", "posX", "posY", "posZ"],
             {"nodeMaterial": "|NM_METAL"}, {"frictionCoef": 0.5},
             {"collision": True}, {"selfCollision": False}, {"group": ""}]
    beams = [["id1:", "id2:"]]
    hydros = [["id1:", "id2:"]]
    before, after = bracketing_stations(a)
    body_near = [body_name(k, i, s) for i in (before, after) for k in "bst" for s in SIDES]

    for side in SIDES:
        p = corner_layout(a, side)
        corner = a + ("L" if side > 0 else "R")
        nodes.append(C("%s corner: body pickups" % corner))
        add_nodes(nodes, [(cn(a, side, k), p[k]) for k in PICKUPS], PICKUP_WEIGHT)
        nodes.append(C("%s corner: upright" % corner))
        nodes.append({"group": "wheelhub_" + corner})
        for k in ("ai", "ao"):
            add_nodes(nodes, [(cn(a, side, k), p[k])], UPRIGHT_WEIGHT[k])
        nodes.append({"group": ""})
        for k in ("u", "lo", "sa", "fh"):
            add_nodes(nodes, [(cn(a, side, k), p[k])], UPRIGHT_WEIGHT[k])

    def N(side, k):
        return cn(a, side, k)

    beams += [C("Pickups bolted to the body (5 beams each, so they can't move)."),
              {"beamType": "|NORMAL", "beamPrecompression": 1, "beamLongBound": 1, "beamShortBound": 1},
              {"beamSpring": 2001000, "beamDamp": 120},
              {"beamDeform": 120000, "beamStrength": 400000}]
    for side in SIDES:
        for k in PICKUPS:
            n = N(side, k)
            near = sorted(body_near, key=lambda b: math.dist(POS[n], body_pos_named(b)))[:5]
            beams += [[n, b] for b in near]
    beams.append(C("Subframe: pickups tied together and across the car."))
    for side in SIDES:
        for x, y in (("laf", "lar"), ("uaf", "uar"), ("laf", "uaf"), ("lar", "uar"),
                     ("laf", "uar"), ("lar", "uaf"), ("tr", "lar"), ("tr", "uar"),
                     ("st", "uaf"), ("st", "uar")):
            beams.append([N(side, x), N(side, y)])
    for k in ("laf", "lar", "uaf", "uar", "st"):
        beams.append([N(1, k), N(-1, k)])
    beams += [[N(1, "laf"), N(-1, "lar")], [N(-1, "laf"), N(1, "lar")]]

    beams += [C("Uprights: every node joined to every other, so they act as solid parts."),
              {"beamSpring": 2501000, "beamDamp": 100},
              {"beamDeform": "FLT_MAX", "beamStrength": "FLT_MAX"}]
    for side in SIDES:
        corner = a + ("L" if side > 0 else "R")
        for i, x in enumerate(UPRIGHT):
            for y in UPRIGHT[i + 1:]:
                row = [N(side, x), N(side, y)]
                if {x, y} == {"ai", "ao"}:
                    row.append({"name": "axle_" + corner})
                beams.append(row)

    beams += [C("Wishbones (and the rear toe links). They bend in a hard hit."),
              {"beamSpring": 3001000, "beamDamp": 120},
              {"beamDeform": 150000, "beamStrength": 400000}]
    for side in SIDES:
        beams += [[N(side, "lo"), N(side, "laf")], [N(side, "lo"), N(side, "lar")],
                  [N(side, "u"), N(side, "uaf")], [N(side, "u"), N(side, "uar")]]
        if a == "R":
            beams.append([N(side, "sa"), N(side, "tr")])

    mr = motion_ratio(a)
    beams.append(C("Springs. Their rest length is longer than drawn (precompression) so that"))
    beams.append(C("the car settles at the drawn ride height under its own weight."))
    beams += [{"beamSpring": sv, "beamDamp": 0},
              {"beamDeform": "FLT_MAX", "beamStrength": "FLT_MAX"}]
    for side in SIDES:
        length = dist(N(side, "lo"), N(side, "st"))
        force = SPRING[a]
        beams.append([N(side, "lo"), N(side, "st"),
                      {"beamPrecompression": "$=1 + %.1f / (%s * %.4f)" % (force, sv, length)}])
    beams += [{"beamPrecompression": 1},
              C("Dampers (shock absorbers)."),
              {"beamSpring": 0, "beamDamp": dv}]
    for side in SIDES:
        beams.append([N(side, "lo"), N(side, "st")])
    beams.append(C("Bump stops and droop limits: no force until the spring has moved too far."))
    length = dist(N(1, "lo"), N(1, "st"))
    beams += [{"beamType": "|BOUNDED",
               "beamShortBound": r(BUMP_TRAVEL * mr / length),
               "beamLongBound": r(DROOP_TRAVEL * mr / length)},
              {"beamSpring": 0, "beamDamp": 0},
              {"beamLimitSpring": 600000, "beamLimitDamp": 3000}]
    for side in SIDES:
        beams.append([N(side, "lo"), N(side, "st")])
    beams += [{"beamType": "|NORMAL", "beamLongBound": 1, "beamShortBound": 1},
              {"beamLimitSpring": 0, "beamLimitDamp": 0}]

    part = {
        "information": {"authors": AUTHORS, "value": 1800 if tune else 1200,
                        "name": "%s %s Double Wishbone Suspension"
                        % (TUNES[tune]["name"], "Front" if a == "F" else "Rear")},
        "slotType": "kestrel_suspension_" + a,
        "variables": [
            ["name", "type", "unit", "category", "default", "min", "max", "title", "description"],
            [sv, "range", "N/m", "Suspension", spring, 15000, 120000, "Spring Rate",
             "Spring stiffness", {"stepDis": 500, "subCategory": "Front" if a == "F" else "Rear"}],
            [dv, "range", "N/m/s", "Suspension", damp, 500, 8000, "Damping",
             "Damper stiffness", {"stepDis": 50, "subCategory": "Front" if a == "F" else "Rear"}],
        ],
        "nodes": nodes,
        "beams": beams,
    }

    if a == "F":
        # Steering: the tie rods are hydros, beams that change length with the
        # steering input. Positive input means steer right; then the steering
        # arms (behind the axle) move to +X, which lengthens the left rod and
        # shortens the right one.
        hydros += [{"beamType": "|NORMAL", "beamPrecompression": 1, "beamLongBound": 1, "beamShortBound": 1},
                   {"beamSpring": 3001000, "beamDamp": 120},
                   {"beamDeform": 150000, "beamStrength": 400000}]
        for side in SIDES:
            length = dist(N(side, "tr"), N(side, "sa"))
            arm = abs(corner_layout(a, side)["sa"][1] - AXLES[a])
            factor = arm * math.sin(math.radians(33)) / length
            hydros.append([N(side, "tr"), N(side, "sa"),
                           {"factor": r(side * factor), "steeringWheelLock": 450,
                            "inRate": 1.25, "outRate": 1.25}])
        part["hydros"] = hydros
    else:
        # The rear differential's weight sits between the rear wheels.
        add_nodes(nodes, [("diff_r", DIFF_POS)], DIFF_MASS)
        beams += [C("Rear differential mount."),
                  {"beamSpring": 2001000, "beamDamp": 150},
                  {"beamDeform": 120000, "beamStrength": 400000}]
        for side in SIDES:
            for k in ("laf", "lar", "uaf", "uar"):
                beams.append(["diff_r", N(side, k)])
    return part


def body_pos_named(name):
    kind = name[0]
    if kind in CENTRE_KINDS:
        return body_pos(kind, int(name[1:]))
    return body_pos(kind, int(name[1:-1]), 1 if name[-1] == "l" else -1)


# =================================================================== wheels ===

WHEEL_NODE_COUNT = 16  # rays per wheel: 2 hub and 2 tyre nodes each
HUB_NODE_WEIGHT = 0.45
TIRE_NODE_WEIGHT = 0.22


def wheel_mass():
    return 2 * WHEEL_NODE_COUNT * (HUB_NODE_WEIGHT + TIRE_NODE_WEIGHT)


def part_wheels(axle):
    a = axle
    psi = "$tirepressure_" + a
    rows = [
        ["name", "hubGroup", "group", "node1:", "node2:", "nodeS", "nodeArm:", "wheelDir"],
        C("Rim. The game builds the wheel itself from these numbers: %d rays," % WHEEL_NODE_COUNT),
        C("each with 2 hub nodes and 2 tyre nodes, between the two axle nodes."),
        {"hubRadius": HUB_R}, {"hubWidth": HUB_W}, {"wheelOffset": 0}, {"numRays": WHEEL_NODE_COUNT},
        {"hubTreadBeamSpring": 1001000, "hubTreadBeamDamp": 10},
        {"hubPeripheryBeamSpring": 1001000, "hubPeripheryBeamDamp": 10},
        {"hubSideBeamSpring": 1501000, "hubSideBeamDamp": 10},
        {"hubNodeWeight": HUB_NODE_WEIGHT}, {"hubNodeMaterial": "|NM_METAL"}, {"hubFrictionCoef": 0.5},
        {"hubBeamDeform": 115000, "hubBeamStrength": 135000},
        C("Tyre: 195/55 R15. Air pressure pushes on the tyre's triangles."),
        {"hasTire": True},
        {"enableTireReinfBeams": False}, {"enableTireLbeams": True}, {"enableTireSideReinfBeams": False},
        {"enableTreadReinfBeams": True}, {"enableTirePeripheryReinfBeams": True},
        {"radius": WHEEL_R}, {"tireWidth": TIRE_W},
        {"wheelSideBeamSpring": "$=%s*875" % psi, "wheelSideBeamDamp": 65},
        {"wheelSideBeamSpringExpansion": 461000, "wheelSideBeamDampExpansion": 65},
        {"wheelSideTransitionZone": 0.08, "wheelSideBeamPrecompression": 0.99},
        {"wheelReinfBeamSpring": 23100, "wheelReinfBeamDamp": 227},
        {"wheelReinfBeamDampCutoffHz": 500, "wheelReinfBeamPrecompression": 0.985},
        {"wheelTreadBeamSpring": 101000, "wheelTreadBeamDamp": 85},
        {"wheelTreadBeamDampCutoffHz": 500, "wheelTreadBeamPrecompression": 0.985},
        {"wheelTreadReinfBeamSpring": 198000, "wheelTreadReinfBeamDamp": 85},
        {"wheelTreadReinfBeamDampCutoffHz": 500, "wheelTreadReinfBeamPrecompression": 0.985},
        {"wheelPeripheryBeamSpring": 95000, "wheelPeripheryBeamDamp": 65},
        {"wheelPeripheryBeamDampCutoffHz": 500, "wheelPeripheryBeamPrecompression": 0.985},
        {"wheelPeripheryReinfBeamSpring": 165000, "wheelPeripheryReinfBeamDamp": 65},
        {"wheelPeripheryReinfBeamDampCutoffHz": 500, "wheelPeripheryReinfBeamPrecompression": 0.985},
        {"nodeWeight": TIRE_NODE_WEIGHT}, {"nodeMaterial": "|NM_RUBBER"},
        {"pressurePSI": psi}, {"dragCoef": 5},
        C("Grip"),
        {"frictionCoef": 1.0}, {"slidingFrictionCoef": 1.05},
        {"stribeckExponent": 1.64}, {"stribeckVelocity": 1.28}, {"treadCoef": 0.9},
        {"noLoadCoef": 1.52}, {"loadSensitivitySlope": 0.000081}, {"fullLoadCoef": 0.56},
        {"softnessCoef": 0.99},
        {"wheelSideBeamDeform": 22000, "wheelSideBeamStrength": 22500},
        {"wheelTreadBeamDeform": 22000, "wheelTreadBeamStrength": 22000},
        {"wheelPeripheryBeamDeform": 50000, "wheelPeripheryBeamStrength": 53000},
        C("Brakes"),
        {"brakeTorque": "$brake_" + a},
        {"parkingTorque": "$parkbrake" if a == "R" else 0},
        {"brakeInputSplit": 1}, {"brakeSplitCoef": 1}, {"brakeSpring": 100},
        {"enableBrakeThermals": True}, {"brakeDiameter": 0.26 if a == "F" else 0.24},
        {"brakeMass": 6 if a == "F" else 5}, {"brakeType": "vented-disc"},
        {"rotorMaterial": "steel"}, {"brakeVentingCoef": 1.0},
        {"squealCoefNatural": 0.0, "squealCoefLowSpeed": 0.0},
        C("The wheels. node1 = outer axle node, node2 = inner; wheelDir is -1 on the left."),
        {"selfCollision": False}, {"collision": True}, {"enableHubcaps": False},
        {"disableMeshBreaking": False}, {"disableHubMeshBreaking": False},
    ]
    flex = [["mesh", "[group]:", "nonFlexMaterials"]]
    for side in SIDES:
        corner = a + ("L" if side > 0 else "R")

        def N(k):
            return cn(a, side, k)

        rows.append({"axleBeams": ["axle_" + corner]})
        rows.append([corner, "wheel_" + corner, "tire_" + corner, N("ao"), N("ai"), 9999, N("sa"),
                     -1 if side > 0 else 1,
                     {"torqueCoupling:": N("sa"), "torqueArm:": N("fh"), "torqueArm2:": N("ao"),
                      "steerAxisUp:": N("u"), "steerAxisDown:": N("lo")}])
        flex.append(["kestrel_rim_" + corner, ["wheel_" + corner, "wheelhub_" + corner]])
        flex.append(["kestrel_tire_" + corner, ["tire_" + corner, "wheel_" + corner]])
    rows += [{"axleBeams": []}, {"selfCollision": True}]
    where = "Front" if a == "F" else "Rear"
    variables = [
        ["name", "type", "unit", "category", "default", "min", "max", "title", "description"],
        [psi, "range", "psi", "Wheels", 30, 15, 50, "Tyre Pressure", "Starting tyre pressure",
         {"stepDis": 0.5, "subCategory": where}],
        ["$brake_" + a, "range", "N m", "Brakes", 1600 if a == "F" else 1000, 300, 4000,
         "Brake Torque", "Maximum braking torque per wheel", {"stepDis": 10, "subCategory": where}],
    ]
    if a == "R":
        variables.append(["$parkbrake", "range", "N m", "Brakes", 1400, 0, 4000, "Handbrake Torque",
                          "Handbrake torque per rear wheel", {"stepDis": 10}])
    return {
        "information": {"authors": AUTHORS, "value": 600,
                        "name": "15x6 Five-Spoke %s Wheels with 195/55 R15 Tyres" % where},
        "slotType": "kestrel_wheels_" + a,
        "variables": variables,
        "flexbodies": flex,
        "pressureWheels": rows,
    }


# =============================================================== powertrain ===
#
# The powertrain is a chain of devices, each taking the output of the one
# named in "inputName":
#   mainEngine -> clutch -> gearbox -> driveshaft -> differential_R
#   -> wheelaxleRL / wheelaxleRR -> the wheels named "RL" and "RR".

ENGINE_NODES = {
    "e1l": (0.20, -1.55, 0.28), "e1r": (-0.20, -1.55, 0.28),
    "e2l": (0.20, -0.95, 0.28), "e2r": (-0.20, -0.95, 0.28),
    "e3l": (0.18, -1.25, 0.62), "e3r": (-0.18, -1.25, 0.62),
}

ENGINES = {
    # torque curve (rpm, N m) of a 1.8 litre twin-cam four
    "kestrel_engine_i4": {
        "name": "1.8 L Twin-Cam I4", "value": 3200, "maxRPM": 7200, "limiter": 7000,
        "torque": [(0, 0), (500, 95), (1000, 120), (2000, 148), (3000, 162), (4000, 170),
                   (4500, 172), (5000, 170), (5500, 164), (6000, 155), (6500, 142),
                   (7000, 126), (7500, 100)],
        "inertia": 0.12,
    },
    "kestrel_engine_i4_sport": {
        "name": "1.8 L Twin-Cam I4 (Sport Cams & Exhaust)", "value": 5200, "maxRPM": 7800,
        "limiter": 7600,
        "torque": [(0, 0), (500, 90), (1000, 115), (2000, 145), (3000, 165), (4000, 180),
                   (5000, 190), (5500, 192), (6000, 190), (6500, 182), (7000, 170),
                   (7500, 152), (8000, 120)],
        "inertia": 0.10,
    },
}


def part_engine(name):
    spec = ENGINES[name]
    nodes = [["id", "posX", "posY", "posZ"],
             {"nodeMaterial": "|NM_METAL"}, {"frictionCoef": 0.5},
             {"collision": True}, {"selfCollision": False},
             {"group": "kestrel_engine"},
             {"engineGroup": ["engine_block", "engine_intake"]}]
    add_nodes(nodes, list(ENGINE_NODES.items()), 21.5)
    nodes += [{"engineGroup": ""}, {"group": ""}]
    keys = list(ENGINE_NODES)
    beams = [["id1:", "id2:"],
             {"beamType": "|NORMAL", "beamPrecompression": 1, "beamLongBound": 1, "beamShortBound": 1},
             {"beamSpring": 4001000, "beamDamp": 150},
             {"beamDeform": "FLT_MAX", "beamStrength": "FLT_MAX"},
             C("Engine block")]
    beams += [[x, y] for i, x in enumerate(keys) for y in keys[i + 1:]]
    beams += [C("Engine mounts. If the one called \"engine\" snaps, the engine stops."),
              {"beamSpring": 1501000, "beamDamp": 150},
              {"beamDeform": 90000, "beamStrength": 250000}]
    mounts = []
    for side in SIDES:
        for e in ("e1", "e2"):
            en = e + lr(side)
            for k in ("laf", "lar"):
                mounts.append([en, cn("F", side, k)])
            mounts.append([en, body_name("b", 1 if e == "e1" else 2, side)])
        mounts.append(["e3" + lr(side), cn("F", side, "uaf")])
        mounts.append(["e3" + lr(side), cn("F", side, "uar")])
    mounts[0].append({"name": "engine"})
    beams += mounts

    torque = [["rpm", "torque"]] + [[a, b] for a, b in spec["torque"]]
    return {
        "information": {"authors": AUTHORS, "name": spec["name"], "value": spec["value"]},
        "slotType": "kestrel_engine",
        "slots": [["type", "default", "description"],
                  ["kestrel_transmission", "kestrel_transmission_5M", "Transmission"]],
        "powertrain": [["type", "name", "inputName", "inputIndex"],
                       ["combustionEngine", "mainEngine", "dummy", 0]],
        "mainEngine": {
            "torque": torque,
            "idleRPM": 850,
            "maxRPM": spec["maxRPM"],
            "hasRevLimiter": True,
            "revLimiterRPM": spec["limiter"],
            "revLimiterType": "timeBased",
            "revLimiterCutTime": 0.05,
            "inertia": spec["inertia"],
            "friction": 12,
            "dynamicFriction": 0.022,
            "engineBrakeTorque": 35,
            "burnEfficiency": 0.30,
            "energyStorage": "mainTank",
            "requiredEnergyType": "gasoline",
            "thermalsEnabled": False,
            "torqueReactionNodes:": ["e1l", "e1r", "e3l"],
            "waterDamage": {"[engineGroup]:": ["engine_intake"]},
            "engineBlock": {"[engineGroup]:": ["engine_block"]},
            "breakTriggerBeam": "engine",
            "uiName": "Engine",
            "soundConfig": "soundConfig",
        },
        "soundConfig": {"sampleName": "V6", "mainGain": -4, "intakeMuffling": 0.6},
        "vehicleController": {"highShiftUpRPM": spec["limiter"] - 300},
        "nodes": nodes,
        "beams": beams,
    }


GEAR_RATIOS = {"R": 3.40, 1: 3.35, 2: 2.05, 3: 1.40, 4: 1.06, 5: 0.84}


def part_transmission():
    nodes = [["id", "posX", "posY", "posZ"],
             {"nodeMaterial": "|NM_METAL"}, {"frictionCoef": 0.5},
             {"collision": True}, {"selfCollision": False}, {"group": ""}]
    add_nodes(nodes, [("g1", (0.0, -0.62, 0.30))], 22.0)
    add_nodes(nodes, [("g2", (0.0, -0.25, 0.27))], 18.0)
    beams = [["id1:", "id2:"],
             {"beamType": "|NORMAL", "beamPrecompression": 1, "beamLongBound": 1, "beamShortBound": 1},
             {"beamSpring": 2001000, "beamDamp": 150},
             {"beamDeform": 100000, "beamStrength": 300000}]
    beams += [["g1", e] for e in ("e2l", "e2r", "e3l", "e3r")]
    beams += [["g1", "g2"]]
    for side in SIDES:
        beams += [["g1", body_name("b", 3, side)], ["g2", body_name("b", 3, side)],
                  ["g2", body_name("b", 4, side)], ["g1", body_name("b", 2, side)],
                  # the floor nodes are all at one height, so add beams up to
                  # the shoulders or the gearbox could bounce up and down
                  ["g2", body_name("t", 3, side)]]
    g = GEAR_RATIOS
    return {
        "information": {"authors": AUTHORS, "name": "5-Speed Manual Gearbox", "value": 1100},
        "slotType": "kestrel_transmission",
        "slots": [["type", "default", "description"],
                  ["kestrel_differential_R", "kestrel_differential_R_open", "Rear Differential"]],
        "powertrain": [["type", "name", "inputName", "inputIndex"],
                       ["frictionClutch", "clutch", "mainEngine", 1],
                       ["manualGearbox", "gearbox", "clutch", 1],
                       ["shaft", "driveshaft", "gearbox", 1,
                        {"uiName": "Driveshaft", "friction": 0.5, "dynamicFriction": 0.0003}]],
        "clutch": {"uiName": "Clutch", "clutchMass": 5.0, "additionalEngineInertia": 0.02},
        "gearbox": {
            "uiName": "Gearbox",
            "gearRatios": [-g["R"], 0, g[1], g[2], g[3], g[4], g[5]],
            "friction": 1.0,
            "dynamicFriction": 0.0005,
            "torqueLossCoef": 0.01,
            "gearboxNode:": ["g1"],
        },
        "vehicleController": {"calculateOptimalLoadShiftPoints": True, "shiftDownRPMOffsetCoef": 1.2},
        "nodes": nodes,
        "beams": beams,
    }


def part_differential(kind):
    lsd = kind == "lsd"
    options = {"diffType": "open", "uiName": "Rear Differential", "defaultVirtualInertia": 0.25}
    if lsd:
        options = {"diffType": "lsd", "lsdPreload": 60, "lsdLockCoef": 0.2, "lsdRevLockCoef": 0.1,
                   "uiName": "Rear Differential", "defaultVirtualInertia": 0.25}
    return {
        "information": {"authors": AUTHORS, "value": 900 if lsd else 500,
                        "name": "Limited Slip Rear Differential" if lsd else "Open Rear Differential"},
        "slotType": "kestrel_differential_R",
        "variables": [
            ["name", "type", "unit", "category", "default", "min", "max", "title", "description"],
            ["$finaldrive", "range", ":1", "Differentials", 4.30 if lsd else 4.10, 2.5, 6.0,
             "Final Drive Ratio", "Gear ratio of the differential", {"stepDis": 0.01}],
        ],
        "powertrain": [
            ["type", "name", "inputName", "inputIndex"],
            ["differential", "differential_R", "driveshaft", 1, options],
            ["shaft", "wheelaxleRL", "differential_R", 1,
             {"connectedWheel": "RL", "uiName": "Rear Left Halfshaft", "friction": 0.2}],
            ["shaft", "wheelaxleRR", "differential_R", 2,
             {"connectedWheel": "RR", "uiName": "Rear Right Halfshaft", "friction": 0.2}],
        ],
        "differential_R": {"gearRatio": "$finaldrive", "friction": 2,
                           "dynamicFriction": 0.0005, "torqueLossCoef": 0.02},
    }


STARTING_FUEL_L = 30
PETROL_KG_PER_L = 0.74


def part_fueltank():
    nodes = [["id", "posX", "posY", "posZ"],
             {"nodeMaterial": "|NM_METAL"}, {"frictionCoef": 0.5},
             {"collision": True}, {"selfCollision": False}, {"group": ""},
             {"engineGroup": "fuel"}]
    tank = [("ft1l", (0.30, 0.48, 0.21)), ("ft1r", (-0.30, 0.48, 0.21)),
            ("ft2l", (0.30, 0.66, 0.21)), ("ft2r", (-0.30, 0.66, 0.21))]
    add_nodes(nodes, tank, 2.5)
    nodes.append({"engineGroup": ""})
    names = [n for n, _ in tank]
    beams = [["id1:", "id2:"],
             {"beamType": "|NORMAL", "beamPrecompression": 1, "beamLongBound": 1, "beamShortBound": 1},
             {"beamSpring": 1001000, "beamDamp": 80},
             {"beamDeform": 60000, "beamStrength": 200000}]
    beams += [[x, y] for i, x in enumerate(names) for y in names[i + 1:]]
    for n in names:
        side = 1 if n.endswith("l") else -1
        beams += [[n, body_name("b", 5, side)], [n, body_name("b", 6, side)],
                  [n, body_name("b", 5, -side)], [n, body_name("t", 5, side)]]
    beams[-1].append({"name": "fuelTank"})
    return {
        "information": {"authors": AUTHORS, "name": "45 L Fuel Tank", "value": 250},
        "slotType": "kestrel_fueltank",
        "variables": [["name", "type", "unit", "category", "default", "min", "max", "title", "description"],
                      ["$fuel", "range", "L", "Chassis", STARTING_FUEL_L, 0, 45, "Fuel Volume", "Fuel at the start",
                       {"stepDis": 0.5}]],
        "energyStorage": [["type", "name"], ["fuelTank", "mainTank"]],
        "mainTank": {"energyType": "gasoline", "fuelCapacity": 45, "startingFuelCapacity": "$fuel",
                     "fuel": {"[engineGroup]:": ["fuel"]}, "breakTriggerBeam": "fuelTank"},
        "nodes": nodes,
        "beams": beams,
    }


def part_main():
    return {
        "information": {"authors": AUTHORS, "name": "Slopworks Kestrel", "value": 14000},
        "slotType": "main",
        "slots": [
            ["type", "default", "description"],
            ["kestrel_body", "kestrel_body", "Body", {"coreSlot": True}],
            ["kestrel_suspension_F", "kestrel_suspension_F", "Front Suspension", {"coreSlot": True}],
            ["kestrel_suspension_R", "kestrel_suspension_R", "Rear Suspension", {"coreSlot": True}],
            ["kestrel_wheels_F", "kestrel_wheels_F", "Front Wheels"],
            ["kestrel_wheels_R", "kestrel_wheels_R", "Rear Wheels"],
            ["kestrel_engine", "kestrel_engine_i4", "Engine"],
            ["kestrel_fueltank", "kestrel_fueltank", "Fuel Tank", {"coreSlot": True}],
            ["soundscape_horn", "soundscape_horn_1", "Horn"],
        ],
        "controller": [["fileName"], ["vehicleController", {}]],
    }


# ============================================================ static loads ===

def static_loads():
    """Work out what each spring must hold, before the suspension is written.

    Takes the masses of the parts built so far, adds the suspension (from
    its layout), the wheels and the fuel, then splits the weight between the
    axles by the lever rule: the axle nearer the centre of mass carries more.
    """
    masses = dict((n, MASS[n]) for n in MASS)
    pos = dict(POS)
    for n in ("ft1l", "ft1r", "ft2l", "ft2r"):
        masses[n] += STARTING_FUEL_L * PETROL_KG_PER_L / 4
    for a in AXLES:
        for side in SIDES:
            p = corner_layout(a, side)
            for k in PICKUPS:
                masses[cn(a, side, k)] = PICKUP_WEIGHT
                pos[cn(a, side, k)] = p[k]
            for k in UPRIGHT:
                masses[cn(a, side, k)] = UPRIGHT_WEIGHT[k]
                pos[cn(a, side, k)] = p[k]
    masses["diff_r"], pos["diff_r"] = DIFF_MASS, DIFF_POS
    for a in AXLES:
        for side in SIDES:
            key = "wheel_" + a + lr(side)
            masses[key], pos[key] = wheel_mass(), (side * 0.72, AXLES[a], AXLE_Z)
    total = sum(masses.values())
    cg_y = sum(masses[n] * pos[n][1] for n in masses) / total
    cg_z = sum(masses[n] * pos[n][2] for n in masses) / total
    yf, yr = AXLES["F"], AXLES["R"]
    front = total * (yr - cg_y) / (yr - yf)
    rear = total - front
    unsprung = sum(UPRIGHT_WEIGHT.values()) + wheel_mass()
    info = {"total_kg": total, "cg_y": cg_y, "cg_z": cg_z, "front_kg": front, "rear_kg": rear}
    for a, axle_kg in (("F", front), ("R", rear)):
        corner_sprung = axle_kg / 2 - unsprung
        SPRING[a] = corner_sprung * 9.81 / motion_ratio(a)
        info["corner_sprung_kg_" + a] = corner_sprung
        info["spring_force_N_" + a] = SPRING[a]
        info["motion_ratio_" + a] = motion_ratio(a)
    return info


# ================================================================ materials ===

def material(name, color, rough=0.5, metal=0.0, clearcoat=0.0, translucent=False):
    stage = {"diffuseColor": list(color), "roughnessFactor": rough, "metallicFactor": metal}
    if clearcoat:
        stage["clearCoatFactor"] = clearcoat
        stage["clearCoatRoughnessFactor"] = 0.05
    return {
        "name": name, "mapTo": name, "class": "Material",
        "Stages": [stage, {}, {}, {}],
        "translucent": translucent,
        "translucentBlendOp": "LerpAlpha" if translucent else "None",
        "doubleSided": True,
        "dynamicCubemap": True,
        "materialTag0": "beamng",
        "version": 1.5,
    }


MATERIALS = {
    meshlib.PAINT: material(meshlib.PAINT, (0.85, 0.30, 0.06, 1), 0.25, 0.1, clearcoat=1),
    meshlib.GLASS: material(meshlib.GLASS, (0.06, 0.09, 0.11, 0.45), 0.05, 0.0, translucent=True),
    meshlib.TRIM: material(meshlib.TRIM, (0.03, 0.03, 0.03, 1), 0.6),
    meshlib.UNDER: material(meshlib.UNDER, (0.05, 0.05, 0.05, 1), 0.9),
    meshlib.HEADLIGHT: material(meshlib.HEADLIGHT, (0.95, 0.95, 0.85, 1), 0.1, 0.3),
    meshlib.TAILLIGHT: material(meshlib.TAILLIGHT, (0.75, 0.03, 0.03, 1), 0.15),
    meshlib.PLATE: material(meshlib.PLATE, (0.92, 0.92, 0.90, 1), 0.5),
    meshlib.BUMPER: material(meshlib.BUMPER, (0.12, 0.12, 0.13, 1), 0.7),
    meshlib.CHROME: material(meshlib.CHROME, (0.8, 0.8, 0.82, 1), 0.15, 1.0),
    meshlib.RIM: material(meshlib.RIM, (0.65, 0.66, 0.68, 1), 0.3, 1.0),
    meshlib.RIM_DARK: material(meshlib.RIM_DARK, (0.10, 0.10, 0.11, 1), 0.6, 0.5),
    meshlib.TIRE: material(meshlib.TIRE, (0.04, 0.04, 0.04, 1), 0.9),
    meshlib.INTERIOR: material(meshlib.INTERIOR, (0.10, 0.10, 0.11, 1), 0.8),
}


# =================================================================== output ===

HEADER = """Slopworks Kestrel - generated by beamng-kestrel/tools/build_mod.py.
Edit the generator and rebuild instead of changing this file by hand.
Axes: +X left, +Y backwards, +Z up. Lengths in metres, masses in kg,
forces in newtons, springs in N/m, dampers in N s/m."""


def write_jbeam(path, parts):
    with open(path, "w", newline="\n") as f:
        f.write(dumps(parts, HEADER))


def write_json(path, data):
    with open(path, "w", newline="\n") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def config(name, parts, vars_=None):
    pc = {"format": 2, "model": MODEL, "mainPartName": MODEL, "parts": parts}
    if vars_:
        pc["vars"] = vars_
    write_json(os.path.join(VEH, name + ".pc"), pc)


def build():
    if os.path.isdir(MOD_ROOT):
        shutil.rmtree(MOD_ROOT)
    os.makedirs(VEH)
    POS.clear()
    MASS.clear()

    # The springs must hold up everything else, so the suspension is made
    # last, once the masses of the other parts are known.
    body = part_body()
    engines = {name: part_engine(name) for name in ENGINES}
    trans = part_transmission()
    tank = part_fueltank()
    loads = static_loads()
    susp = {a + t: part_suspension(a, t) for t in TUNES for a in AXLES}

    write_jbeam(os.path.join(VEH, MODEL + ".jbeam"), {MODEL: part_main()})
    write_jbeam(os.path.join(VEH, "kestrel_body.jbeam"), {"kestrel_body": body})
    write_jbeam(os.path.join(VEH, "kestrel_suspension.jbeam"), {
        "kestrel_suspension_F": susp["F"], "kestrel_suspension_R": susp["R"],
        "kestrel_suspension_F_sport": susp["F_sport"], "kestrel_suspension_R_sport": susp["R_sport"]})
    write_jbeam(os.path.join(VEH, "kestrel_wheels.jbeam"), {
        "kestrel_wheels_F": part_wheels("F"), "kestrel_wheels_R": part_wheels("R")})
    write_jbeam(os.path.join(VEH, "kestrel_engine.jbeam"), engines)
    write_jbeam(os.path.join(VEH, "kestrel_drivetrain.jbeam"), {
        "kestrel_transmission_5M": trans,
        "kestrel_differential_R_open": part_differential("open"),
        "kestrel_differential_R_lsd": part_differential("lsd")})
    write_jbeam(os.path.join(VEH, "kestrel_fueltank.jbeam"), {"kestrel_fueltank": tank})

    meshes = meshlib.build_all()
    used = meshlib.write_dae(meshes, os.path.join(VEH, "kestrel.dae"))
    missing = [m for m in used if m not in MATERIALS]
    if missing:
        raise SystemExit("no material defined for " + ", ".join(missing))
    write_json(os.path.join(VEH, "main.materials.json"), {m: MATERIALS[m] for m in used})

    write_json(os.path.join(VEH, "info.json"), {
        "Name": "Kestrel",
        "Brand": "Slopworks",
        "Author": AUTHORS,
        "Type": "Car",
        "Body Style": "Coupe",
        "Country": "United Kingdom",
        "Derby Class": "Compact Car",
        "Description": "A small, light, rear-wheel-drive coupe from the made-up Slopworks "
                       "company, built entirely from code as a modding example.",
        "Years": {"min": 1994, "max": 1999},
        "default_pc": "kestrel_base",
        "paints": {},
    })
    base_parts = {
        "kestrel_body": "kestrel_body",
        "kestrel_suspension_F": "kestrel_suspension_F",
        "kestrel_suspension_R": "kestrel_suspension_R",
        "kestrel_wheels_F": "kestrel_wheels_F",
        "kestrel_wheels_R": "kestrel_wheels_R",
        "kestrel_engine": "kestrel_engine_i4",
        "kestrel_transmission": "kestrel_transmission_5M",
        "kestrel_differential_R": "kestrel_differential_R_open",
        "kestrel_fueltank": "kestrel_fueltank",
        "soundscape_horn": "soundscape_horn_1",
    }
    sport_parts = dict(base_parts)
    sport_parts.update({
        "kestrel_suspension_F": "kestrel_suspension_F_sport",
        "kestrel_suspension_R": "kestrel_suspension_R_sport",
        "kestrel_engine": "kestrel_engine_i4_sport",
        "kestrel_differential_R": "kestrel_differential_R_lsd",
    })
    config("kestrel_base", base_parts)
    config("kestrel_sport", sport_parts, {"$tirepressure_F": 32, "$tirepressure_R": 32,
                                           "$brake_F": 1900, "$brake_R": 1150})
    common = {"Drivetrain": "RWD", "Transmission": "Manual", "Fuel Type": "Gasoline",
              "Propulsion": "ICE", "Induction Type": "NA", "Body Style": "Coupe"}
    write_json(os.path.join(VEH, "info_kestrel_base.json"), dict(
        common, **{"Configuration": "Base", "Config Type": "Factory", "Value": 14000,
                   "Description": "1.8 litre four, five-speed manual, open differential, "
                                  "comfortable springs."}))
    write_json(os.path.join(VEH, "info_kestrel_sport.json"), dict(
        common, **{"Configuration": "Sport", "Config Type": "Factory", "Value": 17500,
                   "Description": "Hotter cams and exhaust, a limited-slip differential, "
                                  "shorter final drive, stiffer springs and stronger brakes."}))

    for name in ("default.jpg", "kestrel_base.jpg", "kestrel_sport.jpg"):
        src = os.path.join(HERE, "docs", "thumbnail.jpg")
        if os.path.exists(src):
            shutil.copy(src, os.path.join(VEH, name))

    write_json(os.path.join(HERE, "tools", "build_report.json"), loads)
    with zipfile.ZipFile(ZIP_PATH, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _dirs, files in os.walk(MOD_ROOT):
            for f in sorted(files):
                full = os.path.join(root, f)
                arc = os.path.relpath(full, MOD_ROOT).replace(os.sep, "/")
                info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(full, "rb") as fh:
                    z.writestr(info, fh.read())
    return loads


if __name__ == "__main__":
    report = build()
    for k, v in report.items():
        print("%-24s %.3f" % (k, v))
    print("wrote", os.path.relpath(VEH, HERE), "and", os.path.relpath(ZIP_PATH, HERE))
