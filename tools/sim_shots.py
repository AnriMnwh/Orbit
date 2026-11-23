"""Helpers for the desktop simulator.

  python tools/sim_shots.py data  <dir>            write sample photos the sim loads
  python tools/sim_shots.py sheet <shots> <out>    turn .rgb565 dumps into PNGs + sheets
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SCR = 466


def to_rgb565(im):
    out = bytearray()
    for r, g, b in im.convert("RGB").getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += bytes((v & 0xFF, v >> 8))
    return bytes(out)


def from_rgb565(raw):
    px = []
    for i in range(0, len(raw), 2):
        v = raw[i] | (raw[i + 1] << 8)
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        px.append((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    im = Image.new("RGB", (SCR, SCR))
    im.putdata(px)
    return im


def sample_photo(n):
    """Abstract landscapes: enough structure to judge legibility over a photo."""
    skies = [((255, 170, 110), (60, 40, 110)), ((120, 190, 255), (20, 60, 130)), ((250, 120, 150), (40, 20, 70))]
    top, bottom = skies[n % 3]
    im = Image.new("RGB", (SCR, SCR))
    d = ImageDraw.Draw(im)
    for y in range(SCR):
        t = y / SCR
        d.line([(0, y), (SCR, y)], fill=tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3)))
    d.ellipse((300 - n * 90, 70, 380 - n * 90, 150), fill=(255, 240, 210))
    for layer, shade in enumerate((0.55, 0.35, 0.18)):
        pts = [(0, SCR)]
        for x in range(0, SCR + 1, 6):
            h = 250 + layer * 55 + 38 * math.sin(x / (60.0 + 25 * layer) + n * 2 + layer) + 16 * math.sin(x / 17.0 + layer)
            pts.append((x, h))
        pts.append((SCR, SCR))
        d.polygon(pts, fill=tuple(int(c * shade) for c in bottom))
    return im


def write_data(dst):
    dst = Path(dst)
    dst.mkdir(parents=True, exist_ok=True)
    for n in range(3):
        (dst / f"photo_{n}.rgb565").write_bytes(to_rgb565(sample_photo(n)))
    print("sample photos ->", dst)


def round_crop(im):
    """Show what the round panel shows: everything outside the circle is bezel."""
    mask = Image.new("L", (SCR * 2, SCR * 2), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, SCR * 2 - 1, SCR * 2 - 1), fill=255)
    mask = mask.resize((SCR, SCR), Image.LANCZOS)
    bezel = Image.new("RGB", (SCR, SCR), (38, 40, 46))
    return Image.composite(im, bezel, mask)


def sheets(shots, out):
    shots, out = Path(shots), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    files = sorted(shots.glob("*.rgb565"))
    names = []
    for f in files:
        im = round_crop(from_rgb565(f.read_bytes()))
        im.save(out / (f.stem + ".png"))
        names.append(f.stem)
    cols, rows, pad, label = 3, 2, 14, 26
    per = cols * rows
    for s in range(0, len(names), per):
        batch = names[s:s + per]
        sheet = Image.new("RGB", (cols * (SCR + pad) + pad, rows * (SCR + pad + label) + pad), (22, 23, 27))
        d = ImageDraw.Draw(sheet)
        for i, name in enumerate(batch):
            x = pad + (i % cols) * (SCR + pad)
            y = pad + (i // cols) * (SCR + pad + label)
            sheet.paste(Image.open(out / (name + ".png")), (x, y + label))
            d.text((x + 6, y + 6), name, fill=(170, 174, 184))
        path = out / f"sheet_{s // per + 1}.png"
        sheet.save(path)
        print("sheet", path)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "data":
        write_data(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "sheet":
        sheets(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
