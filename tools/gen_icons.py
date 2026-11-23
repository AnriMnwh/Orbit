"""Draws Orbit's icon set and writes it as LVGL A8 images.

Icons are described on a 24x24 grid with a 2-unit round stroke, rendered at 8x
and downsampled, so every icon shares the same weight and corner feel.

  python tools/gen_icons.py            -> main/assets/gen/icons.c + icons.h
  python tools/gen_icons.py --sheet p  -> also writes a contact sheet PNG to p
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

SS = 8
OUT = Path(__file__).resolve().parent.parent / "main" / "assets" / "gen"


class Ic:
    def __init__(self, size):
        self.size = size
        self.k = size * SS / 24.0
        self.img = Image.new("L", (size * SS, size * SS), 0)

    # -- plumbing ---------------------------------------------------------
    def _layer(self):
        im = Image.new("L", self.img.size, 0)
        return im, ImageDraw.Draw(im)

    def _add(self, layer):
        self.img = ImageChops.lighter(self.img, layer)

    def _cut(self, layer):
        self.img = ImageChops.multiply(self.img, ImageChops.invert(layer))

    def _p(self, x, y):
        return (x * self.k, y * self.k)

    def _disc(self, d, x, y, r):
        cx, cy = self._p(x, y)
        rr = r * self.k
        d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=255)

    def _stroke(self, d, pts, w, closed):
        if closed:
            pts = list(pts) + [pts[0]]
        for a, b in zip(pts, pts[1:]):
            d.line([self._p(*a), self._p(*b)], fill=255, width=max(1, round(w * self.k)))
        for p in pts:
            self._disc(d, p[0], p[1], w / 2)

    # -- primitives -------------------------------------------------------
    def line(self, pts, w=2, closed=False):
        im, d = self._layer()
        self._stroke(d, pts, w, closed)
        self._add(im)

    def dot(self, x, y, r):
        im, d = self._layer()
        self._disc(d, x, y, r)
        self._add(im)

    def cut_dot(self, x, y, r):
        im, d = self._layer()
        self._disc(d, x, y, r)
        self._cut(im)

    def poly(self, pts, round_w=0):
        im, d = self._layer()
        d.polygon([self._p(*p) for p in pts], fill=255)
        if round_w:
            self._stroke(d, pts, round_w, True)
        self._add(im)

    def cut_poly(self, pts):
        im, d = self._layer()
        d.polygon([self._p(*p) for p in pts], fill=255)
        self._cut(im)

    @staticmethod
    def arc_pts(cx, cy, r, a0, a1, n=48):
        """Angles in degrees, 0 = east, clockwise on screen (y down)."""
        return [
            (cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)
        ]

    def arc(self, cx, cy, r, a0, a1, w=2):
        self.line(self.arc_pts(cx, cy, r, a0, a1), w)

    def ring(self, cx, cy, r, w=2):
        self.line(self.arc_pts(cx, cy, r, 0, 360, 72), w, closed=True)

    def rrect_pts(self, x0, y0, x1, y1, r):
        pts = []
        pts += self.arc_pts(x1 - r, y0 + r, r, -90, 0, 10)
        pts += self.arc_pts(x1 - r, y1 - r, r, 0, 90, 10)
        pts += self.arc_pts(x0 + r, y1 - r, r, 90, 180, 10)
        pts += self.arc_pts(x0 + r, y0 + r, r, 180, 270, 10)
        return pts

    def rrect(self, x0, y0, x1, y1, r, w=2):
        self.line(self.rrect_pts(x0, y0, x1, y1, r), w, closed=True)

    def rrect_fill(self, x0, y0, x1, y1, r):
        self.poly(self.rrect_pts(x0, y0, x1, y1, r))

    def done(self):
        return self.img.resize((self.size, self.size), Image.LANCZOS)


# -- the icons ------------------------------------------------------------
def clock(i):
    i.ring(12, 12, 9)
    i.line([(12, 7), (12, 12), (15.5, 14)])


def music(i):
    i.line([(9, 17.5), (9, 5.5), (19, 3.5), (19, 15.5)])
    i.dot(6.5, 17.5, 3)
    i.dot(16.5, 15.5, 3)
    i.line([(9, 9), (19, 7)], 1.6)


def keys(i):
    i.rrect_fill(3, 3, 10.5, 10.5, 2.2)
    i.rrect(13.5, 3, 21, 10.5, 2.2)
    i.rrect(3, 13.5, 10.5, 21, 2.2)
    i.rrect(13.5, 13.5, 21, 21, 2.2)


def bell(i):
    body = i.arc_pts(12, 10.5, 6, 180, 360, 24) + [(18, 15), (20, 17.5), (4, 17.5), (6, 15)]
    i.line(body, 2, closed=True)
    i.line([(12, 2.5), (12, 4.5)])
    i.arc(12, 19.5, 2, 20, 160, 2)


def pin(i):
    top = i.arc_pts(12, 9.5, 6.5, 150, 390, 32)
    i.line(top + [(12, 21.5)], 2, closed=True)
    i.ring(12, 9.5, 2.2, 1.8)


def camera(i):
    i.line([(8.5, 6.5), (10, 4), (14, 4), (15.5, 6.5)])
    i.rrect(2.5, 6.5, 21.5, 19.5, 2.5)
    i.ring(12, 13, 3.6)


def image(i):
    i.rrect(3, 4, 21, 20, 3)
    i.dot(8.5, 9.5, 1.7)
    i.line([(3.8, 17.5), (9, 13), (12.5, 16), (16, 12.5), (20.2, 16.5)])


def paw(i):
    i.dot(6.2, 10.2, 2.1)
    i.dot(10.2, 6.6, 2.2)
    i.dot(15, 6.9, 2.2)
    i.dot(18.6, 10.8, 2.1)
    i.poly([(12.4, 11.5), (16.8, 15), (17, 18.4), (14.2, 19.6), (12.2, 18.8),
            (10.2, 19.6), (7.4, 18.3), (7.8, 14.8)], round_w=2.6)


def spark(i):
    def star(cx, cy, r, pinch=0.26):
        pts = []
        for n in range(8):
            a = math.radians(n * 45 - 90)
            rr = r if n % 2 == 0 else r * pinch
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        i.poly(pts, round_w=0.7)
    star(10, 13, 8.5)
    star(18.8, 5.2, 3.6)


def gear(i):
    for n in range(8):
        a = math.radians(n * 45)
        i.line([(12 + 7.2 * math.cos(a), 12 + 7.2 * math.sin(a)),
                (12 + 9.6 * math.cos(a), 12 + 9.6 * math.sin(a))], 2.6)
    i.ring(12, 12, 6.2, 2.2)
    i.ring(12, 12, 2.4, 1.8)


def timer(i):
    i.ring(12, 13.5, 7.8)
    i.line([(9.5, 2.5), (14.5, 2.5)])
    i.line([(12, 13.5), (12, 9)])
    i.line([(18.6, 6.4), (19.8, 7.6)])


def play(i):
    i.poly([(8, 5), (19, 12), (8, 19)], round_w=2.2)


def pause(i):
    i.rrect_fill(6, 4.5, 10, 19.5, 1.4)
    i.rrect_fill(14, 4.5, 18, 19.5, 1.4)


def nxt(i):
    i.poly([(5, 6), (14.5, 12), (5, 18)], round_w=2)
    i.line([(18.5, 5.5), (18.5, 18.5)], 2.4)


def prev(i):
    i.poly([(19, 6), (9.5, 12), (19, 18)], round_w=2)
    i.line([(5.5, 5.5), (5.5, 18.5)], 2.4)


def _speaker(i):
    i.poly([(3.5, 9.5), (7, 9.5), (11.5, 5.5), (11.5, 18.5), (7, 14.5), (3.5, 14.5)], round_w=1.8)


def vol_up(i):
    _speaker(i)
    i.arc(11.5, 12, 4.2, -42, 42)
    i.arc(11.5, 12, 8, -42, 42)


def vol_down(i):
    _speaker(i)
    i.arc(11.5, 12, 4.2, -42, 42)


def bluetooth(i):
    i.line([(6.5, 7.5), (17, 16.5), (12, 21), (12, 3), (17, 7.5), (6.5, 16.5)])


def wifi(i):
    i.arc(12, 18.5, 12.5, 225, 315)
    i.arc(12, 18.5, 8, 225, 315)
    i.arc(12, 18.5, 3.8, 225, 315)
    i.dot(12, 18.6, 1.5)


def sun(i):
    i.ring(12, 12, 4)
    for n in range(8):
        a = math.radians(n * 45)
        i.line([(12 + 7.3 * math.cos(a), 12 + 7.3 * math.sin(a)),
                (12 + 9.4 * math.cos(a), 12 + 9.4 * math.sin(a))])


def moon(i):
    i.dot(11.5, 12.5, 9)
    i.cut_dot(16.5, 8.5, 7.6)


def torch(i):
    i.line([(7, 3.5), (17, 3.5), (17, 7), (14.5, 10.5), (14.5, 20.5), (9.5, 20.5), (9.5, 10.5), (7, 7)],
           2, closed=True)
    i.line([(12, 13), (12, 15.5)])


def bolt(i):
    i.poly([(13.5, 2.5), (5.5, 13.5), (11, 13.5), (10, 21.5), (18.5, 10), (12.8, 10)], round_w=1.4)


def back(i):
    i.line([(14.5, 5), (7.5, 12), (14.5, 19)], 2.4)


def check(i):
    i.line([(4.5, 12.5), (9.8, 17.8), (19.5, 6.5)], 2.4)


def close(i):
    i.line([(6, 6), (18, 18)], 2.4)
    i.line([(18, 6), (6, 18)], 2.4)


def plus(i):
    i.line([(12, 5), (12, 19)], 2.4)
    i.line([(5, 12), (19, 12)], 2.4)


def minus(i):
    i.line([(5, 12), (19, 12)], 2.4)


def mic(i):
    i.rrect(9, 2.5, 15, 14, 3)
    i.arc(12, 11, 6.5, 0, 180)
    i.line([(12, 17.5), (12, 21)])
    i.line([(8.5, 21), (15.5, 21)])


def phone(i):
    i.rrect(6.5, 2.5, 17.5, 21.5, 2.6)
    i.line([(11, 18), (13, 18)])


def shutter(i):
    i.ring(12, 12, 9.2)
    i.dot(12, 12, 5.6)


def video(i):
    i.rrect(2.5, 6.5, 15.5, 17.5, 2.4)
    i.line([(15.5, 10.5), (21, 7.5), (21, 16.5), (15.5, 13.5)], 2, closed=True)


def apple(i):
    i.ring(12, 14.2, 6.8)
    i.line([(12, 7.4), (12.6, 4)])
    i.line([(13.6, 5.2), (16.6, 3.6)], 2.4)


def ball(i):
    i.ring(12, 12, 9)
    i.arc(3.5, 3.5, 9.5, 12, 78)
    i.arc(20.5, 20.5, 9.5, 192, 258)


def drop(i):
    pts = [(12, 2.8)] + i.arc_pts(12, 14.2, 6.4, -42, 222, 40)
    i.line(pts, 2, closed=True)


def heart(i):
    left = i.arc_pts(8.1, 9, 4.4, 150, 330, 20)
    right = i.arc_pts(15.9, 9, 4.4, 210, 390, 20)
    i.line(left + right + [(12, 20)], 2, closed=True)


def steps(i):
    i.line([(3.5, 19.5), (8.5, 19.5), (8.5, 14.5), (13.5, 14.5), (13.5, 9.5), (18.5, 9.5), (18.5, 4.5), (21, 4.5)], 2.2)


def power(i):
    i.arc(12, 13, 8, -55, 235)
    i.line([(12, 3), (12, 12)])


def target(i):
    i.ring(12, 12, 6.5)
    i.dot(12, 12, 2)
    for a, b in (((12, 2), (12, 5)), ((12, 19), (12, 22)), ((2, 12), (5, 12)), ((19, 12), (22, 12))):
        i.line([a, b])


def link(i):
    i.rrect(3, 8, 13, 16, 4)
    i.rrect(11, 8, 21, 16, 4)


def trash(i):
    i.line([(4, 6.5), (20, 6.5)])
    i.line([(9.5, 6.5), (9.5, 3.5), (14.5, 3.5), (14.5, 6.5)])
    i.line([(6, 6.5), (7, 20.5), (17, 20.5), (18, 6.5)])


def chat(i):
    i.rrect(3, 3.5, 21, 16.5, 4)
    i.cut_poly([(7.6, 15), (11.4, 15), (11.4, 18), (7.6, 18)])
    i.line([(7, 16.5), (7, 21), (11.8, 16.5)])


def face(i):
    i.ring(12, 12, 9)
    i.dot(8.7, 10, 1.3)
    i.dot(15.3, 10, 1.3)
    i.arc(12, 12.2, 4.6, 25, 155)


def compass(i):
    i.ring(12, 12, 9)
    i.poly([(15.8, 8.2), (13.4, 13.4), (8.2, 15.8), (10.6, 10.6)], round_w=1)
    i.cut_dot(12, 12, 1.1)


def info(i):
    i.ring(12, 12, 9)
    i.line([(12, 11), (12, 16.5)], 2.2)
    i.dot(12, 7.6, 1.3)


def aperture(i):
    i.ring(12, 12, 9)
    for n in range(6):
        a = math.radians(n * 60 + 20)
        b = math.radians(n * 60 + 105)
        i.line([(12 + 9 * math.cos(a), 12 + 9 * math.sin(a)),
                (12 + 4.4 * math.cos(b), 12 + 4.4 * math.sin(b))], 1.5)


def stop(i):
    i.rrect_fill(6, 6, 18, 18, 2.4)


def restart(i):
    i.arc(12, 12.5, 8, -50, 250)
    i.line([(12.5, 1.6), (16.6, 5.4), (12.2, 8.6)], 2.2)


def zzz(i):
    i.line([(4, 5), (11, 5), (4, 13), (11, 13)], 2.2)
    i.line([(14, 12), (20, 12), (14, 19.5), (20, 19.5)], 2)


def rec(i):
    i.dot(12, 12, 7.2)


def alarm(i):
    i.ring(12, 13.5, 7.5)
    i.line([(12, 9.5), (12, 13.5), (14.8, 15.2)])
    i.line([(3.2, 6.6), (6.6, 3.4)], 2.2)
    i.line([(20.8, 6.6), (17.4, 3.4)], 2.2)


def hourglass(i):
    i.line([(6, 3), (18, 3)], 2.2)
    i.line([(6, 21), (18, 21)], 2.2)
    i.line([(7.5, 3.5), (7.5, 6.5), (12, 12), (16.5, 6.5), (16.5, 3.5)])
    i.line([(7.5, 20.5), (7.5, 17.5), (12, 12), (16.5, 17.5), (16.5, 20.5)])


def cloud(i):
    pts = i.arc_pts(7.5, 14.5, 4.5, 90, 250, 16) + i.arc_pts(12.5, 10, 5.2, 195, 345, 20) + \
          i.arc_pts(17.2, 14.6, 4.4, 285, 450, 16)
    i.line(pts, 2, closed=True)


def wind(i):
    i.line([(3, 9.5), (13.5, 9.5)] + i.arc_pts(13.5, 6.8, 2.7, 90, -150, 16))
    i.line([(3, 14.5), (17.5, 14.5)] + i.arc_pts(17.5, 17.2, 2.7, -90, 150, 16))
    i.line([(3, 19.2), (9.5, 19.2)], 2)


def calc(i):
    i.rrect(4.5, 2.5, 19.5, 21.5, 3)
    i.line([(8.2, 7), (15.8, 7)], 2.2)
    for y in (12.2, 16.8):
        for x in (8.4, 12, 15.6):
            i.dot(x, y, 1.25)


def piano(i):
    i.rrect(3, 5, 21, 19, 2.5)
    i.line([(9, 13), (9, 19)], 1.6)
    i.line([(15, 13), (15, 19)], 1.6)
    i.rrect_fill(7.4, 5, 10.6, 12.5, 0.8)
    i.rrect_fill(13.4, 5, 16.6, 12.5, 0.8)


def metronome(i):
    i.line([(8.5, 3.5), (15.5, 3.5), (19, 20.5), (5, 20.5)], 2, closed=True)
    i.line([(12, 17), (16.5, 6.5)], 1.8)
    i.dot(15.2, 9.6, 1.9)


def tuner(i):
    i.arc(12, 16.5, 9, 200, 340)
    i.line([(12, 16.5), (16, 9)], 2.2)
    i.dot(12, 16.5, 2)
    i.line([(12, 4.5), (12, 6.5)], 1.8)


def level(i):
    i.ring(12, 12, 9)
    i.ring(12, 12, 3.2, 1.8)
    i.line([(2, 12), (5.5, 12)], 1.8)
    i.line([(18.5, 12), (22, 12)], 1.8)


def dice(i):
    i.rrect(3.5, 3.5, 20.5, 20.5, 4)
    for x, y in ((8, 8), (16, 8), (12, 12), (8, 16), (16, 16)):
        i.dot(x, y, 1.5)


def sunrise(i):
    i.arc(12, 16, 5, 180, 360)
    i.line([(2.5, 16), (21.5, 16)])
    i.line([(6, 20), (18, 20)], 1.8)
    for a in (200, 235, 270, 305, 340):
        r = math.radians(a)
        i.line([(12 + 7.4 * math.cos(r), 16 + 7.4 * math.sin(r)),
                (12 + 9.6 * math.cos(r), 16 + 9.6 * math.sin(r))], 1.8)


def dumbbell(i):
    i.line([(8, 12), (16, 12)], 2.4)
    i.rrect_fill(4.5, 6.5, 8, 17.5, 1.4)
    i.rrect_fill(16, 6.5, 19.5, 17.5, 1.4)
    i.line([(2.5, 9.5), (2.5, 14.5)], 2)
    i.line([(21.5, 9.5), (21.5, 14.5)], 2)


def monitor(i):
    i.rrect(2.5, 4, 21.5, 16.5, 2.5)
    i.line([(12, 16.5), (12, 20.5)])
    i.line([(8, 20.5), (16, 20.5)])


def tally(i):
    for x in (5.5, 9.5, 13.5, 17.5):
        i.line([(x, 5), (x, 19)], 2)
    i.line([(3, 16.5), (20.5, 7.5)], 2)


def shuffle(i):
    i.line([(3, 7), (7, 7), (15, 17), (20.5, 17)])
    i.line([(3, 17), (7, 17), (9.3, 14.2)])
    i.line([(12.7, 9.8), (15, 7), (20.5, 7)])
    i.line([(18, 4.2), (21, 7), (18, 9.8)])
    i.line([(18, 14.2), (21, 17), (18, 19.8)])


def repeat(i):
    i.line([(17.5, 3), (20.5, 6), (17.5, 9)])
    i.line([(3.5, 11), (3.5, 9), (6.5, 6), (20, 6)])
    i.line([(6.5, 21), (3.5, 18), (6.5, 15)])
    i.line([(20.5, 13), (20.5, 15), (17.5, 18), (4, 18)])


def calendar(i):
    i.rrect(3.5, 5, 20.5, 20.5, 2.6)
    i.line([(3.5, 10.2), (20.5, 10.2)], 2)
    i.line([(8, 2.6), (8, 6.4)], 2.2)
    i.line([(16, 2.6), (16, 6.4)], 2.2)
    for x in (8, 12, 16):
        i.dot(x, 15.2, 1.35)


def grid(i):
    for y in (5, 12, 19):
        for x in (5, 12, 19):
            i.dot(x, y, 2.3)


def half(i):
    i.ring(12, 12, 9)
    i.poly(i.arc_pts(12, 12, 9, -90, 90, 24))


def wave(i):
    for y in (7.5, 12.5, 17.5):
        i.line(i.arc_pts(6.5, y + 2.6, 4.4, 217, 323, 12) + i.arc_pts(13.5, y - 2.6, 4.4, 143, 37, 12)
               + i.arc_pts(20.5, y + 2.6, 4.4, 217, 270, 8), 2)


def gem(i):
    i.ring(12, 12, 9.5)
    i.line([(12, 5.5), (17, 12), (12, 18.5), (7, 12)], 2, closed=True)
    i.dot(12, 12, 1.6)


def tv(i):
    i.rrect(2.5, 6.5, 21.5, 20.5, 3)
    i.line([(8, 2.5), (12, 6.5), (16, 2.5)], 2)
    i.rrect_fill(5.6, 9.6, 14.6, 17.4, 1.2)
    i.dot(18.2, 11, 1.2)
    i.dot(18.2, 15.6, 1.2)


def cup(i):
    i.line([(5, 9), (17, 9), (16, 18.5), (14.5, 20.5), (7.5, 20.5), (6, 18.5)], 2, closed=True)
    i.arc(17.2, 13.2, 3, -75, 95)
    i.line([(9, 3), (9, 5.5)], 1.8)
    i.line([(13, 3), (13, 5.5)], 1.8)


def ticket(i):
    i.line([(3, 7), (21, 7), (21, 10)] + i.arc_pts(21, 12, 2, 270, 90, 8) + [(21, 14), (21, 17), (3, 17), (3, 14)]
           + i.arc_pts(3, 12, 2, 90, -90, 8) + [(3, 10)], 2, closed=True)
    for y in (9.6, 12, 14.4):
        i.dot(9.5, y, 0.85)
    i.line([(13.5, 10), (16.5, 14)], 1.8)


def tape(i):
    i.rrect(2.5, 5.5, 21.5, 18.5, 2.5)
    i.ring(8, 12, 2.4, 1.8)
    i.ring(16, 12, 2.4, 1.8)
    i.line([(8, 14.4), (16, 14.4)], 1.6)


def globe(i):
    i.ring(12, 12, 9)
    i.line([(3, 12), (21, 12)], 1.8)
    i.line(i.arc_pts(4.5, 12, 11.7, -50, 50, 20), 1.8)
    i.line(i.arc_pts(19.5, 12, 11.7, 130, 230, 20), 1.8)


def month(i):
    i.rrect(3.5, 4.5, 20.5, 20.5, 2.6)
    i.rrect_fill(3.5, 4.5, 20.5, 9.5, 2.2)
    for y in (13, 17):
        for x in (8, 12, 16):
            i.dot(x, y, 1.25)


def orbit(i):
    # The Orbit mark (tools/gen_logo.py), sized for the 24 grid.
    k, cx, cy, u = 11 / 30.75, 11.6, 12.4, 0.7071
    i.dot(cx, cy, 27 * k)
    i.cut_dot(cx + 3 * k * u, cy - 3 * k * u, 19.5 * k)
    i.cut_dot(cx + 24.75 * k * u, cy - 24.75 * k * u, 9.6 * k)
    i.dot(cx + 24.75 * k * u, cy - 24.75 * k * u, 6 * k)


ICONS = [
    ("clock", clock), ("music", music), ("keys", keys), ("bell", bell), ("pin", pin),
    ("camera", camera), ("image", image), ("paw", paw), ("spark", spark), ("gear", gear),
    ("timer", timer), ("play", play), ("pause", pause), ("next", nxt), ("prev", prev),
    ("vol_up", vol_up), ("vol_down", vol_down), ("bluetooth", bluetooth), ("wifi", wifi),
    ("sun", sun), ("moon", moon), ("torch", torch), ("bolt", bolt), ("back", back),
    ("check", check), ("close", close), ("plus", plus), ("minus", minus), ("mic", mic),
    ("phone", phone), ("shutter", shutter), ("video", video), ("apple", apple), ("ball", ball),
    ("drop", drop), ("heart", heart), ("steps", steps), ("power", power), ("target", target),
    ("link", link), ("trash", trash), ("chat", chat), ("face", face), ("compass", compass),
    ("info", info), ("aperture", aperture), ("stop", stop), ("restart", restart), ("zzz", zzz),
    ("rec", rec), ("half", half), ("grid", grid), ("alarm", alarm), ("hourglass", hourglass),
    ("cloud", cloud), ("wind", wind), ("calc", calc), ("piano", piano), ("metronome", metronome),
    ("tuner", tuner), ("level", level), ("dice", dice), ("sunrise", sunrise), ("dumbbell", dumbbell),
    ("monitor", monitor), ("tally", tally), ("shuffle", shuffle), ("repeat", repeat),
    ("calendar", calendar), ("wave", wave), ("gem", gem), ("tv", tv), ("cup", cup), ("ticket", ticket),
    ("tape", tape), ("globe", globe), ("month", month), ("orbit", orbit),
]

# Two sizes cover the whole UI: 48 for launcher and hero buttons, 28 inline.
SIZES = [48, 28]


def render(fn, size):
    ic = Ic(size)
    fn(ic)
    return ic.done()


def emit():
    OUT.mkdir(parents=True, exist_ok=True)
    c = ['/* Generated by tools/gen_icons.py. Do not edit. */', '#include "icons.h"', ""]
    h = ['/* Generated by tools/gen_icons.py. Do not edit. */', "#pragma once", '#include "lvgl.h"', ""]
    for name, fn in ICONS:
        for size in SIZES:
            data = render(fn, size).tobytes()
            sym = f"ic_{name}_{size}"
            c.append(f"static const uint8_t {sym}_map[] = {{")
            for o in range(0, len(data), 24):
                c.append("    " + ",".join(str(b) for b in data[o:o + 24]) + ",")
            c.append("};")
            c.append(f"const lv_image_dsc_t {sym} = {{")
            c.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,")
            c.append(f"                .w = {size}, .h = {size}, .stride = {size} }},")
            c.append(f"    .data_size = sizeof({sym}_map), .data = {sym}_map,")
            c.append("};")
            c.append("")
            h.append(f"extern const lv_image_dsc_t {sym};")
    (OUT / "icons.c").write_text("\n".join(c) + "\n")
    (OUT / "icons.h").write_text("\n".join(h) + "\n")
    print(f"icons: {len(ICONS)} x {SIZES} -> {OUT}")


def sheet(path):
    cols, cell = 8, 96
    rows = math.ceil(len(ICONS) / cols)
    im = Image.new("RGB", (cols * cell, rows * cell), (0, 0, 0))
    for n, (name, fn) in enumerate(ICONS):
        x, y = (n % cols) * cell, (n // cols) * cell
        big = render(fn, 48)
        small = render(fn, 28)
        im.paste((240, 240, 245), (x + 8, y + 24), big)
        im.paste((255, 122, 69), (x + 62, y + 34), small)
        ImageDraw.Draw(im).text((x + 8, y + 4), name, fill=(120, 124, 135))
    im.save(path)
    print("sheet:", path)


if __name__ == "__main__":
    emit()
    if "--sheet" in sys.argv:
        sheet(sys.argv[sys.argv.index("--sheet") + 1])
