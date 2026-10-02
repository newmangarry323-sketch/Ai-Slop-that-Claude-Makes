#!/usr/bin/env python3
"""Turn VGA text-mode dumps (.vga, 80x25 cells of char + attribute) into PNGs.

Each cell is drawn with the standard 16-colour VGA palette and the IBM PC
code page 437 glyphs, using the Unifont font (8x16 pixels per character, like
the VGA ROM font).  The page is laid out in HTML and photographed with
headless Chromium and cropped with ImageMagick's "convert", so no Python
imaging library is needed.

Attribute bit 7 is drawn as a bright background, because the SkarletOS kernel
switches the VGA "blink" bit off (see vga_init in kernel/arch_x86_64.c).

Usage: vga_render.py OUT_DIR file.vga [file.vga ...]
Environment: CHROME=/path/to/chrome, UNIFONT=/path/to/unifont.otf
"""
import glob
import html
import os
import shutil
import subprocess
import sys
import tempfile

# The standard VGA/CGA text palette (index = 4-bit colour number), except
# colour 4: the SkarletOS kernel reprograms it from red (#AA0000) to maroon,
# #820000 being the nearest the VGA's 6-bit DAC gets to #800000.
PALETTE = [
    "#000000", "#0000AA", "#00AA00", "#00AAAA", "#820000", "#AA00AA", "#AA5500", "#AAAAAA",
    "#555555", "#5555FF", "#55FF55", "#55FFFF", "#FF5555", "#FF55FF", "#FFFF55", "#FFFFFF",
]

# Code page 437 as Unicode: control-code glyphs first, then the upper half.
LOW = (" ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼")
HIGH = ("ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»"
        "░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
        "αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ")
CP437 = list(LOW) + [chr(c) for c in range(32, 127)] + ["⌂"] + list(HIGH)
assert len(CP437) == 256

# Block glyphs are drawn as solid CSS fills rather than font glyphs, so the
# big clock digits have no anti-aliased seams between cells.
BLOCKS = {
    0xDB: "{fg}",                                              # full block
    0xDF: "linear-gradient({fg} 50%, {bg} 50%)",               # upper half
    0xDC: "linear-gradient({bg} 50%, {fg} 50%)",               # lower half
    0xDD: "linear-gradient(90deg, {fg} 50%, {bg} 50%)",        # left half
    0xDE: "linear-gradient(90deg, {bg} 50%, {fg} 50%)",        # right half
}


def find_chrome():
    if os.environ.get("CHROME"):
        return os.environ["CHROME"]
    for name in ("chromium", "chromium-browser", "google-chrome", "chrome"):
        if shutil.which(name):
            return shutil.which(name)
    found = glob.glob("/opt/pw-browsers/chromium-*/chrome-linux/chrome")
    if found:
        return found[0]
    raise SystemExit("Chromium not found; set CHROME=/path/to/chrome")


def to_html(cells, font):
    rows = []
    for y in range(25):
        spans = []
        for x in range(80):
            ch, attr = cells[(y * 80 + x) * 2], cells[(y * 80 + x) * 2 + 1]
            fg, bg = PALETTE[attr & 15], PALETTE[attr >> 4]
            if ch in BLOCKS:
                spans.append('<i style="background:%s"></i>' % BLOCKS[ch].format(fg=fg, bg=bg))
                continue
            glyph = CP437[ch]
            cls = ' class="s"' if ord(glyph) > 127 else ""
            spans.append('<i%s style="color:%s;background:%s">%s</i>' % (
                cls, fg, bg, html.escape(glyph)))
        rows.append("<div>" + "".join(spans) + "</div>")
    return """<!doctype html><meta charset="utf-8"><style>
@font-face { font-family: vga; src: url("file://%s"); }
html, body { margin: 0; background: #000; }
body { font: 16px/16px vga, monospace; width: 640px; }
div { height: 16px; white-space: pre; }
i { font-style: normal; display: inline-block; width: 8px; height: 16px;
    overflow: hidden; vertical-align: top; }
i.s span { display: inline-block; transform-origin: 0 0; }
</style><body>%s<script>
// Some symbols are two cells wide in Unifont: measure each one and squeeze
// only those into their single cell.
document.fonts.load("16px vga").then(() => {
  for (const el of document.querySelectorAll("i.s")) {
    const s = document.createElement("span"); s.textContent = el.textContent;
    el.textContent = ""; el.appendChild(s);
    const w = s.getBoundingClientRect().width;
    if (w > 8.5) s.style.transform = "scaleX(" + (8 / w) + ")";
  }
});
</script>""" % (font, "".join(rows))


def main():
    out_dir, files = sys.argv[1], sys.argv[2:]
    os.makedirs(out_dir, exist_ok=True)
    chrome = find_chrome()
    font = os.environ.get("UNIFONT", "/usr/share/fonts/opentype/unifont/unifont.otf")
    with tempfile.TemporaryDirectory() as tmp:
        for path in files:
            cells = open(path, "rb").read()
            assert len(cells) == 4000, path
            page = os.path.join(tmp, "screen.html")
            with open(page, "w", encoding="utf-8") as f:
                f.write(to_html(cells, font))
            png = os.path.join(out_dir, os.path.basename(path)[:-4] + ".png")
            # The headless viewport is shorter than the window, so take a
            # taller shot and crop it to exactly 80x25 cells (1280x800).
            subprocess.run([chrome, "--headless", "--no-sandbox", "--disable-gpu",
                            "--hide-scrollbars", "--force-device-scale-factor=2",
                            "--window-size=640,600", "--virtual-time-budget=2000",
                            "--screenshot=" + png, "file://" + page],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["convert", png, "-crop", "1280x800+0+0", "+repage", png],
                           check=True)
            print("wrote", png)


if __name__ == "__main__":
    main()
