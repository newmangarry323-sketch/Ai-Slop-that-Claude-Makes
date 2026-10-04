"""The Kestrel's shape: dimensions, the body profile, and helpers shared by the
mesh and physics generators.

BeamNG's axes (the same in .jbeam and in the .dae meshes):
  +X = left,  +Y = backwards (the front of the car is at -Y),  +Z = up.
All lengths are in metres. The ground is at z = 0 when the car stands still.
"""

import math

# --- wheels and axles ------------------------------------------------------
WHEEL_R = 0.30          # tyre outer radius (a 195/55 R15 is about 0.297 m)
TIRE_W = 0.195          # tyre width
HUB_R = 0.19            # rim radius (15 inch rim = 0.19 m)
HUB_W = 0.17            # rim width
TRACK_HALF = 0.72       # wheel centre to car centreline (track 1.44 m)
AXLE_Z = 0.30           # wheel centre height = tyre radius
AXLE_F_Y = -1.30        # front axle
AXLE_R_Y = 1.10         # rear axle (wheelbase 2.40 m)
ARCH_R = 0.36           # radius of the wheel arch cut-out in the body

AXLES = {"F": AXLE_F_Y, "R": AXLE_R_Y}

# --- body profile ----------------------------------------------------------
# Each row is a cross-section of the car at one Y position. Every section is
# the same chain of 7 points from the bottom centre round to the top centre
# (left half only; the right half is the mirror image):
#
#   p6 roof/hood centre           p5 roof edge (top of the side glass)
#   p4 window base                p3 shoulder (belt line)
#   p2 sill (lower body side)     p1 underbody edge      p0 bottom centre
#
# Where there is no cabin, p4..p6 lie on the bonnet or boot lid, so the same
# chain describes the whole car and the sections can be joined into a mesh.
#
#    y      p0/p1 z  p1 x   p2 z  p2 x   p3 z  p3 x   p5 z  p5 x   p6 z
SECTIONS = [
    (-2.00, 0.24, 0.50, 0.30, 0.76, 0.56, 0.78, 0.60, 0.40, 0.605),
    (-1.75, 0.22, 0.55, 0.26, 0.82, 0.66, 0.84, 0.71, 0.45, 0.720),
    (-1.30, 0.20, 0.55, 0.24, 0.84, 0.73, 0.85, 0.77, 0.46, 0.780),
    (-0.90, 0.18, 0.60, 0.22, 0.85, 0.76, 0.85, 0.80, 0.46, 0.810),
    (-0.55, 0.16, 0.65, 0.20, 0.85, 0.78, 0.85, 0.82, 0.48, 0.830),
    (-0.05, 0.15, 0.66, 0.19, 0.85, 0.80, 0.84, 1.20, 0.66, 1.230),
    (0.40, 0.15, 0.66, 0.19, 0.85, 0.82, 0.85, 1.22, 0.67, 1.250),
    (0.80, 0.16, 0.66, 0.20, 0.85, 0.84, 0.85, 1.17, 0.64, 1.200),
    (1.35, 0.20, 0.60, 0.24, 0.84, 0.86, 0.84, 0.92, 0.48, 0.930),
    (1.70, 0.23, 0.55, 0.27, 0.82, 0.86, 0.82, 0.93, 0.46, 0.940),
    (1.98, 0.27, 0.52, 0.32, 0.78, 0.84, 0.78, 0.90, 0.44, 0.910),
]

FRONT_Y = SECTIONS[0][0]
REAR_Y = SECTIONS[-1][0]

# The cabin (glass) runs between these Y positions.
CABIN_Y = (-0.60, 1.40)
B_PILLAR_Y = (0.36, 0.46)


def _pchip_slopes(xs, ys):
    """Slopes for a monotone cubic (PCHIP) curve: smooth, never overshoots."""
    n = len(xs)
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0:
            m[i] = 0.0
        else:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    return m


def _pchip(xs, ys, ms, x):
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    i = max(k for k in range(len(xs) - 1) if xs[k] <= x)
    h = xs[i + 1] - xs[i]
    t = (x - xs[i]) / h
    h00 = 2 * t**3 - 3 * t**2 + 1
    h10 = t**3 - 2 * t**2 + t
    h01 = -2 * t**3 + 3 * t**2
    h11 = t**3 - t**2
    return h00 * ys[i] + h10 * h * ms[i] + h01 * ys[i + 1] + h11 * h * ms[i + 1]


_YS = [s[0] for s in SECTIONS]
_COLS = list(zip(*SECTIONS))[1:]
_SLOPES = [_pchip_slopes(_YS, list(c)) for c in _COLS]


def section(y):
    """The 7 profile points (x, z) of the left half of the body at position y."""
    bz, bw, sz, sw, tz, tw, rz, rw, cz = (
        _pchip(_YS, list(c), m, y) for c, m in zip(_COLS, _SLOPES))
    p4 = (tw - 0.04, tz + 0.03)
    return [(0.0, bz), (bw, bz), (sw, sz), (tw, tz), p4, (rw, rz), (0.0, cz)]


def arch_top(y):
    """Height of the wheel arch edge at y, or None away from the wheels."""
    for ay in AXLES.values():
        dy = y - ay
        if abs(dy) < ARCH_R:
            return AXLE_Z + math.sqrt(ARCH_R ** 2 - dy ** 2)
    return None


def greenhouse_height(y):
    """Height of the side glass at y (zero where there is no cabin)."""
    p = section(y)
    return p[5][1] - p[4][1]
