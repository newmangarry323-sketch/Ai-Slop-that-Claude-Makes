"""Draws every texture the mod uses, as 16x16 pixel art, with nothing but the standard library.

    python tools/make_textures.py            # writes into src/main/resources/assets/rustbuilding/textures
    python tools/make_textures.py --preview  # also writes build/texture-preview.png, everything enlarged

Each texture is a small function below, so changing one means editing a few lines and re-running this.
The random noise is seeded, so the output is the same every run.
"""

import os
import random
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "src", "main", "resources", "assets", "rustbuilding", "textures")
ICON = os.path.join(ROOT, "src", "main", "resources", "assets", "rustbuilding", "icon.png")

CLEAR = (0, 0, 0, 0)


def hex_colour(value, alpha=255):
    value = value.lstrip("#")
    return (int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16), alpha)


def shade(colour, amount):
    """Lighten (amount > 0) or darken (amount < 0) a colour by a fraction."""
    r, g, b, a = colour
    if amount >= 0:
        return (round(r + (255 - r) * amount), round(g + (255 - g) * amount), round(b + (255 - b) * amount), a)
    return (round(r * (1 + amount)), round(g * (1 + amount)), round(b * (1 + amount)), a)


class Image:
    def __init__(self, width, height, fill=CLEAR):
        self.width = width
        self.height = height
        self.pixels = [[fill for _ in range(width)] for _ in range(height)]

    def set(self, x, y, colour):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y][x] = colour

    def get(self, x, y):
        return self.pixels[y][x]

    def rect(self, x0, y0, x1, y1, colour):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, colour)

    def noise(self, rng, strength, x0=0, y0=0, x1=None, y1=None):
        """Jitters the brightness of every opaque pixel in a rectangle."""
        x1 = self.width - 1 if x1 is None else x1
        y1 = self.height - 1 if y1 is None else y1
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                colour = self.pixels[y][x]
                if colour[3]:
                    self.pixels[y][x] = shade(colour, rng.uniform(-strength, strength))

    def scaled(self, factor):
        out = Image(self.width * factor, self.height * factor)
        for y in range(out.height):
            for x in range(out.width):
                out.pixels[y][x] = self.pixels[y // factor][x // factor]
        return out

    def paste(self, other, ox, oy):
        for y in range(other.height):
            for x in range(other.width):
                colour = other.pixels[y][x]
                if colour[3]:
                    self.set(ox + x, oy + y, colour)

    def save(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        raw = b"".join(b"\x00" + bytes(channel for pixel in row for channel in pixel) for row in self.pixels)

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        with open(path, "wb") as handle:
            handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# --- Building grades -------------------------------------------------------------------------

def twig():
    """A lattice of lashed sticks with gaps you can see through, like Rust's twig frames."""
    rng = random.Random(1)
    img = Image(16, 16)
    stick = hex_colour("#7b5833")
    light = shade(stick, 0.25)
    dark = shade(stick, -0.3)
    lashing = hex_colour("#c9b48a")

    def horizontal(y):
        for x in range(16):
            img.set(x, y, light)
            img.set(x, y + 1, dark)

    def vertical(x):
        for y in range(16):
            img.set(x, y, light)
            img.set(x + 1, y, dark)

    horizontal(0)
    horizontal(7)
    horizontal(14)
    vertical(0)
    vertical(14)
    # A diagonal brace across each half.
    for i in range(6):
        img.set(2 + i * 2, 6 - i, stick)
        img.set(3 + i * 2, 6 - i, dark)
        img.set(2 + i * 2, 13 - i, stick)
        img.set(3 + i * 2, 13 - i, dark)
    img.noise(rng, 0.12)
    for x, y in ((1, 1), (14, 1), (1, 8), (14, 8), (1, 15), (14, 15)):
        img.set(x, y, lashing)
    return img


def wood():
    """Vertical planks with dark seams and a couple of nails."""
    rng = random.Random(2)
    base = hex_colour("#8a6439")
    img = Image(16, 16, base)
    for plank in range(4):
        tone = shade(base, rng.uniform(-0.12, 0.1))
        img.rect(plank * 4, 0, plank * 4 + 3, 15, tone)
        img.rect(plank * 4, 0, plank * 4, 15, shade(tone, -0.35))
        # Grain: short darker streaks down the plank.
        for _ in range(4):
            x = plank * 4 + rng.randint(1, 3)
            y = rng.randint(0, 12)
            for dy in range(rng.randint(2, 4)):
                img.set(x, y + dy, shade(tone, -0.15))
        # The end of one board and the start of the next.
        joint = rng.randint(4, 11)
        img.rect(plank * 4 + 1, joint, plank * 4 + 3, joint, shade(tone, -0.3))
    img.noise(rng, 0.05)
    nail = hex_colour("#4a4a4a")
    for x, y in ((2, 2), (6, 13), (10, 2), (14, 13)):
        img.set(x, y, nail)
    return img


def stone():
    """Large staggered stone blocks with deep mortar, as Rust's stone grade."""
    rng = random.Random(3)
    base = hex_colour("#8c8b84")
    img = Image(16, 16, hex_colour("#56554f"))
    # Two courses of 7 x 7 blocks; the second is offset by half a block. Mortar fills the rest.
    for top, offset in ((0, 0), (8, 4)):
        for start in (offset - 8, offset, offset + 8):
            tone = shade(base, rng.uniform(-0.1, 0.1))
            for x in range(start + 1, start + 8):
                if not 0 <= x < 16:
                    continue
                for y in range(top, top + 7):
                    img.set(x, y, tone)
                img.set(x, top, shade(tone, 0.18))
                img.set(x, top + 6, shade(tone, -0.15))
    img.noise(rng, 0.07)
    return img


def sheet_metal():
    """Corrugated sheet steel with rust creeping in, Rust's sheet metal grade."""
    rng = random.Random(4)
    base = hex_colour("#7d858b")
    img = Image(16, 16, base)
    for x in range(16):
        phase = x % 4
        tone = shade(base, (0.22, 0.05, -0.15, -0.05)[phase])
        img.rect(x, 0, x, 15, tone)
    # Overlapping sheet seam and rivets.
    img.rect(0, 7, 15, 7, shade(base, -0.35))
    img.rect(0, 8, 15, 8, shade(base, 0.15))
    for x in range(1, 16, 4):
        img.set(x, 6, hex_colour("#b8bec2"))
        img.set(x, 15, hex_colour("#b8bec2"))
    rust = hex_colour("#8f4f24")
    for _ in range(18):
        x, y = rng.randint(0, 15), rng.randint(0, 15)
        img.set(x, y, shade(rust, rng.uniform(-0.2, 0.2)))
        if rng.random() < 0.5:
            img.set(x, min(15, y + 1), shade(rust, -0.25))
    img.noise(rng, 0.04)
    return img


def armored():
    """Thick dark armour plates, bolted down - the top grade."""
    rng = random.Random(5)
    base = hex_colour("#45484d")
    img = Image(16, 16, base)
    edge = shade(base, 0.35)
    seam = shade(base, -0.45)
    for top, left in ((0, 0), (0, 8), (8, 0), (8, 8)):
        img.rect(left, top, left + 7, top + 7, shade(base, rng.uniform(-0.06, 0.06)))
        img.rect(left, top, left + 7, top, edge)
        img.rect(left, top, left, top + 7, edge)
        img.rect(left, top + 7, left + 7, top + 7, seam)
        img.rect(left + 7, top, left + 7, top + 7, seam)
        for bx, by in ((left + 2, top + 2), (left + 5, top + 2), (left + 2, top + 5), (left + 5, top + 5)):
            img.set(bx, by, hex_colour("#8c9096"))
            img.set(bx + 1, by + 1, hex_colour("#26282b"))
    img.noise(rng, 0.04)
    return img


# --- Tool cupboard ---------------------------------------------------------------------------

def plank_panel(seed, base_hex):
    rng = random.Random(seed)
    base = hex_colour(base_hex)
    img = Image(16, 16, base)
    for row in range(4):
        tone = shade(base, rng.uniform(-0.1, 0.08))
        img.rect(0, row * 4, 15, row * 4 + 3, tone)
        img.rect(0, row * 4 + 3, 15, row * 4 + 3, shade(tone, -0.3))
    img.noise(rng, 0.05)
    return img


def cupboard_side():
    img = plank_panel(6, "#7a5534")
    frame = hex_colour("#4f3520")
    img.rect(0, 0, 0, 15, frame)
    img.rect(15, 0, 15, 15, frame)
    return img


def cupboard_top():
    img = plank_panel(7, "#86603b")
    frame = hex_colour("#4f3520")
    img.rect(0, 0, 15, 0, frame)
    img.rect(0, 15, 15, 15, frame)
    img.rect(0, 0, 0, 15, frame)
    img.rect(15, 0, 15, 15, frame)
    return img


def cupboard_front():
    """A pegboard of tools behind a wooden frame."""
    rng = random.Random(8)
    frame = hex_colour("#4f3520")
    board = hex_colour("#a88a60")
    img = Image(16, 16, board)
    img.noise(rng, 0.05)
    for y in range(2, 14, 3):
        for x in range(2, 14, 3):
            img.set(x, y, shade(board, -0.35))
    img.rect(0, 0, 15, 1, frame)
    img.rect(0, 14, 15, 15, frame)
    img.rect(0, 0, 1, 15, frame)
    img.rect(14, 0, 15, 15, frame)
    steel = hex_colour("#9aa0a6")
    handle = hex_colour("#5c3d22")
    # Hammer.
    img.rect(3, 3, 6, 4, steel)
    img.rect(4, 5, 4, 11, handle)
    # Saw.
    img.rect(8, 3, 12, 6, steel)
    for x in range(8, 13, 2):
        img.set(x, 7, steel)
    img.rect(11, 2, 12, 2, handle)
    # Spanner.
    img.rect(9, 9, 9, 12, steel)
    img.set(8, 9, steel)
    img.set(10, 9, steel)
    img.set(8, 12, steel)
    img.set(10, 12, steel)
    return img


# --- Doors -----------------------------------------------------------------------------------

def door_half(seed, base_hex, top, armoured):
    rng = random.Random(seed)
    base = hex_colour(base_hex)
    img = Image(16, 16, base)
    frame = shade(base, -0.4)
    if armoured:
        img.rect(3, 0, 12, 15, shade(base, 0.08))
        for y in range(1, 16, 5):
            for x in (4, 11):
                img.set(x, y, hex_colour("#8c9096"))
        if top:
            img.rect(5, 4, 10, 5, hex_colour("#111214"))
    else:
        for x in range(3, 13):
            img.rect(x, 0, x, 15, shade(base, (0.2, 0.0, -0.15, 0.0)[x % 4]))
        rust = hex_colour("#8f4f24")
        for _ in range(7):
            img.set(rng.randint(3, 12), rng.randint(0, 15), rust)
        if top:
            img.rect(5, 3, 10, 3, frame)
    img.rect(0, 0, 2, 15, frame)
    img.rect(13, 0, 15, 15, frame)
    if top:
        img.rect(0, 0, 15, 1, frame)
    else:
        img.rect(0, 14, 15, 15, frame)
        img.rect(11, 3, 12, 4, hex_colour("#c9c9c9"))
    img.noise(rng, 0.04)
    return img


def door_item(top, bottom):
    """A miniature of the door: both halves squeezed into one 8 x 16 picture."""
    img = Image(16, 16)
    for y in range(8):
        for x in range(8):
            source_x = 2 + x * 12 // 8
            img.set(4 + x, y, top.get(source_x, y * 2))
            img.set(4 + x, 8 + y, bottom.get(source_x, y * 2))
    return img


# --- Items -----------------------------------------------------------------------------------

def building_plan():
    """Blueprint paper with a house drawn on it."""
    img = Image(16, 16)
    paper = hex_colour("#2f5f9e")
    line = hex_colour("#dce9ff")
    faint = hex_colour("#5a86c0")
    img.rect(1, 2, 14, 13, paper)
    for x in range(2, 14, 3):
        img.rect(x, 3, x, 12, faint)
    for y in range(4, 13, 3):
        img.rect(2, y, 13, y, faint)
    # House outline.
    for x in range(4, 12):
        img.set(x, 11, line)
    for y in range(7, 12):
        img.set(4, y, line)
        img.set(11, y, line)
    for i in range(4):
        img.set(4 + i, 7 - i, line)
        img.set(11 - i, 7 - i, line)
    img.rect(7, 9, 8, 10, line)
    # Curled corner.
    img.rect(13, 12, 14, 13, hex_colour("#c3d6f2"))
    img.set(1, 2, shade(paper, -0.3))
    return img


def hammer():
    """A wooden-handled hammer, handle rising to the top right with the steel head across its end."""
    img = Image(16, 16)
    handle = hex_colour("#7a5232")
    handle_dark = shade(handle, -0.35)
    head = hex_colour("#9ea4aa")
    head_dark = shade(head, -0.4)
    head_light = hex_colour("#d7dbde")
    for i in range(10):
        img.set(1 + i, 14 - i, handle)
        img.set(2 + i, 14 - i, handle_dark)
        img.set(1 + i, 13 - i, handle)
    # Head: long across the handle (u), four pixels thick along it (v), centred on the handle's end.
    cx, cy = 11, 4
    for y in range(16):
        for x in range(16):
            u = (x - cx) + (y - cy)
            v = (x - cx) - (y - cy)
            if -4 <= u <= 4 and -2 <= v <= 1:
                img.set(x, y, head_light if v == -2 else head_dark if v == 1 else head)
    return img


def code_lock():
    img = Image(16, 16)
    body = hex_colour("#3b3f44")
    img.rect(3, 2, 12, 13, body)
    img.rect(3, 2, 12, 2, shade(body, 0.3))
    img.rect(4, 3, 11, 4, hex_colour("#1b2a1b"))
    img.rect(5, 3, 7, 3, hex_colour("#55ff6a"))
    for row in range(3):
        for column in range(3):
            img.set(5 + column * 2, 6 + row * 2, hex_colour("#c7cbd0"))
    img.set(11, 12, hex_colour("#ff4040"))
    img.rect(3, 13, 12, 13, shade(body, -0.4))
    return img


# --- Mod icon --------------------------------------------------------------------------------

def icon(swatches):
    """The four solid grades as a 2x2 wall, framed by twig, at 64 x 64."""
    img = Image(64, 64)
    for index, swatch in enumerate(swatches):
        img.paste(swatch.scaled(2), 32 * (index % 2), 32 * (index // 2))
    frame = twig().scaled(4)
    for y in range(64):
        for x in range(64):
            if (x < 4 or x >= 60 or y < 4 or y >= 60) and frame.get(x, y)[3]:
                img.set(x, y, frame.get(x, y))
    return img


def main():
    preview = "--preview" in sys.argv
    textures = {
        "block/twig": twig(),
        "block/wood": wood(),
        "block/stone": stone(),
        "block/sheet_metal": sheet_metal(),
        "block/armored": armored(),
        "block/tool_cupboard_front": cupboard_front(),
        "block/tool_cupboard_side": cupboard_side(),
        "block/tool_cupboard_top": cupboard_top(),
        "block/sheet_metal_door_top": door_half(9, "#7d858b", True, False),
        "block/sheet_metal_door_bottom": door_half(10, "#7d858b", False, False),
        "block/armored_door_top": door_half(11, "#45484d", True, True),
        "block/armored_door_bottom": door_half(12, "#45484d", False, True),
        "item/building_plan": building_plan(),
        "item/hammer": hammer(),
        "item/code_lock": code_lock(),
    }
    textures["item/sheet_metal_door"] = door_item(textures["block/sheet_metal_door_top"], textures["block/sheet_metal_door_bottom"])
    textures["item/armored_door"] = door_item(textures["block/armored_door_top"], textures["block/armored_door_bottom"])

    for name, image in textures.items():
        image.save(os.path.join(TEXTURES, name + ".png"))

    mod_icon = icon([textures["block/wood"], textures["block/stone"], textures["block/sheet_metal"], textures["block/armored"]])
    mod_icon.save(ICON)
    print("wrote %d textures and the icon" % len(textures))

    if preview:
        names = list(textures)
        sheet = Image(len(names) * 136, 136, hex_colour("#202020"))
        for index, name in enumerate(names):
            sheet.paste(textures[name].scaled(8), index * 136 + 4, 4)
        sheet.save(os.path.join(ROOT, "build", "texture-preview.png"))
        mod_icon.scaled(4).save(os.path.join(ROOT, "build", "icon-preview.png"))
        print("wrote build/texture-preview.png and build/icon-preview.png")


if __name__ == "__main__":
    main()
