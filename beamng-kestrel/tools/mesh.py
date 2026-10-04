"""Builds the Kestrel's visual meshes and writes them as a COLLADA (.dae) file.

A mesh here is a dict  {material_name: [triangle, ...]}  where each triangle is
three (position, normal) corners. Triangles are wound counter-clockwise when
seen from outside, which is the usual convention for the front face.
"""

import math

from shape import (ARCH_R, AXLE_Z, AXLES, B_PILLAR_Y, CABIN_Y, FRONT_Y, HUB_R,
                   HUB_W, REAR_Y, TIRE_W, TRACK_HALF, WHEEL_R, arch_top,
                   greenhouse_height, section)

# ---------------------------------------------------------------- vectors ---


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def norm(a):
    length = math.sqrt(a[0] ** 2 + a[1] ** 2 + a[2] ** 2)
    return (0.0, 0.0, 1.0) if length < 1e-12 else scale(a, 1 / length)


def face_normal(a, b, c):
    return norm(cross(sub(b, a), sub(c, a)))


def mirror(p):
    return (-p[0], p[1], p[2])


# ------------------------------------------------------------ mesh helper ---


class Mesh:
    def __init__(self):
        self.groups = {}

    def tri(self, mat, a, b, c, na=None, nb=None, nc=None):
        n = face_normal(a, b, c)
        if n == (0.0, 0.0, 1.0) and abs(cross(sub(b, a), sub(c, a))[2]) < 1e-12:
            return  # degenerate
        self.groups.setdefault(mat, []).append(
            ((a, na or n), (b, nb or n), (c, nc or n)))

    def quad(self, mat, a, b, c, d, normals=None):
        """a-b-c-d counter-clockwise seen from outside."""
        na, nb, nc, nd = normals or (None,) * 4
        self.tri(mat, a, b, c, na, nb, nc)
        self.tri(mat, a, c, d, na, nc, nd)

    def grid(self, rows, material_of, outward=1, keep=None):
        """Join a grid of points rows[i][j] with smooth normals.

        material_of(i, j) gives the material of the cell between rows i, i+1
        and columns j, j+1, or None to leave a hole. keep(corner, cell) can
        drop single corners of a cell: the cell then keeps only the triangles
        without them, split along whichever diagonal keeps more.
        """
        ni, nj = len(rows), len(rows[0])
        acc = [[(0.0, 0.0, 0.0)] * nj for _ in range(ni)]
        cells = []
        for i in range(ni - 1):
            for j in range(nj - 1):
                mat = material_of(i, j)
                if mat is None:
                    continue
                corners = ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))
                if outward < 0:
                    corners = (corners[0], corners[3], corners[2], corners[1])
                a, b, c, d = (rows[ii][jj] for ii, jj in corners)
                n = cross(sub(c, a), sub(d, b))
                for (ii, jj) in corners:
                    acc[ii][jj] = add(acc[ii][jj], n)
                cells.append((mat, corners, (a, b, c, d), (i, j)))
        for mat, corners, quad, cell in cells:
            ns = [norm(acc[ii][jj]) for ii, jj in corners]
            splits = [((0, 1, 2), (0, 2, 3)), ((0, 1, 3), (1, 2, 3))]
            if keep is None:
                tris = splits[0]
            else:
                good = [[t for t in split if all(keep(corners[k], cell) for k in t)] for split in splits]
                tris = max(good, key=len)
            for t in tris:
                self.tri(mat, *(quad[k] for k in t), *(ns[k] for k in t))

    def box(self, mat, center, size):
        cx, cy, cz = center
        hx, hy, hz = (s / 2 for s in size)
        v = [(cx + sx * hx, cy + sy * hy, cz + sz * hz)
             for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
        # v index = 4*(sx>0) + 2*(sy>0) + (sz>0)
        faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6),
                 (0, 2, 6, 4), (1, 5, 7, 3)]
        for f in faces:
            self.quad(mat, *(v[k] for k in f))

    def disc_fan(self, mat, center, ring):
        """Fill a closed polygon `ring` (counter-clockwise seen from outside)."""
        for k in range(len(ring)):
            self.tri(mat, center, ring[k], ring[(k + 1) % len(ring)])


# ------------------------------------------------------------------ body ---

PAINT = "kestrel_paint"
GLASS = "kestrel_glass"
TRIM = "kestrel_trim"
UNDER = "kestrel_underbody"
HEADLIGHT = "kestrel_headlight"
TAILLIGHT = "kestrel_taillight"
PLATE = "kestrel_plate"
BUMPER = "kestrel_bumper"
CHROME = "kestrel_chrome"
RIM = "kestrel_rim"
RIM_DARK = "kestrel_rim_dark"
TIRE = "kestrel_tire"
INTERIOR = "kestrel_interior"

# How many pieces each of the 6 profile segments is split into, and what
# it is: 0 underbody, 1 lower side curve, 2 side, 3 shoulder, 4 side glass
# or deck edge, 5 roof / bonnet / boot.
SEGMENT_STEPS = [3, 2, 7, 1, 4, 5]


def profile(y):
    """Detailed left-half profile at y: list of (x, z, segment_index)."""
    p = section(y)
    pts = []
    for s in range(6):
        (x0, z0), (x1, z1) = p[s], p[s + 1]
        steps = SEGMENT_STEPS[s]
        for k in range(steps + (1 if s == 5 else 0)):
            t = k / steps
            x, z = x0 + (x1 - x0) * t, z0 + (z1 - z0) * t
            if s == 2:
                x += 0.025 * math.sin(math.pi * t)          # a little side bulge
            if s == 5:
                z += 0.03 * math.sin(math.pi / 2 * t)        # roof / bonnet crown
            pts.append((x, z, s))
    # Move the first side point above the wheel arch down onto the arch edge,
    # so the cut-out follows the circle instead of the grid.
    za = arch_top(y)
    if za is not None:
        for k in range(1, len(pts)):
            (x0, z0, s0), (x1, z1, s1) = pts[k - 1], pts[k]
            if s0 in (1, 2) and x0 > 0.45 and z0 < za <= z1:
                t = (za - z0) / (z1 - z0)
                pts[k] = (x0 + (x1 - x0) * t, za, s1)
                break
    return pts


def below_arch(y, x, z, seg):
    za = arch_top(y)
    return za is not None and seg in (1, 2) and x > 0.45 and z < za - 1e-6


def body_material(seg, y0, y1, z_mid, x_mid, rows_z):
    yc = (y0 + y1) / 2
    if seg == 0:
        return UNDER
    if seg in (1, 2, 3):
        return PAINT          # wheel arch holes are cut by below_arch()
    if seg == 4:
        if CABIN_Y[0] < yc < CABIN_Y[1]:
            gh = greenhouse_height(yc)
            if gh > 0.10:
                return TRIM if B_PILLAR_Y[0] < yc < B_PILLAR_Y[1] else GLASS
            if gh > 0.015:
                return TRIM   # the thin wedge where the side glass starts
        return PAINT
    # roof: steep parts inside the cabin are the windscreen and rear window
    slope = abs(rows_z[1] - rows_z[0]) / max(y1 - y0, 1e-6)
    if CABIN_Y[0] < yc < CABIN_Y[1] and slope > 0.3:
        return GLASS
    return PAINT


def build_body():
    m = Mesh()
    n = 110
    ys = [FRONT_Y + (REAR_Y - FRONT_Y) * i / n for i in range(n + 1)]
    profiles = [profile(y) for y in ys]
    rows = [[(x, y, z) for (x, z, _s) in prof] for y, prof in zip(ys, profiles)]
    segs = [s for (_x, _z, s) in profiles[0]]

    def material_of(i, j):
        # a cell belongs to the segment of its first (lower) point
        a, b = rows[i][j], rows[i][j + 1]
        c = rows[i + 1][j]
        z_mid = (a[2] + b[2]) / 2
        x_mid = (a[0] + b[0]) / 2
        return body_material(segs[j], a[1], c[1], z_mid, x_mid, (a[2], c[2]))

    # Left half: rows run front to back, columns run bottom to top. Seen from
    # the left, (i,j) -> (i+1,j) -> (i+1,j+1) -> (i,j+1) is counter-clockwise.
    # The mirrored right half needs the opposite order.
    def keep(corner, cell):
        if segs[cell[1]] not in (1, 2):
            return True       # only the body sides have wheel arches
        i, j = corner
        x, y, z = rows[i][j]
        return not below_arch(y, x, z, segs[j])

    m.grid(rows, material_of, outward=1, keep=keep)
    right = [[mirror(p) for p in row] for row in rows]
    m.grid(right, material_of, outward=-1, keep=keep)

    # End caps (flat nose and tail), filled from a centre point. The ring runs
    # counter-clockwise when seen from the front, so the tail reverses it.
    for k in (0, -1):
        y = ys[k]
        left = [(x, y, z) for (x, z, _s) in profiles[k]]
        ring = [mirror(p) for p in reversed(left[1:-1])] + left
        zc = sum(p[2] for p in left) / len(left)
        if k == -1:
            ring = list(reversed(ring))
        m.disc_fan(PAINT, (0.0, y, zc), ring)

    add_front_details(m, ys[0])
    add_rear_details(m, ys[-1])
    add_arch_liners(m)
    add_mirrors_and_exhaust(m)
    add_interior(m)
    return m


def add_interior(m):
    """Just enough inside to look like a car through the tinted glass."""
    m.box(INTERIOR, (0.0, -0.40, 0.64), (1.44, 0.30, 0.20))        # dashboard
    m.box(INTERIOR, (0.0, -0.05, 0.36), (0.22, 0.60, 0.26))        # centre console
    m.box(INTERIOR, (0.0, 0.0, 0.17), (1.40, 1.30, 0.02))          # floor
    for s in (1, -1):
        m.box(INTERIOR, (s * 0.36, 0.32, 0.33), (0.46, 0.50, 0.12))   # seat cushion
        m.box(INTERIOR, (s * 0.36, 0.60, 0.64), (0.44, 0.10, 0.56))   # seat back
        m.box(INTERIOR, (s * 0.36, 0.62, 0.98), (0.24, 0.08, 0.14))   # headrest
    # steering wheel: a flat ring facing the driver, tilted back 20 degrees
    cx, cy, cz = 0.36, -0.08, 0.80
    tilt = math.radians(20)
    up = (0.0, math.sin(tilt), math.cos(tilt))
    steps = 24

    def ring_point(radius, k):
        a = 2 * math.pi * k / steps
        return (cx + radius * math.cos(a), cy + radius * math.sin(a) * up[1],
                cz + radius * math.sin(a) * up[2])

    for k in range(steps):
        a, b = ring_point(0.15, k), ring_point(0.19, k)
        c, d = ring_point(0.19, k + 1), ring_point(0.15, k + 1)
        m.quad(TRIM, a, d, c, b)   # front face towards the driver
    m.box(TRIM, (cx, cy - 0.15, cz - 0.04), (0.06, 0.30, 0.06))       # column


def flat_rect(m, mat, y, x0, x1, z0, z1, facing):
    """A rectangle in the plane y = const, facing -Y (facing=-1) or +Y."""
    a, b, c, d = (x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)
    if facing < 0:
        m.quad(mat, a, b, c, d)
    else:
        m.quad(mat, b, a, d, c)


def add_front_details(m, y):
    y -= 0.004
    for s in (1, -1):
        x0, x1 = sorted((s * 0.46, s * 0.72))
        flat_rect(m, HEADLIGHT, y, x0, x1, 0.45, 0.52, -1)
    flat_rect(m, TRIM, y, -0.30, 0.30, 0.37, 0.47, -1)
    flat_rect(m, BUMPER, y, -0.66, 0.66, 0.29, 0.35, -1)


def add_rear_details(m, y):
    y += 0.004
    for s in (1, -1):
        x0, x1 = sorted((s * 0.46, s * 0.72))
        flat_rect(m, TAILLIGHT, y, x0, x1, 0.68, 0.78, 1)
    flat_rect(m, PLATE, y, -0.24, 0.24, 0.50, 0.62, 1)
    flat_rect(m, BUMPER, y, -0.70, 0.70, 0.33, 0.40, 1)


def add_arch_liners(m, steps=28):
    """Black half-tubes inside each wheel arch, so you can't see into the body."""
    z_floor = 0.20
    lo = math.asin((z_floor - AXLE_Z) / ARCH_R)
    angles = [lo + (math.pi - 2 * lo) * k / steps for k in range(steps + 1)]
    for ay in AXLES.values():
        for s in (1, -1):
            inner, outer = 0.50 * s, 0.85 * s
            arc = [(ay + ARCH_R * math.cos(a), AXLE_Z + ARCH_R * math.sin(a)) for a in angles]
            for k in range(steps):
                (y0, z0), (y1, z1) = arc[k], arc[k + 1]
                a, b = (inner, y0, z0), (outer, y0, z0)
                c, d = (outer, y1, z1), (inner, y1, z1)
                # the inside of the tube faces the wheel
                n0 = norm((0, ay - y0, AXLE_Z - z0))
                n1 = norm((0, ay - y1, AXLE_Z - z1))
                if s > 0:
                    m.quad(UNDER, a, b, c, d, (n0, n0, n1, n1))
                else:
                    m.quad(UNDER, a, d, c, b, (n0, n1, n1, n0))
            # wall on the inner side of the arch
            ring = [(inner, yy, zz) for (yy, zz) in arc]
            centre = (inner, ay, z_floor)
            for k in range(steps):
                if s > 0:
                    m.tri(UNDER, centre, ring[k], ring[k + 1])
                else:
                    m.tri(UNDER, centre, ring[k + 1], ring[k])


def add_mirrors_and_exhaust(m):
    for s in (1, -1):
        m.box(PAINT, (s * 0.905, -0.42, 0.875), (0.10, 0.06, 0.08))
        m.box(TRIM, (s * 0.845, -0.42, 0.835), (0.06, 0.03, 0.03))
        # mirror glass on the back of the housing
        x0, x1 = sorted((s * 0.865, s * 0.945))
        flat_rect(m, CHROME, -0.42 + 0.031, x0, x1, 0.845, 0.905, 1)
    tube(m, CHROME, (-0.45, 1.88, 0.255), (-0.45, 2.02, 0.255), 0.035, 16, caps=False)
    disc_y(m, TRIM, (-0.45, 2.015, 0.255), 0.03, 16, facing=1)


def tube(m, mat, p0, p1, r, steps, caps=True):
    """Cylinder along Y from p0 to p1 (both with the same x and z)."""
    pts = [(math.cos(2 * math.pi * k / steps), math.sin(2 * math.pi * k / steps)) for k in range(steps)]
    for k in range(steps):
        (c0, s0), (c1, s1) = pts[k], pts[(k + 1) % steps]
        a = (p0[0] + r * c0, p0[1], p0[2] + r * s0)
        b = (p0[0] + r * c1, p0[1], p0[2] + r * s1)
        c = (p1[0] + r * c1, p1[1], p1[2] + r * s1)
        d = (p1[0] + r * c0, p1[1], p1[2] + r * s0)
        m.quad(mat, a, d, c, b, ((c0, 0, s0), (c0, 0, s0), (c1, 0, s1), (c1, 0, s1)))


def disc_y(m, mat, centre, r, steps, facing):
    ring = [(centre[0] + r * math.cos(2 * math.pi * k / steps), centre[1],
             centre[2] + r * math.sin(2 * math.pi * k / steps)) for k in range(steps)]
    if facing > 0:
        ring.reverse()
    m.disc_fan(mat, centre, ring)


# ---------------------------------------------------------------- wheels ---

TIRE_PROFILE = [  # (radius, offset across the tyre / half width), inner to outer
    (HUB_R, -0.92), (0.255, -1.0), (0.288, -0.93), (WHEEL_R, -0.62), (WHEEL_R, 0.62),
    (0.288, 0.93), (0.255, 1.0), (HUB_R, 0.92)]


def revolve(m, mat, centre, outer_sign, prof, steps=32):
    """Spin a profile [(radius, axial offset)] round the X axis at `centre`.

    outer_sign is +1 for wheels on the left (+X) side, -1 on the right, so
    positive offsets always point away from the car.
    """
    cx, cy, cz = centre
    ring = [(math.cos(2 * math.pi * k / steps), math.sin(2 * math.pi * k / steps)) for k in range(steps + 1)]
    rows = [[(cx + outer_sign * off, cy + r * cs, cz + r * sn) for (cs, sn) in ring] for (r, off) in prof]
    # going outwards along the profile and round the circle is clockwise
    # when seen from outside on the left (+X) side, so flip there
    m.grid(rows, lambda i, j: mat, outward=-outer_sign)


def build_tire(centre, side):
    m = Mesh()
    half = TIRE_W / 2
    revolve(m, TIRE, centre, side, [(r, o * half) for (r, o) in TIRE_PROFILE])
    return m


def build_rim(centre, side, spokes=5):
    m = Mesh()
    half = HUB_W / 2
    # barrel (seen from inside the tyre it is hidden, but closes the wheel)
    revolve(m, RIM_DARK, centre, side, [(HUB_R - 0.004, half), (HUB_R - 0.004, -half)])
    # outer lip
    revolve(m, RIM, centre, side, [(HUB_R, half - 0.01), (HUB_R + 0.008, half + 0.004), (HUB_R - 0.02, half + 0.006)])
    cx, cy, cz = centre
    face_x = cx + side * (half - 0.015)
    deep_x = cx + side * (half - 0.07)
    steps_per = 6
    total = spokes * 2 * steps_per
    for k in range(total):
        a0 = 2 * math.pi * k / total
        a1 = 2 * math.pi * (k + 1) / total
        is_spoke = (k // steps_per) % 2 == 0
        x = face_x if is_spoke else deep_x
        mat = RIM if is_spoke else RIM_DARK
        r_in, r_out = 0.065, HUB_R - 0.02
        p = [(x, cy + r * math.cos(a), cz + r * math.sin(a)) for (r, a) in
             ((r_in, a0), (r_out, a0), (r_out, a1), (r_in, a1))]
        if side > 0:
            m.quad(mat, *p)
        else:
            m.quad(mat, p[0], p[3], p[2], p[1])
    # centre cap
    ring = [(face_x + side * 0.008, cy + 0.065 * math.cos(2 * math.pi * k / 20),
             cz + 0.065 * math.sin(2 * math.pi * k / 20)) for k in range(20)]
    if side < 0:
        ring.reverse()
    m.disc_fan(CHROME, (face_x + side * 0.012, cy, cz), ring)
    return m


def wheel_centre(corner):
    """corner is 'FL', 'FR', 'RL' or 'RR'."""
    side = 1 if corner[1] == "L" else -1
    return (side * TRACK_HALF, AXLES[corner[0]], AXLE_Z), side


def build_all():
    meshes = {"kestrel_body": build_body()}
    for corner in ("FL", "FR", "RL", "RR"):
        centre, side = wheel_centre(corner)
        meshes["kestrel_tire_" + corner] = build_tire(centre, side)
        meshes["kestrel_rim_" + corner] = build_rim(centre, side)
    return meshes


# ---------------------------------------------------------------- collada ---


def _fmt(v):
    s = "%.5f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def write_dae(meshes, path):
    """Write meshes {name: Mesh} as COLLADA 1.4.1, Z up, metres.

    Each mesh becomes a geometry and a scene node with the same name; that
    name is what the jbeam "flexbodies" section refers to.
    """
    materials = sorted({mat for mesh in meshes.values() for mat in mesh.groups})
    out = ['<?xml version="1.0" encoding="utf-8"?>',
           '<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">',
           '  <asset>',
           '    <contributor><authoring_tool>beamng-kestrel tools/build_mod.py</authoring_tool></contributor>',
           '    <unit name="meter" meter="1"/>',
           '    <up_axis>Z_UP</up_axis>',
           '  </asset>',
           '  <library_effects>']
    for mat in materials:
        out += ['    <effect id="%s-effect"><profile_COMMON><technique sid="common"><lambert>' % mat,
                '      <diffuse><color sid="diffuse">0.8 0.8 0.8 1</color></diffuse>',
                '    </lambert></technique></profile_COMMON></effect>']
    out.append('  </library_effects>')
    out.append('  <library_materials>')
    for mat in materials:
        out.append('    <material id="%s-material" name="%s"><instance_effect url="#%s-effect"/></material>' % (mat, mat, mat))
    out.append('  </library_materials>')
    out.append('  <library_geometries>')
    for name, mesh in meshes.items():
        pos_index, nrm_index, positions, normals, uvs, uv_index = {}, {}, [], [], [], {}

        def idx(table, store, key):
            if key not in table:
                table[key] = len(store)
                store.append(key)
            return table[key]

        prims = []
        for mat, tris in sorted(mesh.groups.items()):
            indices = []
            for tri in tris:
                for (p, n) in tri:
                    pk = tuple(round(c, 5) for c in p)
                    nk = tuple(round(c, 4) for c in norm(n))
                    uk = (round(p[1], 4), round(p[2] + 0.5 * abs(p[0]), 4))
                    indices += [idx(pos_index, positions, pk), idx(nrm_index, normals, nk),
                                idx(uv_index, uvs, uk)]
            prims.append((mat, len(tris), indices))
        gid = name + "-mesh"
        out.append('    <geometry id="%s" name="%s"><mesh>' % (gid, name))
        for suffix, data, params in (("positions", positions, "XYZ"),
                                      ("normals", normals, "XYZ"),
                                      ("map-0", uvs, "ST")):
            flat = " ".join(_fmt(c) for v in data for c in v)
            out.append('      <source id="%s-%s"><float_array id="%s-%s-array" count="%d">%s</float_array>'
                       % (gid, suffix, gid, suffix, len(data) * len(params), flat))
            out.append('        <technique_common><accessor source="#%s-%s-array" count="%d" stride="%d">%s</accessor></technique_common></source>'
                       % (gid, suffix, len(data), len(params),
                          "".join('<param name="%s" type="float"/>' % c for c in params)))
        out.append('      <vertices id="%s-vertices"><input semantic="POSITION" source="#%s-positions"/></vertices>' % (gid, gid))
        for mat, count, indices in prims:
            out.append('      <triangles material="%s-material" count="%d">' % (mat, count))
            out.append('        <input semantic="VERTEX" source="#%s-vertices" offset="0"/>' % gid)
            out.append('        <input semantic="NORMAL" source="#%s-normals" offset="1"/>' % gid)
            out.append('        <input semantic="TEXCOORD" source="#%s-map-0" offset="2" set="0"/>' % gid)
            out.append('        <p>%s</p></triangles>' % " ".join(map(str, indices)))
        out.append('    </mesh></geometry>')
    out.append('  </library_geometries>')
    out.append('  <library_visual_scenes><visual_scene id="Scene" name="Scene">')
    for name, mesh in meshes.items():
        out.append('    <node id="%s" name="%s" type="NODE">' % (name, name))
        out.append('      <matrix sid="transform">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</matrix>')
        out.append('      <instance_geometry url="#%s-mesh" name="%s"><bind_material><technique_common>' % (name, name))
        for mat in sorted(mesh.groups):
            out.append('        <instance_material symbol="%s-material" target="#%s-material"/>' % (mat, mat))
        out.append('      </technique_common></bind_material></instance_geometry>')
        out.append('    </node>')
    out.append('  </visual_scene></library_visual_scenes>')
    out.append('  <scene><instance_visual_scene url="#Scene"/></scene>')
    out.append('</COLLADA>')
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    return materials
