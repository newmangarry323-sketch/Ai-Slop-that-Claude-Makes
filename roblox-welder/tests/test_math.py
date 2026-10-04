"""Checks the welding maths in src/shared/WelderShared.luau.

There is no Luau runtime in this project's tooling, so the formulas are
copied here line for line and checked with plain Python. If you change a
formula in WelderShared.luau, change it here too.

Run: python3 tests/test_math.py
"""

import math
import pathlib
import re
import unittest

SHARED = (pathlib.Path(__file__).resolve().parent.parent / "src/shared/WelderShared.luau").read_text()


def setting(name: str) -> float:
    """Read a number from the Config table so the tests use the real values."""
    m = re.search(rf"^\s*{name}\s*=\s*([^,]+),", SHARED, re.M)
    assert m, f"Config.{name} not found"
    expr = m.group(1).replace("math.rad", "math.radians")
    return float(eval(expr, {"math": math}))


C = {n: setting(n) for n in [
    "MinGoodSpeed", "IdealSpeed", "MaxGoodSpeed", "BeadWidth", "BeadHeight",
    "IdealReach", "MaxReach", "StrengthPerStud", "TorqueArmBase", "TorqueArmPerStud",
    "StartTemperature", "AmbientTemperature", "CoolingRate", "GlowTemperature",
    "ColdTemperature", "TackLength", "ThinMetalLimit",
]}


def clamp(x, lo, hi):
    return lo if x < lo else hi if x > hi else x


# --- copies of the Luau functions -----------------------------------------
def speed_quality(speed):
    if speed > C["MaxGoodSpeed"]:
        over = (speed - C["MaxGoodSpeed"]) / (C["MaxGoodSpeed"] * 1.2)
        return clamp(1 - over, 0.15, 1), "fast"
    if speed < C["MinGoodSpeed"]:
        return clamp(0.6 + 0.4 * speed / C["MinGoodSpeed"], 0.6, 1), "slow"
    return 1, "good"


def arc_longness(d):
    return clamp((d - C["IdealReach"]) / (C["MaxReach"] - C["IdealReach"]), 0, 1)


def bead_shape(speed, longness):
    heat = clamp(C["IdealSpeed"] / max(speed, 0.05), 0.4, 2.5)
    spread = 1 + 0.4 * longness
    return C["BeadWidth"] * heat ** 0.45 * spread, C["BeadHeight"] * heat ** 0.3 / spread


def disc_for_cap(w, h):
    h = max(h, 0.005)
    r = (w * w / 4 + h * h) / (2 * h)
    return r, r - h


def fit_cap_to_thickness(w, h, t):
    limit = t * 0.9
    if limit <= 0:
        return w, h
    min_h = w * w / (4 * limit)
    if min_h <= w / 2:
        return w, max(h, min_h)
    return 2 * limit, limit


def fillet_disc(w, t=None):
    if t is not None:
        w = min(w, t * 0.9 / 0.54)
    return w * 0.5, w * 0.05


def joint_strength(length, q):
    f = C["StrengthPerStud"] * length * q
    return f, f * (C["TorqueArmBase"] + length * C["TorqueArmPerStud"])


def temperature_at(age):
    air = C["AmbientTemperature"]
    return air + (C["StartTemperature"] - air) * math.exp(-C["CoolingRate"] * max(age, 0))


GLOW = [(525, 70, 0, 0), (600, 120, 12, 0), (700, 170, 30, 0), (815, 215, 55, 5), (900, 240, 95, 15),
        (1000, 255, 140, 35), (1100, 255, 185, 80), (1200, 255, 220, 135), (1350, 255, 245, 210)]


def glow_rgb(t):
    for a, b in zip(GLOW, GLOW[1:]):
        if t <= b[0]:
            k = (t - a[0]) / (b[0] - a[0])
            return tuple(a[i] + (b[i] - a[i]) * k for i in (1, 2, 3))
    return GLOW[-1][1:]


# --- tests ------------------------------------------------------------------
class SpeedAndShape(unittest.TestCase):
    def test_good_range_is_full_quality(self):
        for s in (C["MinGoodSpeed"], C["IdealSpeed"], C["MaxGoodSpeed"]):
            self.assertEqual(speed_quality(s), (1, "good"))

    def test_fast_is_worse_than_slow(self):
        # Too fast (poor fusion) is the worse fault in this model.
        self.assertLess(speed_quality(C["MaxGoodSpeed"] * 2)[0], speed_quality(C["MinGoodSpeed"] / 2)[0])
        self.assertGreaterEqual(speed_quality(100)[0], 0.15)

    def test_slow_bead_is_wider_and_taller(self):
        w_slow, h_slow = bead_shape(0.3, 0)
        w_fast, h_fast = bead_shape(4.0, 0)
        self.assertGreater(w_slow, w_fast)
        self.assertGreater(h_slow, h_fast)

    def test_long_arc_is_wider_and_flatter(self):
        w0, h0 = bead_shape(C["IdealSpeed"], 0)
        w1, h1 = bead_shape(C["IdealSpeed"], 1)
        self.assertGreater(w1, w0)
        self.assertLess(h1, h0)

    def test_arc_longness_range(self):
        self.assertEqual(arc_longness(0), 0)
        self.assertEqual(arc_longness(C["IdealReach"]), 0)
        self.assertEqual(arc_longness(C["MaxReach"]), 1)


class BeadGeometry(unittest.TestCase):
    def test_disc_cap_has_requested_width_and_height(self):
        for w, h in [(0.32, 0.09), (0.5, 0.05), (0.2, 0.1)]:
            r, d = disc_for_cap(w, h)
            self.assertAlmostEqual(r - d, h)  # sticks out by h
            self.assertAlmostEqual(2 * math.sqrt(r * r - d * d), w)  # chord at the surface is w

    def test_below_surface_extent_formula(self):
        # The part of the disc under the surface is d + r = w^2 / (4h).
        w, h = 0.32, 0.09
        r, d = disc_for_cap(w, h)
        self.assertAlmostEqual(d + r, w * w / (4 * h))

    def test_fit_keeps_disc_inside_plate(self):
        for t in (0.15, 0.2, 0.4, 1.0):
            for speed in (0.3, 1.4, 4.0):
                w, h = bead_shape(speed, 0)
                w2, h2 = fit_cap_to_thickness(w, h, t)
                r, d = disc_for_cap(w2, h2)
                self.assertLessEqual(d + r, t * 0.9 + 1e-9, (t, speed))
                self.assertLessEqual(w2, w + 1e-9)

    def test_fit_leaves_thick_plate_alone(self):
        w, h = bead_shape(C["IdealSpeed"], 0)
        self.assertEqual(fit_cap_to_thickness(w, h, 2.0), (w, h))

    def test_fillet_stays_inside_plates(self):
        for t in (0.15, 0.2, 0.5):
            w, _ = bead_shape(0.3, 0)
            r, d = fillet_disc(w, t)
            # centre is d behind the corner along the 45 degree bisector
            behind = d / math.sqrt(2) + r
            self.assertLessEqual(behind, t * 0.9 + 1e-9)

    def test_fillet_legs_about_half_width(self):
        w = 0.32
        r, d = fillet_disc(w)
        off = d / math.sqrt(2)
        leg = math.sqrt(r * r - off * off) - off
        self.assertAlmostEqual(leg / w, 0.46, places=2)


class Heat(unittest.TestCase):
    def test_starts_hot_and_cools_to_air(self):
        self.assertAlmostEqual(temperature_at(0), C["StartTemperature"])
        self.assertAlmostEqual(temperature_at(1e6), C["AmbientTemperature"])

    def test_glow_lasts_about_eight_seconds(self):
        # time when T falls to the Draper point
        air, t0, k = C["AmbientTemperature"], C["StartTemperature"], C["CoolingRate"]
        t = -math.log((C["GlowTemperature"] - air) / (t0 - air)) / k
        self.assertGreater(t, 6)
        self.assertLess(t, 10)

    def test_glow_gets_brighter_with_temperature(self):
        last = -1
        for temp in range(525, 1400, 25):
            brightness = sum(glow_rgb(temp))
            self.assertGreaterEqual(brightness, last)
            last = brightness

    def test_glow_table_starts_at_draper_point(self):
        self.assertEqual(GLOW[0][0], C["GlowTemperature"])
        self.assertIn("{ 525, 70, 0, 0 }", SHARED)  # the Luau table matches this copy


class Strength(unittest.TestCase):
    # The practice bench beam: 6 x 0.6 x 0.6 studs, welded at one end to an
    # anchored post, centre of mass 3 studs out. Roblox's default Metal
    # density is taken as 7.85 here; Studio shows the real mass (Mass
    # property), so if it differs, retune StrengthPerStud.
    BEAM_MASS = 6 * 0.6 * 0.6 * 7.85
    GRAVITY = 196.2
    LEVER = 3.0

    def beam_torque(self):
        return self.BEAM_MASS * self.GRAVITY * self.LEVER

    def test_tack_weld_snaps(self):
        _, torque = joint_strength(C["TackLength"], 1.0)
        self.assertLess(torque, self.beam_torque())

    def test_short_weld_snaps(self):
        _, torque = joint_strength(0.6, 0.9)
        self.assertLess(torque, self.beam_torque())

    def test_proper_weld_holds(self):
        _, torque = joint_strength(1.2, 0.85)
        self.assertGreater(torque, self.beam_torque())

    def test_bad_weld_of_same_length_snaps(self):
        _, torque = joint_strength(1.2, 0.4)
        self.assertLess(torque, self.beam_torque())

    def test_weight_alone_never_the_problem_for_proper_weld(self):
        force, _ = joint_strength(1.2, 0.85)
        self.assertGreater(force, self.BEAM_MASS * self.GRAVITY)



class ExampleNumbers(unittest.TestCase):
    """The claims made in examples/04 and examples/05 (Metal density 7.85 assumed)."""

    G = 196.2
    DENSITY = 7.85

    def shelf_torques(self):
        # Steel shelf 3 x 0.3 x 2.5, centre 1.25 studs out from the wall.
        shelf = 3 * 0.3 * 2.5 * self.DENSITY * self.G * 1.25
        # Crates 0.8^3, density 10*i, at these distances out from the wall.
        levers = [0.6, 0.6, 1.2, 1.9, 1.9, 2.1]
        running, out = shelf, [shelf]
        for i, lever in enumerate(levers, start=1):
            running += 0.8 ** 3 * 10 * i * self.G * lever
            out.append(running)
        return out  # torque with 0..6 crates

    def crates_held(self, length, quality):
        _, rating = joint_strength(length, quality)
        torques = self.shelf_torques()
        if torques[0] > rating:
            return -1  # shelf falls when the prop goes
        return max(i for i, t in enumerate(torques) if t <= rating)

    def test_tack_drops_empty_shelf(self):
        self.assertEqual(self.crates_held(C["TackLength"], 1.0), -1)

    def test_short_weld_holds_a_few(self):
        held = self.crates_held(0.6, 0.9)
        self.assertGreaterEqual(held, 0)
        self.assertLess(held, 6)

    def test_full_back_edge_holds_all(self):
        self.assertEqual(self.crates_held(3.0, 0.85), 6)

    def impact_force(self, drop_height, frames):
        # Landing speed, stopped over `frames` Heartbeat frames (1/60 s each),
        # minus the ImpactGrace allowance, times the lighter block's mass.
        v = math.sqrt(2 * self.G * drop_height)
        a = v / (frames / 60)
        grace = setting("ImpactGrace") * self.G
        return max(0, a - grace) * 1 * self.DENSITY

    def test_drop_tack_cracks(self):
        force_rating, _ = joint_strength(0.3, 1)
        for frames in (1, 2, 3):
            self.assertGreater(self.impact_force(20, frames), force_rating)

    def test_drop_full_weld_holds(self):
        force_rating, _ = joint_strength(4, 0.95)
        for frames in (1, 2, 3):
            self.assertLess(self.impact_force(20, frames), force_rating)


if __name__ == "__main__":
    unittest.main(verbosity=1)
