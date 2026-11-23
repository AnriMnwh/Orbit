"""Draws the Orbit logo.

The mark is a ring seen slightly from the side, thick where it is near and
thin where it is far, with a small body sitting in a gap on its far side: an
orbit, and a round watch with its crown.

  python tools/gen_logo.py           -> main/assets/gen/logo.c + logo.h (the watch's boot mark)
  python tools/gen_logo.py --brand   -> also assets/logo/: SVGs, Android icon vectors, PNGs

Everything is worked out from the few numbers below, on a 108 x 108 canvas
(the size of an Android adaptive icon, whose safe zone is the middle 66).
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "main" / "assets" / "gen"
BRAND = ROOT / "assets" / "logo"
TTF = ROOT / "assets" / "fonts" / "Montserrat-VariableFont.ttf"

# -- geometry -------------------------------------------------------------
C = (54.0, 54.0)                 # centre of the ring
RO = 27.0                        # outer edge of the ring
RI = 19.5                        # the hole, pushed towards the body so the ring thins there
LEAN = 3.0
TOWARD = math.radians(-45)       # the body sits up and to the right
U = (math.cos(TOWARD), math.sin(TOWARD))
HOLE = (C[0] + LEAN * U[0], C[1] + LEAN * U[1])
BODY_AT = 24.75                  # how far out the body sits: the middle of the thin part
BODY = (C[0] + BODY_AT * U[0], C[1] + BODY_AT * U[1])
BODY_R = 6.0
GAP = BODY_R + 3.6               # the ring is cut away this far from the body's centre

# -- colours --------------------------------------------------------------
NEAR = (0x7A, 0xB0, 0xFF)        # the ring where it is thick: sky blue
FAR = (0xC9, 0xB3, 0xFF)         # and where it is thin: soft violet
WARM = (0xFF, 0xB6, 0x8C)        # the body: peach
INK = (0x0F, 0x11, 0x15)         # the dark it sits on
PAPER = (0xF1, 0xF3, 0xF4)       # text on dark
# Deeper versions for light backgrounds.
NEAR_L, FAR_L, WARM_L = (0x2F, 0x6F, 0xE6), (0x7E, 0x5B, 0xE8), (0xE8, 0x7A, 0x33)


def hexc(c):
    return "#%02X%02X%02X" % c


def meet(c0, r0, c1, r1):
    """The two points where two circles cross."""
    dx, dy = c1[0] - c0[0], c1[1] - c0[1]
    d = math.hypot(dx, dy)
    a = (r0 * r0 - r1 * r1 + d * d) / (2 * d)
    h = math.sqrt(r0 * r0 - a * a)
    mx, my = c0[0] + a * dx / d, c0[1] + a * dy / d
    return (mx + h * dy / d, my - h * dx / d), (mx - h * dy / d, my + h * dx / d)


def angle(c, p):
    return math.atan2(p[1] - c[1], p[0] - c[0])


def turn(a, b):
    """Signed angle from a to b, the short way. Positive is clockwise on screen."""
    return (b - a + math.pi) % (2 * math.pi) - math.pi


def ring_path(scale=1.0, dx=0.0, dy=0.0):
    """The ring with its gap, as one SVG path made only of arcs."""
    def side(points, centre):
        # the crossing that comes before the body going clockwise, then the one after
        before = min(points, key=lambda p: turn(TOWARD, angle(centre, p)))
        after = max(points, key=lambda p: turn(TOWARD, angle(centre, p)))
        return before, after

    a1, a2 = side(meet(C, RO, BODY, GAP), C)
    b1, b2 = side(meet(HOLE, RI, BODY, GAP), HOLE)

    def pt(p):
        return "%.3f,%.3f" % (p[0] * scale + dx, p[1] * scale + dy)

    def arc(r, big, sweep, to):
        return "A%.3f,%.3f 0 %d %d %s" % (r * scale, r * scale, big, sweep, pt(to))

    cut1 = 1 if turn(angle(BODY, a1), angle(BODY, b1)) > 0 else 0
    cut2 = 1 if turn(angle(BODY, b2), angle(BODY, a2)) > 0 else 0
    return " ".join(["M" + pt(a2), arc(RO, 1, 1, a1), arc(GAP, 0, cut1, b1), arc(RI, 1, 0, b2), arc(GAP, 0, cut2, a2), "Z"])


def body_path(scale=1.0, dx=0.0, dy=0.0):
    x, y, r = BODY[0] * scale + dx, BODY[1] * scale + dy, BODY_R * scale
    return "M%.3f,%.3f a%.3f,%.3f 0 1 0 %.3f,0 a%.3f,%.3f 0 1 0 %.3f,0 Z" % (x - r, y, r, r, 2 * r, r, r, -2 * r)


def grad_ends(scale=1.0, dx=0.0, dy=0.0):
    """The gradient runs across the ring, from its near side to the body."""
    return ((C[0] - RO * U[0]) * scale + dx, (C[1] - RO * U[1]) * scale + dy,
            (C[0] + RO * U[0]) * scale + dx, (C[1] + RO * U[1]) * scale + dy)


# -- raster ---------------------------------------------------------------
def masks(size, box, ss=4):
    """Coverage of the ring and of the body, for the canvas square `box`
    (x, y, side in canvas units) drawn `size` pixels wide."""
    k = size * ss / box[2]

    def circle(d, c, r, fill):
        x, y = (c[0] - box[0]) * k, (c[1] - box[1]) * k
        d.ellipse((x - r * k, y - r * k, x + r * k, y + r * k), fill=fill)

    ring = Image.new("L", (size * ss, size * ss), 0)
    d = ImageDraw.Draw(ring)
    circle(d, C, RO, 255)
    circle(d, HOLE, RI, 0)
    circle(d, BODY, GAP, 0)
    body = Image.new("L", (size * ss, size * ss), 0)
    circle(ImageDraw.Draw(body), BODY, BODY_R, 255)
    small = (size, size)
    return ring.resize(small, Image.LANCZOS), body.resize(small, Image.LANCZOS)


def blend(size, box, near, far):
    """The ring's colour at every pixel."""
    im = Image.new("RGB", (size, size))
    px = im.load()
    k = box[2] / size
    for y in range(size):
        for x in range(size):
            cx, cy = box[0] + (x + 0.5) * k - C[0], box[1] + (y + 0.5) * k - C[1]
            t = min(1.0, max(0.0, (cx * U[0] + cy * U[1] + RO) / (2 * RO)))
            px[x, y] = tuple(round(near[i] + (far[i] - near[i]) * t) for i in range(3))
    return im


def picture(size, box, near=NEAR, far=FAR, warm=WARM, back=None):
    """The mark as an RGBA picture, optionally on a solid background."""
    ring, body = masks(size, box)
    out = Image.new("RGBA", (size, size), (back + (255,)) if back else (0, 0, 0, 0))
    out.paste(blend(size, box, near, far), (0, 0), ring)
    out.paste(Image.new("RGB", (size, size), warm), (0, 0), body)
    return out


# -- the watch ------------------------------------------------------------
RING_PX = 150   # the ring as drawn at boot; the canvas square below fills it
WATCH_BOX = (C[0] - 33, C[1] - 33, 66)
DOT_PX = 34


def emit_watch():
    GEN.mkdir(parents=True, exist_ok=True)
    k = RING_PX / WATCH_BOX[2]
    ring, _ = masks(RING_PX, WATCH_BOX)
    colour = blend(RING_PX, WATCH_BOX, NEAR, FAR)
    rgb = bytearray()
    for r, g, b in colour.getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        rgb += bytes((v & 0xFF, v >> 8))
    ring_bytes = bytes(rgb) + ring.tobytes()

    half = DOT_PX / k / 2
    _, dot = masks(DOT_PX, (BODY[0] - half, BODY[1] - half, 2 * half))

    def table(name, data):
        rows = [f"static const uint8_t {name}[] = {{"]
        rows += ["    " + ",".join(str(b) for b in data[o:o + 24]) + "," for o in range(0, len(data), 24)]
        return rows + ["};"]

    c = ["/* Generated by tools/gen_logo.py. Do not edit. */", '#include "logo.h"', ""]
    c += table("logo_ring_map", ring_bytes)
    c += ["const lv_image_dsc_t logo_ring = {",
          "    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,",
          f"                .w = {RING_PX}, .h = {RING_PX}, .stride = {RING_PX * 2} }},",
          "    .data_size = sizeof(logo_ring_map), .data = logo_ring_map,", "};", ""]
    c += table("logo_dot_map", dot.tobytes())
    c += ["const lv_image_dsc_t logo_dot = {",
          "    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,",
          f"                .w = {DOT_PX}, .h = {DOT_PX}, .stride = {DOT_PX} }},",
          "    .data_size = sizeof(logo_dot_map), .data = logo_dot_map,", "};", ""]
    h = ["/* Generated by tools/gen_logo.py. Do not edit. */", "#pragma once", '#include "lvgl.h"', "",
         "/* The Orbit mark for the boot screen: the ring in its own colours, and",
         " * the body that circles it as a separate one-colour picture. */",
         "extern const lv_image_dsc_t logo_ring, logo_dot;",
         f"#define LOGO_ORBIT_PX {round(BODY_AT * k)}   /* the body's distance from the ring's centre */",
         f"#define LOGO_BODY_COLOR 0x{WARM[0]:02X}{WARM[1]:02X}{WARM[2]:02X}", ""]
    (GEN / "logo.c").write_text("\n".join(c) + "\n")
    (GEN / "logo.h").write_text("\n".join(h) + "\n")
    print(f"logo: ring {RING_PX}px, dot {DOT_PX}px -> {GEN}")


# -- the brand files ------------------------------------------------------
def wordmark(size, weight=600, spacing=0.02):
    """'Orbit' in Montserrat as an SVG path whose baseline is y = 0. Returns (path, width)."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    font = instancer.instantiateVariableFont(TTFont(str(TTF)), {"wght": weight})
    glyphs, cmap, k = font.getGlyphSet(), font.getBestCmap(), size / font["head"].unitsPerEm
    x, parts = 0.0, []
    for ch in "Orbit":
        name = cmap[ord(ch)]
        pen = SVGPathPen(glyphs, ntos=lambda v: "%.2f" % v)
        glyphs[name].draw(TransformPen(pen, (k, 0, 0, -k, x, 0)))
        parts.append(pen.getCommands())
        x += (font["hmtx"][name][0] + spacing * font["head"].unitsPerEm) * k
    return " ".join(parts), x - spacing * size


def svg_mark(near, far, warm, mono=None):
    x0, y0, x1, y1 = grad_ends()
    fill = mono or "url(#ring)"
    defs = "" if mono else (f'<defs><linearGradient id="ring" gradientUnits="userSpaceOnUse" x1="{x0:.2f}" y1="{y0:.2f}" '
                            f'x2="{x1:.2f}" y2="{y1:.2f}"><stop offset="0" stop-color="{hexc(near)}"/>'
                            f'<stop offset="1" stop-color="{hexc(far)}"/></linearGradient></defs>')
    return defs + f'<path fill="{fill}" d="{ring_path()}"/><path fill="{mono or hexc(warm)}" d="{body_path()}"/>'


def svg(inner, w=108, h=108):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w * 4}" height="{h * 4}">'
            f"{inner}</svg>\n")


def android_vector(body, extra=""):
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            "<!-- The Orbit mark. Generated by tools/gen_logo.py in the watch project: change it there. -->\n"
            '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    xmlns:aapt="http://schemas.android.com/aapt"\n'
            f'    android:width="108dp"\n    android:height="108dp"\n    android:viewportWidth="108"\n'
            f'    android:viewportHeight="108"{extra}>\n{body}</vector>\n')


def emit_brand():
    (BRAND / "android").mkdir(parents=True, exist_ok=True)
    (BRAND / "png").mkdir(parents=True, exist_ok=True)
    x0, y0, x1, y1 = grad_ends()

    # SVG: the mark alone (for dark and for light backgrounds, and in one colour), the app icon, the lockup.
    (BRAND / "orbit-mark.svg").write_text(svg(svg_mark(NEAR, FAR, WARM)))
    (BRAND / "orbit-mark-on-light.svg").write_text(svg(svg_mark(NEAR_L, FAR_L, WARM_L)))
    (BRAND / "orbit-mark-mono.svg").write_text(svg(svg_mark(None, None, None, mono="#FFFFFF")))
    (BRAND / "orbit-icon.svg").write_text(svg(f'<rect width="108" height="108" rx="24" fill="{hexc(INK)}"/>' + svg_mark(NEAR, FAR, WARM)))
    word, width = wordmark(46)
    for name, ink, colours in (("orbit-lockup.svg", PAPER, (NEAR, FAR, WARM)), ("orbit-lockup-on-light.svg", INK, (NEAR_L, FAR_L, WARM_L))):
        total = 92 + width + 14
        inner = (f'<g transform="translate(-10,-10)">{svg_mark(*colours)}</g>'
                 f'<path fill="{hexc(ink)}" transform="translate(88,60.5)" d="{word}"/>')
        (BRAND / name).write_text(svg(inner, round(total), 88))

    # Android: adaptive icon layers and a status-bar icon.
    grad = ('        <aapt:attr name="android:fillColor">\n'
            f'            <gradient android:type="linear" android:startX="{x0:.2f}" android:startY="{y0:.2f}"\n'
            f'                android:endX="{x1:.2f}" android:endY="{y1:.2f}"\n'
            f'                android:startColor="{hexc(NEAR)}" android:endColor="{hexc(FAR)}" />\n'
            "        </aapt:attr>\n")
    (BRAND / "android" / "ic_launcher_foreground.xml").write_text(android_vector(
        f'    <path android:pathData="{ring_path()}">\n{grad}    </path>\n'
        f'    <path android:fillColor="{hexc(WARM)}" android:pathData="{body_path()}" />\n'))
    (BRAND / "android" / "ic_launcher_monochrome.xml").write_text(android_vector(
        f'    <path android:fillColor="#FFFFFF" android:pathData="{ring_path()}" />\n'
        f'    <path android:fillColor="#FFFFFF" android:pathData="{body_path()}" />\n'))
    (BRAND / "android" / "ic_launcher_background.xml").write_text(android_vector(
        '    <path android:pathData="M0,0h108v108h-108z">\n'
        '        <aapt:attr name="android:fillColor">\n'
        '            <gradient android:type="radial" android:centerX="54" android:centerY="54" android:gradientRadius="60"\n'
        '                android:startColor="#1B1F2A" android:endColor="#0B0C10" />\n'
        "        </aapt:attr>\n    </path>\n"))
    (BRAND / "android" / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@drawable/ic_launcher_background" />\n'
        '    <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'
        '    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />\n'
        "</adaptive-icon>\n")
    # Status bar: white, 24 dp, the mark enlarged to fill the square.
    grow = 108 * 0.92 / (2 * 30.75)
    stat = ('<?xml version="1.0" encoding="utf-8"?>\n'
            "<!-- The Orbit mark for the status bar. Generated by tools/gen_logo.py in the watch project. -->\n"
            '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    android:width="24dp"\n    android:height="24dp"\n    android:viewportWidth="108"\n    android:viewportHeight="108">\n'
            f'    <group android:pivotX="54" android:pivotY="54" android:scaleX="{grow:.3f}" android:scaleY="{grow:.3f}"\n'
            '        android:translateX="-3" android:translateY="3">\n'
            f'        <path android:fillColor="#FFFFFF" android:pathData="{ring_path()}" />\n'
            f'        <path android:fillColor="#FFFFFF" android:pathData="{body_path()}" />\n'
            "    </group>\n</vector>\n")
    (BRAND / "android" / "ic_stat_orbit.xml").write_text(stat)

    # PNG: transparent mark, store icon, and a preview sheet.
    full = (0, 0, 108)
    picture(1024, full).save(BRAND / "png" / "orbit-mark-1024.png")
    picture(1024, full, NEAR_L, FAR_L, WARM_L).save(BRAND / "png" / "orbit-mark-on-light-1024.png")
    icon = picture(512, full, back=INK)
    icon.save(BRAND / "png" / "orbit-icon-512.png")
    # The lockup as a picture: the mark with the name beside it, on the dark.
    from PIL import ImageFont
    k = 8  # pixels per canvas unit
    font = ImageFont.truetype(str(TTF), 46 * k)
    font.set_variation_by_axes([600])
    lock = Image.new("RGB", (round((92 + width + 14) * k), 88 * k), INK)
    mark = picture(108 * k, full)
    lock.paste(mark, (-10 * k, -10 * k), mark)
    ImageDraw.Draw(lock).text((88 * k, round(60.5 * k)), "Orbit", font=font, fill=PAPER, anchor="ls")
    lock.resize((lock.width // 4, lock.height // 4), Image.LANCZOS).save(BRAND / "png" / "orbit-lockup.png")
    print(f"brand: wrote {BRAND}")


if __name__ == "__main__":
    emit_watch()
    if "--brand" in sys.argv:
        emit_brand()
