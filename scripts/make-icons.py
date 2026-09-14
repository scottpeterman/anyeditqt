#!/usr/bin/env python3
"""scripts/make-icons.py -- generate the application icon for all three platforms.

    python3 scripts/make-icons.py

Writes into assets/:

    anyedit.icns          macOS bundle icon
    anyedit.ico           Windows executable icon
    anyedit-<N>.png       Linux hicolor sizes, 16..512

THE OUTPUTS ARE COMMITTED. This script is the source of truth for what they
contain, but a macOS or Windows build must not need Python and Pillow to
produce an icon -- that would put an image library in the dependency list of a
C++ editor. Run this when the design changes, and commit what falls out.

NO iconutil AND NO sips. Both are macOS-only, and the icon has to be
regenerable on the machine the change is made on. The .icns container is
written here directly: it is a four-byte magic, a length, and a sequence of
typed chunks, and since 10.7 those chunks may hold PNG data as-is. The type
codes below are the ones iconutil itself emits for a standard .iconset.

THE DESIGN, so a change is a decision rather than a nudge:

  * A squircle, not a rounded rectangle. macOS has used a superellipse since
    Big Sur and a rounded rect next to it looks subtly wrong at every size.
  * Occupying 824 of 1024 points, which is Apple's own proportion. An icon
    drawn edge to edge is visibly larger than every other icon in the Dock.
  * Three code lines and a caret, in the editor's own tomorrow-night accent
    colours. It reads as "lines of text" at 16px, where anything more literal
    -- a glyph, a document corner, a pen -- turns to mush.
  * Drawn at 4x and downsampled. Pillow has no antialiased polygon fill, so
    supersampling is the only way to get clean curves out of it.
"""

import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(os.path.dirname(HERE), "assets")

# tomorrow-night, the same values as widget/src/palette.cpp
BG_TOP = (0x2B, 0x2D, 0x30)
BG_BOTTOM = (0x1A, 0x1C, 0x1E)
PURPLE = (0xB2, 0x94, 0xBB)   # keyword
GREEN = (0xB5, 0xBD, 0x68)    # string
BLUE = (0x81, 0xA2, 0xBE)     # function name
ORANGE = (0xDE, 0x93, 0x5F)   # constant
CARET = (0xE8, 0xE9, 0xE7)

SS = 4          # supersampling factor
CANVAS = 1024   # design canvas, in points


def squircle(draw, box, fill, n=5.0, steps=720):
    """Superellipse |x/a|^n + |y/b|^n = 1, as a filled polygon.

    n=5 is the shape macOS uses. n=2 would be an ellipse and n=infinity a
    rectangle; the whole point is that it is neither.
    """
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    a, b = (x1 - x0) / 2.0, (y1 - y0) / 2.0
    pts = []
    for i in range(steps):
        t = 2.0 * 3.141592653589793 * i / steps
        ct, st = __import__("math").cos(t), __import__("math").sin(t)
        x = cx + a * (abs(ct) ** (2.0 / n)) * (1 if ct >= 0 else -1)
        y = cy + b * (abs(st) ** (2.0 / n)) * (1 if st >= 0 else -1)
        pts.append((x, y))
    draw.polygon(pts, fill=fill)


def vertical_gradient(size, top, bottom):
    img = Image.new("RGB", (1, size))
    for y in range(size):
        t = y / max(1, size - 1)
        img.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return img.resize((size, size), Image.BILINEAR)


def render(size_pt=CANVAS, simple=None):
    """The icon at `size_pt`, supersampled and downsampled. RGBA.

    `simple` drops to two thicker code lines and a caret. At 16 and 32 points a
    bar from the full design is under a pixel tall and three of them stack into
    a grey smudge; the icon has to be REDRAWN for those sizes, not scaled into
    them. Every icon set that works does this.
    """
    if simple is None:
        simple = size_pt <= 32
    S = size_pt * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # The squircle as a mask, so the gradient can be pasted through it. Filling
    # the polygon with a flat colour and drawing the gradient on top would need
    # a second mask anyway.
    mask = Image.new("L", (S, S), 0)
    inset = int(S * (CANVAS - 824) / 2 / CANVAS)
    squircle(ImageDraw.Draw(mask), (inset, inset, S - inset, S - inset), 255)

    body = vertical_gradient(S, BG_TOP, BG_BOTTOM).convert("RGBA")
    img.paste(body, (0, 0), mask)

    # A one-pixel-ish inner highlight along the top edge, which is what stops a
    # dark icon reading as a hole at large sizes.
    edge = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ed = ImageDraw.Draw(edge)
    squircle(ed, (inset, inset, S - inset, S - inset), (255, 255, 255, 38))
    inner = Image.new("L", (S, S), 0)
    grow = int(S * 0.010)
    squircle(ImageDraw.Draw(inner), (inset + grow, inset + grow,
                                     S - inset - grow, S - inset - grow), 255)
    edge.putalpha(Image.composite(Image.new("L", (S, S), 0), edge.split()[3], inner))
    img.alpha_composite(edge)

    d = ImageDraw.Draw(img)

    # Three code lines and a caret. Positions are fractions of the canvas so
    # the same call renders every size identically.
    def bar(x0f, x1f, yf, colour, hf=0.052):
        x0, x1 = x0f * S, x1f * S
        y = yf * S
        h = hf * S
        d.rounded_rectangle([x0, y - h / 2, x1, y + h / 2], radius=h / 2, fill=colour)

    if simple:
        bar(0.255, 0.470, 0.390, PURPLE, hf=0.105)
        bar(0.255, 0.400, 0.610, BLUE, hf=0.105)
        cw, ch = 0.075 * S, 0.230 * S
        cx, cy = 0.600 * S, 0.610 * S
    else:
        bar(0.255, 0.430, 0.345, PURPLE)
        bar(0.455, 0.620, 0.345, BLUE)

        bar(0.325, 0.470, 0.500, GREEN)
        bar(0.495, 0.600, 0.500, ORANGE)

        bar(0.255, 0.360, 0.655, PURPLE)
        bar(0.385, 0.560, 0.655, BLUE)

        cw, ch = 0.030 * S, 0.108 * S
        cx, cy = 0.665 * S, 0.500 * S

    # The caret sits at the end of a line, where an insertion point actually
    # is. Taller than the bars and square-ended, so it reads as a cursor rather
    # than one more word.
    d.rectangle([cx - cw / 2, cy - ch / 2, cx + cw / 2, cy + ch / 2], fill=CARET)

    return img.resize((size_pt, size_pt), Image.LANCZOS)


def write_icns(path, images):
    """images: {ostype: PIL.Image}. Written as PNG payloads, 10.7+."""
    chunks = b""
    for ostype, im in images.items():
        from io import BytesIO
        buf = BytesIO()
        im.save(buf, format="PNG")
        data = buf.getvalue()
        chunks += ostype.encode("ascii") + struct.pack(">I", len(data) + 8) + data
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)


def main():
    os.makedirs(ASSETS, exist_ok=True)

    # Rendered per size rather than scaled from one master: the bars are only
    # a few pixels tall at 16, and a LANCZOS downscale of a 1024 render keeps
    # them crisper than anything drawn directly at that size would be, but the
    # squircle inset has to be recomputed or it rounds to nothing.
    sizes = [16, 32, 64, 128, 256, 512, 1024]
    rendered = {s: render(s) for s in sizes}

    # The exact type codes iconutil emits for a standard .iconset.
    write_icns(os.path.join(ASSETS, "anyedit.icns"), {
        "icp4": rendered[16],    # 16x16
        "ic11": rendered[32],    # 16x16@2x
        "icp5": rendered[32],    # 32x32
        "ic12": rendered[64],    # 32x32@2x
        "ic07": rendered[128],   # 128x128
        "ic13": rendered[256],   # 128x128@2x
        "ic08": rendered[256],   # 256x256
        "ic14": rendered[512],   # 256x256@2x
        "ic09": rendered[512],   # 512x512
        "ic10": rendered[1024],  # 512x512@2x
    })

    # Pillow writes a multi-size ICO from one image plus a size list, but it
    # downscales internally with a cheaper filter, so the sizes are handed over
    # already rendered.
    ico_sizes = [16, 24, 32, 48, 64, 128, 256]
    ico = {s: (rendered[s] if s in rendered else render(s)) for s in ico_sizes}
    ico[256].save(os.path.join(ASSETS, "anyedit.ico"), format="ICO",
                  sizes=[(s, s) for s in ico_sizes],
                  append_images=[ico[s] for s in ico_sizes if s != 256])

    for s in [16, 32, 48, 64, 128, 256, 512]:
        im = rendered[s] if s in rendered else render(s)
        im.save(os.path.join(ASSETS, "anyedit-%d.png" % s))

    for name in sorted(os.listdir(ASSETS)):
        print("  %-20s %d bytes" % (name, os.path.getsize(os.path.join(ASSETS, name))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
