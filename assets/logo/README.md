# The Orbit logo and look

Everything in this folder except this note is written by
`python tools/gen_logo.py --brand`. Change the numbers at the top of that
script and run it again rather than editing the files.

## The mark

A ring seen slightly from the side, thick where it is near and thin where it
is far, with a small body sitting in a gap on its far side. It is an orbit,
and it is also the round watch with its crown.

| File | Use |
| --- | --- |
| `orbit-mark.svg` | The mark on dark backgrounds |
| `orbit-mark-on-light.svg` | The mark on light backgrounds (deeper colours) |
| `orbit-mark-mono.svg` | One colour, for stamping or tinting |
| `orbit-icon.svg` | The mark on its dark rounded square |
| `orbit-lockup.svg`, `orbit-lockup-on-light.svg` | Mark with the name beside it |
| `png/orbit-icon-512.png` | App store icon |
| `png/orbit-mark-1024.png`, `png/orbit-mark-on-light-1024.png` | Transparent pictures of the mark |
| `png/orbit-lockup.png` | The lockup on dark |
| `android/ic_launcher.xml` | Adaptive icon; goes in `res/mipmap-anydpi-v26/` |
| `android/ic_launcher_foreground.xml`, `_background.xml`, `_monochrome.xml` | Its three layers; go in `res/drawable/` |
| `android/ic_stat_orbit.xml` | White 24 dp mark for the status bar and notifications |

Keep clear space of at least the body's width around the mark. Do not
recolour the ring and the body separately, rotate it, or put the coloured
version on a mid-tone background: use the one-colour version there.

On the watch the same mark is the boot screen, where the body circles the ring
once and settles into its gap (`main/assets/gen/logo.c`, written by the same
script without `--brand`).

## Colours

| Name | Value | Where |
| --- | --- | --- |
| Ring, near side | `#7AB0FF` | Logo; on light `#2F6FE6` |
| Ring, far side | `#C9B3FF` | Logo; on light `#7E5BE8` |
| Body | `#FFB68C` | Logo; on light `#E87A33` |
| Ink | `#0F1115` | Icon background, app's dark background |
| Black | `#000000` | The watch's own background (its screen is AMOLED) |
| Surface | `#212121` | Cards and chips |
| Surface, raised | `#313131` | Buttons on a surface |
| Line | `#424242` | Dividers, tracks |
| Text | `#F1F3F4` | |
| Text, dim | `#AEB3BB` | Secondary text |
| Text, faint | `#62676F` | Hints, disabled |
| Good / warn / bad | `#4ADE80` / `#FFC53D` / `#FF5D5D` | Status only |

The watch has six accent colours and the wearer picks one (Settings >
Display, or `cfg` `accent` over the link). An app that wants to match the
watch should take that one as its main colour:

| 0 | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- |
| sky `#A8C7FA` | mint `#A8DAB5` | rose `#FFB1C8` | violet `#D0BCFF` | peach `#FFB68C` | sun `#FDD663` |

Each watch app also has one colour of its own, used for its icon and its main
number or button and nowhere else: Agenda `#8AB4F8`, Calendar `#F28B82`, Music
`#FF6B9A`, Phone `#4ADE9B`, Vision `#5CCFE6`, ZZZ `#FFD60A`, and so on (see
the `*_COLOR` defines in `main/apps/`).

## Type

Montserrat throughout (`assets/fonts/`, SIL Open Font License). Regular for
text, weight 600 for the name and for titles. Small labels above a group are
capitals at 16 px with 2 px of letter spacing in the dim text colour. Big
numbers are Montserrat too: weight 640 over weight 260 on the digital face,
weight 900 on the TV wall.

## Shapes

- Buttons are fully rounded pills, 56 px tall on the 466 px screen; icon
  buttons are circles.
- Cards have a 30 px corner radius on the surface colour, with 20 px of
  padding. On a phone that is about 28 dp and 16 dp.
- Icons sit on a 24 unit grid with a 2 unit round stroke.
- Colour is spent sparingly: black or near-black ground, one accent, and at
  most one extra hue per screen.
