"""Xbox and PlayStation button icons for the game's prompts (runtime/mlb/prompts.c), drawn here and compressed to BC7.

    .venv/Scripts/python -I tools/make_prompts.py [--out work/gen/prompt_icons.h] [--check <symbols.png>]

build.py runs it when the header is older than this file. Nothing of the game is in the repository or in the header:
every icon is drawn here from shapes and Windows' own Bahnschrift font.

How MLB draws prompts. Text carries tokens such as ^b0^ (a button) and ^b16^ (a stick); the engine draws each one as a
cell of one 1024x1024 BC7 texture, the "symbols" entry of NSW/UI/ART/FONTS/FONTS.TEX (its other cells are emoji,
badges and arrows). The Switch build ships only Switch buttons there: the PS4/ and PS5/ folders of the RomFS hold
animation, stadium and subtitle files, no button art. A button cell is a dark disc (sRGB 44) in a white ring, or a
shoulder tab, with a white letter. The runtime finds that texture by its bytes and copies the blocks made here over
the cells, so every prompt of the game shows the controller in use.

What is replaced (cell geometry measured on a RenderDoc capture of the texture, 1.0.15):
- A, B, X, Y discs: the face buttons, by label (Xbox A, PlayStation cross for Switch A) or by position
  (MLB_PAD_MAP=position: Switch A is the east button, Xbox B or circle), so each prompt names the button to press.
- L, R, ZL, ZR tabs: LB RB LT RT, or L1 R1 L2 R2.
- Plus and minus discs: Menu and View, or Options and Create.
- The Joy-Con d-pad, four round buttons with the pressed ones lit (six cells: up and down, left and right, each
  direction): a cross-shaped d-pad, the pressed arms lit in the game's own teal.
Only the inside of a disc or tab is replaced (whole 4x4 blocks inside the ring), so the game's ring stays as it is;
the d-pad cells are drawn whole, transparent around the cross as the originals are. Sticks, arrows and SL / SR keep
their Switch icons (L and R in a disc read as the sticks on any pad).

The play HUD (the pitch list, the fielding diamond) draws its buttons from other textures, one 128x128 BC7 texture per
button (TARGETS): the same icons are drawn over their discs and tabs, and each texture is found by its own hash.

--check-hud <GAME.TEX> (diagnosis, local only, needs the lz4 module): reads those textures from the game's
NSW/UI/GAME/GAME.TEX, checks their hashes and that the replaced blocks hold only the old letter and the dark fill, and
writes every set over them to work/scratch/hud-icons-check.png.

--check <png> (diagnosis, local only): the texture exported from a capture. Checks that every replaced block lies
inside its cell (no ring or neighbour texel under it) and covers the whole Switch letter, and writes the texture with
each set's icons over it next to the png (<name>-<style>-<map>.png).
"""
import argparse
import io
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = Path('C:/Windows/Fonts/bahnschrift.ttf')
# The game's letters: a DIN-like face, stem about 0.14 of the cap height; Bahnschrift at weight 430 matches.
FONT_WEIGHT = 430
SS = 8   # supersampling: everything is drawn 8 times larger, then each 8x8 square averaged

FILL = (44, 44, 44, 255)          # inside a disc or tab (sRGB)
WHITE = (245, 245, 245, 255)      # the game's rings and letters
TEAL = (14, 238, 207, 255)        # a lit d-pad button: ring and arrow
TEAL_FILL = (36, 101, 92, 255)    # its inside

# Xbox: letters in the colours of the controller's buttons, a little brighter for a dark disc.
XBOX_COLOURS = {'A': (108, 194, 74, 255), 'B': (232, 72, 72, 255), 'X': (64, 150, 232, 255), 'Y': (246, 196, 46, 255)}
# PlayStation: the symbols in their traditional colours, brightened the same way.
PS_COLOURS = {'cross': (124, 178, 240, 255), 'circle': (240, 100, 104, 255), 'square': (226, 140, 220, 255),
              'triangle': (64, 220, 170, 255)}

# ---- the cells, in texels (texel x covers x..x+1). Discs: centre of the inner disc (inside the ring), radius DISC_R.
# Tabs: the inner rectangle and the corner rounded like a shoulder button (radius TAB_BIG; the others TAB_SMALL).
DISCS = {'A': (736.0, 259.5), 'B': (638.0, 259.5), 'X': (180.0, 259.5), 'Y': (540.0, 259.5),
         'plus': (954.0, 154.5), 'minus': (834.0, 259.0)}
DISC_R = 43.0
TABS = {'ZL': ((521, 115, 633, 194), 'tl'), 'L': ((652, 115, 764, 194), 'tl'), 'ZR': ((783, 115, 895, 194), 'tr'),
        'R': ((9, 220, 121, 299), 'tr')}
TAB_BIG, TAB_SMALL = 45.0, 5.0
# The d-pad cells: the block-aligned square each is drawn into (the 100-texel cell 2 texels in; neighbours start
# outside the square) and the directions it lights.
DPADS = {'dpad_ud': (104, 0, ('up', 'down')), 'dpad_lr': (208, 0, ('left', 'right')),
         'dpad_left': (624, 0, ('left',)), 'dpad_right': (728, 0, ('right',)), 'dpad_up': (832, 0, ('up',)),
         'dpad_down': (0, 104, ('down',))}
DPAD_SIZE = 104
FACE_CAP, TAB_CAP = 43, 46   # cap heights of the game's letters in a disc and in a tab
STROKE = 6.0                 # the letters' stem width, for the drawn symbols


class Target:
    """A texture of the game whose button icons are replaced. prompts.c finds it by its width (= height) and the FNV-1a
    of the first 16 KiB of its storage as the game wrote it. Its cells: discs (centre of the inner disc, radius
    disc_r), tabs (inner rectangle and big corner, radius tab_big), d-pads; face_cap and tab_cap are the cap heights
    of its letters, tab_w the widest a tab's text gets (default 0.7 of the tab)."""
    def __init__(self, name, hsh, size, discs, disc_r, tabs, tab_big, face_cap, tab_cap, dpads=None, tab_w=None):
        self.name, self.hash, self.size = name, hsh, size
        self.discs, self.disc_r, self.tabs, self.tab_big = discs, disc_r, tabs, tab_big
        self.face_cap, self.tab_cap, self.dpads, self.tab_w = face_cap, tab_cap, dpads or {}, tab_w

    def cells(self):
        return [*self.discs, *self.tabs, *self.dpads]


# The prompts' atlas: the "symbols" entry of NSW/UI/ART/FONTS/FONTS.TEX (1.0.15).
ATLAS = Target('symbols', 0x654fe56d4ffb4ee7, 1024, DISCS, DISC_R, TABS, TAB_BIG, FACE_CAP, TAB_CAP, DPADS, tab_w=78.0)


# The play HUD's buttons (the pitch list, the fielding diamond, ...): one 128x128 BC7 texture each, entries of
# NSW/UI/GAME/GAME.TEX (and of THEME1, THEME2, TRAIN, FRONTEND and HRDERBY, the same bytes) named after the PlayStation
# buttons and drawn with the Switch ones in their places: button_x (cross) is Switch B, button_circle A,
# button_triangle X, button_square Y, button_l1 / r1 / l2 / r2 L / R / ZL / ZR (found with a RenderDoc capture of the
# pitch list; geometry measured on the textures). A disc: radius 49 inside the ring around the texture's centre,
# letters 48 texels tall. A tab: the inner rectangle, its big corner of radius about 33 (38 here: the blocks keep clear of the ring).
def hud_face(name, hsh, cell):
    return Target(name, hsh, 128, {cell: (64.0, 64.0)}, 49.0, {}, 0.0, 48, 0)


def hud_tab(name, hsh, cell, rect, big):
    return Target(name, hsh, 128, {}, 0.0, {cell: (rect, big)}, 38.0, 0, 40)


TARGETS = [
    ATLAS,
    hud_face('button_circle', 0x9e7795814ec32b4c, 'A'), hud_face('button_x', 0xfc55934c56c5f503, 'B'),
    hud_face('button_triangle', 0x0e124bb0e4be60dd, 'X'), hud_face('button_square', 0x92d1e6d7e03ab5f5, 'Y'),
    hud_tab('button_l1', 0xc4556fa43dd8f284, 'L', (17, 31, 111, 97), 'tl'),
    hud_tab('button_r1', 0xeeef6d131239d56f, 'R', (17, 31, 112, 98), 'tr'),
    hud_tab('button_l2', 0xf5203794488141e5, 'ZL', (15, 30, 114, 99), 'tl'),
    hud_tab('button_r2', 0x7c78b49245b74d37, 'ZR', (15, 29, 114, 99), 'tr'),
]

# What each style puts in each cell, faces by label; FACE_POSITION relabels the faces for MLB_PAD_MAP=position.
STYLES = {
    'xbox': {'A': 'A', 'B': 'B', 'X': 'X', 'Y': 'Y', 'L': 'LB', 'R': 'RB', 'ZL': 'LT', 'ZR': 'RT',
             'plus': 'menu', 'minus': 'view'},
    'playstation': {'A': 'cross', 'B': 'circle', 'X': 'square', 'Y': 'triangle', 'L': 'L1', 'R': 'R1',
                    'ZL': 'L2', 'ZR': 'R2', 'plus': 'options', 'minus': 'create'},
}
# By position, the Switch button at a place is pressed with the pad's button at that place: Switch A (east) with
# Xbox B / circle, B (south) with A / cross, X (north) with Y / triangle, Y (west) with X / square.
FACE_POSITION = {'A': 'B', 'B': 'A', 'X': 'Y', 'Y': 'X'}
# The sets the runtime chooses from, in this order (prompts.c).
SETS = (('xbox', 'label'), ('xbox', 'position'), ('playstation', 'label'), ('playstation', 'position'))


# ---------------------------------------------------------------- drawing
class Canvas:
    """An RGBA box of the texture at (x0, y0), w x h texels, drawn SS times larger; coordinates are texture texels."""
    def __init__(self, x0, y0, w, h, background):
        self.x0, self.y0, self.w, self.h = x0, y0, w, h
        self.im = Image.new('RGBA', (w * SS, h * SS), background)
        self.d = ImageDraw.Draw(self.im)

    def p(self, x, y):
        return ((x - self.x0) * SS, (y - self.y0) * SS)

    def box(self, x0, y0, x1, y1):
        return [*self.p(x0, y0), *self.p(x1, y1)]

    def rect(self, b, colour):
        self.d.rectangle(self.box(*b), fill=colour)

    def rrect(self, b, r, fill=None, outline=None, width=0.0):
        self.d.rounded_rectangle(self.box(*b), radius=r * SS, fill=fill, outline=outline, width=round(width * SS))

    def ellipse(self, b, fill=None, outline=None, width=0.0):
        self.d.ellipse(self.box(*b), fill=fill, outline=outline, width=round(width * SS))

    def polygon(self, pts, colour):
        self.d.polygon([self.p(x, y) for x, y in pts], fill=colour)

    def line(self, pts, w, colour):
        """A polyline w texels wide with round ends and joints."""
        self.d.line([self.p(x, y) for x, y in pts], fill=colour, width=round(w * SS), joint='curve')
        for x, y in pts:
            self.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=colour)

    def text(self, cx, cy, s, cap, colour, max_w=None):
        """s centred on (cx, cy) by its ink, capitals cap texels tall; narrowed to max_w texels when wider."""
        f = font(cap)
        x0, y0, x1, y1 = self.d.textbbox((0, 0), s, font=f)
        if max_w and x1 - x0 > max_w * SS:
            f = font(cap * max_w * SS / (x1 - x0))
            x0, y0, x1, y1 = self.d.textbbox((0, 0), s, font=f)
        px, py = self.p(cx, cy)
        self.d.text((px - (x0 + x1) / 2, py - (y0 + y1) / 2), s, font=f, fill=colour)

    def texels(self):
        """The box averaged down to texels: (h, w, 4) uint8. Colour is averaged by coverage, so an edge over
        transparency keeps the colour of what covers it."""
        a = np.asarray(self.im, dtype=np.float64).reshape(self.h, SS, self.w, SS, 4)
        al = a[..., 3:4]
        rgb = (a[..., :3] * al).sum(axis=(1, 3)) / np.maximum(al.sum(axis=(1, 3)), 1e-9)
        out = np.concatenate([rgb, al.mean(axis=(1, 3))], axis=-1)
        return np.clip(np.round(out), 0, 255).astype(np.uint8)


_fonts = {}


def font(cap):
    """Bahnschrift at FONT_WEIGHT, sized so that a capital is cap texels tall (drawn SS times larger)."""
    if cap not in _fonts:
        f = ImageFont.truetype(str(FONT), 100)
        f.set_variation_by_axes([FONT_WEIGHT, 100])
        top, bottom = f.getbbox('H')[1], f.getbbox('H')[3]
        f = ImageFont.truetype(str(FONT), round(100 * cap * SS / (bottom - top)))
        f.set_variation_by_axes([FONT_WEIGHT, 100])
        _fonts[cap] = f
    return _fonts[cap]


def draw_glyph(c, g, cx, cy, face_cap=FACE_CAP, tab_cap=TAB_CAP, tab_w=78.0):
    """Icon g centred on (cx, cy): letters face_cap texels tall in a disc (the drawn symbols scaled with them), tab_cap
    in a tab, narrowed to tab_w."""
    k = face_cap / FACE_CAP
    stroke = STROKE * k
    if g in XBOX_COLOURS:
        c.text(cx, cy, g, face_cap, XBOX_COLOURS[g])
    elif g in ('LB', 'RB', 'LT', 'RT', 'L1', 'R1', 'L2', 'R2'):
        c.text(cx, cy, g, tab_cap, WHITE, max_w=tab_w)
    elif g == 'cross':
        h = 15.5 * k
        c.line([(cx - h, cy - h), (cx + h, cy + h)], stroke, PS_COLOURS[g])
        c.line([(cx - h, cy + h), (cx + h, cy - h)], stroke, PS_COLOURS[g])
    elif g == 'circle':
        r = 19.0 * k
        c.ellipse((cx - r, cy - r, cx + r, cy + r), outline=PS_COLOURS[g], width=stroke)
    elif g == 'square':
        h = 17.0 * k
        c.rrect((cx - h, cy - h, cx + h, cy + h), 2.0 * k, outline=PS_COLOURS[g], width=stroke)
    elif g == 'triangle':
        s = 40.0 * k   # side; the triangle's box is centred, a little high so it looks centred
        hgt = s * math.sqrt(3) / 2
        top, base = cy - hgt / 2 - 2.0 * k, cy + hgt / 2 - 2.0 * k
        c.line([(cx, top), (cx + s / 2, base), (cx - s / 2, base), (cx, top)], stroke, PS_COLOURS[g])
    elif g in ('menu', 'options'):   # three lines, as on both controllers' button
        for dy in (-11.0, 0.0, 11.0):
            c.line([(cx - 16.0, cy + dy), (cx + 16.0, cy + dy)], 5.5, WHITE)
    elif g == 'view':                # two overlapping windows, the back one up and to the right
        s, o = 26.0, 7.5
        c.rrect((cx - s / 2 + o, cy - s / 2 - o, cx + s / 2 + o, cy + s / 2 - o), 4.0, outline=WHITE, width=5.0)
        c.rrect((cx - s / 2 - o, cy - s / 2 + o, cx + s / 2 - o, cy + s / 2 + o), 4.0, fill=FILL, outline=WHITE,
                width=5.0)
    elif g == 'create':              # the DualSense Create button's mark: three short strokes fanning upwards
        for a in (-48.0, 0.0, 48.0):
            sx, sy = math.sin(math.radians(a)), -math.cos(math.radians(a))
            c.line([(cx + sx * 12, cy + 17 + sy * 12), (cx + sx * 34, cy + 17 + sy * 34)], 6.0, WHITE)
    else:
        raise ValueError(g)


def draw_dpad(c, x0, y0, lit):
    """A cross-shaped d-pad in the DPAD_SIZE square at (x0, y0): a white rim around dark arms, the lit arms with a
    teal rim, a dark teal inside and a teal arrow pointing out (the game's lit Joy-Con button)."""
    cx, cy = x0 + DPAD_SIZE / 2, y0 + DPAD_SIZE / 2
    reach, half, rim = 48.0, 19.0, 5.0   # arm end from the centre, half the arm's width, rim width
    dirs = {'up': (0, -1), 'down': (0, 1), 'left': (-1, 0), 'right': (1, 0)}

    def arm(name, inset, colour):
        """One arm from the far side of the centre square to its end; the other arms cover its inner corners."""
        ux, uy = dirs[name]
        r, h = reach - inset, half - inset
        if ux:
            x0, x1 = sorted((cx - ux * h, cx + ux * r))
            y0, y1 = cy - h, cy + h
        else:
            y0, y1 = sorted((cy - uy * h, cy + uy * r))
            x0, x1 = cx - h, cx + h
        c.rrect((x0, y0, x1, y1), max(6.0 - inset, 1.5), fill=colour)

    for name in dirs:
        arm(name, 0.0, TEAL if name in lit else WHITE)
    for name in dirs:
        arm(name, rim, TEAL_FILL if name in lit else FILL)
    h = half - rim
    c.rect((cx - h, cy - h, cx + h, cy + h), FILL)   # the centre stays dark: lit arms read as the pressed buttons
    for name in lit:
        ux, uy = dirs[name]
        mx, my = cx + ux * 31.0, cy + uy * 31.0
        tip, back, w = 9.5, 7.0, 10.5
        if ux:
            pts = [(mx + ux * tip, my), (mx - ux * back, my - w), (mx - ux * back, my + w)]
        else:
            pts = [(mx, my + uy * tip), (mx - w, my - uy * back), (mx + w, my - uy * back)]
        c.polygon(pts, TEAL)


# ---------------------------------------------------------------- the blocks of a cell
def inside_disc(t, px, py, name, margin):
    cx, cy = t.discs[name]
    return (px - cx) ** 2 + (py - cy) ** 2 <= (t.disc_r - margin) ** 2


def inside_tab(t, px, py, name, margin):
    (x0, y0, x1, y1), big = t.tabs[name]
    x0, y0, x1, y1 = x0 + margin, y0 + margin, x1 - margin, y1 - margin
    if not (x0 <= px <= x1 and y0 <= py <= y1):
        return False
    for k, (qx, qy) in {'tl': (x0, y0), 'tr': (x1, y0), 'bl': (x0, y1), 'br': (x1, y1)}.items():
        r = max(t.tab_big - margin, 0.0) if k == big else TAB_SMALL
        ccx = qx + r if k[1] == 'l' else qx - r
        ccy = qy + r if k[0] == 't' else qy - r
        beyond_x = px < ccx if k[1] == 'l' else px > ccx
        beyond_y = py < ccy if k[0] == 't' else py > ccy
        if beyond_x and beyond_y and (px - ccx) ** 2 + (py - ccy) ** 2 > r * r:
            return False
    return True


def cell_blocks(t, name):
    """(bx, by) of the 4x4 blocks the cell's icon replaces: every corner of the block a texel inside the disc or tab,
    or the whole square of a d-pad."""
    if name in t.dpads:
        x0, y0, _ = t.dpads[name]
        return [(bx, by) for by in range(y0 // 4, (y0 + DPAD_SIZE) // 4)
                for bx in range(x0 // 4, (x0 + DPAD_SIZE) // 4)]
    inside = inside_disc if name in t.discs else inside_tab
    n = t.size // 4
    return [(bx, by) for by in range(n) for bx in range(n)
            if all(inside(t, px, py, name, 1.0) for px in (bx * 4, bx * 4 + 4) for py in (by * 4, by * 4 + 4))]


def render_cell(t, name, glyph):
    """The cell's blocks and their texels: ([(bx, by)], [(16, 4) uint8])."""
    blocks = cell_blocks(t, name)
    bx0, by0 = min(b[0] for b in blocks), min(b[1] for b in blocks)
    bx1, by1 = max(b[0] for b in blocks) + 1, max(b[1] for b in blocks) + 1
    c = Canvas(bx0 * 4, by0 * 4, (bx1 - bx0) * 4, (by1 - by0) * 4, (0, 0, 0, 0) if name in t.dpads else FILL)
    if name in t.dpads:
        draw_dpad(c, *t.dpads[name])
    elif name in t.discs:
        cx, cy = t.discs[name]
        draw_glyph(c, glyph, cx, cy, face_cap=t.face_cap)
    else:
        (x0, y0, x1, y1), _ = t.tabs[name]
        draw_glyph(c, glyph, (x0 + x1) / 2, (y0 + y1) / 2, tab_cap=t.tab_cap, tab_w=t.tab_w or 0.7 * (x1 - x0))
    t = c.texels()
    return blocks, [t[(by - by0) * 4:(by - by0) * 4 + 4, (bx - bx0) * 4:(bx - bx0) * 4 + 4].reshape(16, 4)
                    for bx, by in blocks]


# ---------------------------------------------------------------- BC7: modes 4, 5 and 6 (one subset)
W2 = np.array([0, 21, 43, 64])
W3 = np.array([0, 9, 18, 27, 37, 46, 55, 64])
W4 = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64])


def interp(e0, e1, w):
    """BC7 interpolation between integer endpoints (N, C) at weights w (K,): (N, K, C)."""
    return ((64 - w)[None, :, None] * e0[:, None, :] + w[None, :, None] * e1[:, None, :] + 32) >> 6


class Quant:
    """Endpoint precision: bits per channel, expanded to 8 by bit replication; pbit: mode 6's shared lowest bit."""
    def __init__(self, bits, pbit=False):
        self.bits, self.pbit = bits, pbit

    def codes(self, e):
        if not self.pbit:
            m = (1 << self.bits) - 1
            return np.clip(np.round(e * m / 255.0), 0, m).astype(np.int64)
        best = None
        for p in (0, 1):   # the p-bit that brings the endpoint closest
            c = np.clip(np.round((e - p) / 2.0), 0, 127).astype(np.int64)
            err = ((c * 2 + p - e) ** 2).sum(1)
            c = np.concatenate([c, np.full((len(e), 1), p)], 1)
            if best is None:
                best = (c, err)
            else:
                b = err < best[1]
                best = (np.where(b[:, None], c, best[0]), np.where(b, err, best[1]))
        return best[0]

    def expand(self, c):
        if self.pbit:
            return c[:, :-1] * 2 + c[:, -1:]
        b = self.bits
        return (c << (8 - b)) | (c >> (2 * b - 8))


def principal(x, wt):
    """Weighted mean and principal axis of x (N, 16, C) with weights wt (N, 16)."""
    s = wt.sum(1)[:, None] + 1e-9
    mean = (x * wt[..., None]).sum(1) / s
    d = (x - mean[:, None]) * np.sqrt(wt)[..., None]
    cov = np.einsum('nkc,nkd->ncd', d, d)
    v = x.max(1) - x.min(1) + 1e-3
    for _ in range(12):
        v = np.einsum('ncd,nd->nc', cov, v)
        v /= np.linalg.norm(v, axis=1, keepdims=True) + 1e-12
    return mean, v


def fit(x, wt, weights, q):
    """One index set: x (N, 16, C) in 0..255, wt (N, 16) error weights, weights the interpolation table, q a Quant.
    Endpoints at the ends of the principal axis, then twice least squares for the indices found; keeps the best.
    Returns codes0, codes1, indices (N, 16), error (N,)."""
    mean, axis = principal(x, wt)
    t = np.einsum('nkc,nc->nk', x - mean[:, None], axis)
    e0 = mean + np.where(wt > 0, t, np.inf).min(1)[:, None] * axis
    e1 = mean + np.where(wt > 0, t, -np.inf).max(1)[:, None] * axis
    e0, e1 = np.nan_to_num(e0, posinf=0, neginf=0), np.nan_to_num(e1, posinf=0, neginf=0)
    best = None
    sw = np.sqrt(wt)
    for _ in range(3):
        c0, c1 = q.codes(np.clip(e0, 0, 255)), q.codes(np.clip(e1, 0, 255))
        pal = interp(q.expand(c0), q.expand(c1), weights)                  # (N, K, C)
        dist = ((x[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)      # (N, 16, K)
        idx = dist.argmin(-1)
        err = (np.take_along_axis(dist, idx[..., None], -1)[..., 0] * wt).sum(1)
        if best is None:
            best = [c0, c1, idx, err]
        else:
            b = err < best[3]
            best = [np.where(b[:, None], c0, best[0]), np.where(b[:, None], c1, best[1]),
                    np.where(b[:, None], idx, best[2]), np.where(b, err, best[3])]
        f = weights[idx] / 64.0 * sw
        g = sw - f
        xs = x * sw[..., None]
        a11, a12, a22 = (g * g).sum(1), (g * f).sum(1), (f * f).sum(1)
        b1, b2 = np.einsum('nk,nkc->nc', g, xs), np.einsum('nk,nkc->nc', f, xs)
        det = a11 * a22 - a12 * a12
        ok = (np.abs(det) > 1e-6)[:, None]
        det = np.where(ok[:, 0], det, 1.0)[:, None]
        e0 = np.where(ok, (a22[:, None] * b1 - a12[:, None] * b2) / det, e0)
        e1 = np.where(ok, (a11[:, None] * b2 - a12[:, None] * b1) / det, e1)
    return best


def anchor_fix(c0, c1, idx, nbits):
    """Texel 0's index must have its top bit clear: where it has not, swap the endpoints and mirror the indices."""
    top = 1 << (nbits - 1)
    s = idx[:, 0] >= top
    return (np.where(s[:, None], c1, c0), np.where(s[:, None], c0, c1), np.where(s[:, None], 2 * top - 1 - idx, idx))


class Bits:
    """BC7 blocks written field by field, least significant bit first."""
    def __init__(self, n):
        self.v, self.pos = [0] * n, 0

    def put(self, vals, nbits):
        vals = np.broadcast_to(np.asarray(vals, dtype=np.int64), (len(self.v),))
        for i, x in enumerate(vals):
            self.v[i] |= int(x) << self.pos
        self.pos += nbits

    def indices(self, idx, nbits):
        self.put(idx[:, 0], nbits - 1)
        for k in range(1, 16):
            self.put(idx[:, k], nbits)

    def blocks(self):
        assert self.pos == 128, self.pos
        return [x.to_bytes(16, 'little') for x in self.v]


def encode(px):
    """BC7 blocks of px (N, 16, 4) uint8: per block the best of mode 6 (RGBA together, 4-bit indices) and modes 5 and 4
    (colour and alpha apart, which keeps antialiased edges over transparency clean)."""
    x = px.astype(np.float64)
    a = x[..., 3]
    cov = a / 255.0
    # a transparent texel's colour does not count: it takes the block's mean colour, so the colours stay on a line
    mean_rgb = (x[..., :3] * cov[..., None]).sum(1) / np.maximum(cov.sum(1), 1e-9)[:, None]
    rgb = np.where(cov[..., None] > 0, x[..., :3], mean_rgb[:, None, :])
    cw = cov + 1e-3
    ones = np.ones_like(a)

    def colour_err(q, c0, c1, idx, w):
        got = np.take_along_axis(interp(q.expand(c0), q.expand(c1), w), idx[..., None], 1)
        return (((got - rgb) ** 2).sum(-1) * cw).sum(1)

    def alpha_err(q, c0, c1, idx, w):
        got = np.take_along_axis(interp(q.expand(c0), q.expand(c1), w)[..., 0], idx, 1)
        return ((got - a) ** 2).sum(1)

    q6 = Quant(7, pbit=True)
    m6 = fit(np.concatenate([rgb, a[..., None]], -1), ones, W4, q6)
    got = np.take_along_axis(interp(q6.expand(m6[0]), q6.expand(m6[1]), W4), m6[2][..., None], 1)
    e6 = (((got[..., :3] - rgb) ** 2).sum(-1) * cw + (got[..., 3] - a) ** 2).sum(1)

    q7, q8 = Quant(7), Quant(8)
    m5c, m5a = fit(rgb, cw, W2, q7), fit(a[..., None], ones, W2, q8)
    e5 = colour_err(q7, *m5c[:3], W2) + alpha_err(q8, *m5a[:3], W2)

    q5b, q6b = Quant(5), Quant(6)
    m4c, m4a = fit(rgb, cw, W3, q5b), fit(a[..., None], ones, W2, q6b)   # index mode 1: 3-bit colour, 2-bit alpha
    e4 = colour_err(q5b, *m4c[:3], W3) + alpha_err(q6b, *m4a[:3], W2)

    choice = np.argmin(np.stack([e6, e5, e4]), 0)
    out = [None] * len(px)
    for mode, sel in ((6, np.nonzero(choice == 0)[0]), (5, np.nonzero(choice == 1)[0]),
                      (4, np.nonzero(choice == 2)[0])):
        if not len(sel):
            continue
        b = Bits(len(sel))
        if mode == 6:
            c0, c1, idx = anchor_fix(m6[0][sel], m6[1][sel], m6[2][sel], 4)
            b.put(1 << 6, 7)
            for ch in range(4):
                b.put(c0[:, ch], 7)
                b.put(c1[:, ch], 7)
            b.put(c0[:, 4], 1)
            b.put(c1[:, 4], 1)
            b.indices(idx, 4)
        else:
            mc, ma = (m5c, m5a) if mode == 5 else (m4c, m4a)
            cbits, abits, cidx_bits = (7, 8, 2) if mode == 5 else (5, 6, 3)
            c0, c1, ci = anchor_fix(mc[0][sel], mc[1][sel], mc[2][sel], cidx_bits)
            a0, a1, ai = anchor_fix(ma[0][sel], ma[1][sel], ma[2][sel], 2)
            if mode == 5:
                b.put(1 << 5, 6)
                b.put(0, 2)        # rotation: none
            else:
                b.put(1 << 4, 5)
                b.put(0, 2)        # rotation: none
                b.put(1, 1)        # index mode 1: the 3-bit indices are the colour's
            for ch in range(3):
                b.put(c0[:, ch], cbits)
                b.put(c1[:, ch], cbits)
            b.put(a0[:, 0], abits)
            b.put(a1[:, 0], abits)
            if mode == 5:
                b.indices(ci, 2)
                b.indices(ai, 2)
            else:
                b.indices(ai, 2)   # the 2-bit set comes first; in index mode 1 that is alpha's
                b.indices(ci, 3)
        for i, blk in zip(sel, b.blocks()):
            out[i] = blk
    return out


def decode(blocks, bw, bh):
    """RGBA texels (bh*4, bw*4, 4) of bw x bh blocks, decoded by Pillow (an independent check of encode)."""
    w, h = bw * 4, bh * 4
    hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, h, w, len(blocks) * 16, 0, 1)
    hdr += b'\0' * 44 + struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0) + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', 98, 3, 0, 1, 0)   # DXGI_FORMAT_BC7_UNORM, 2D, one image
    return np.asarray(Image.open(io.BytesIO(hdr + b''.join(blocks))).convert('RGBA'))


# ---------------------------------------------------------------- the sets and the header
def build():
    """({(target, cell, icon): (blocks, texels)}, [[(target, cell, icon)] per set])."""
    sets, patches = [], {}
    for style, mapping in SETS:
        s = []
        for t in TARGETS:
            for cell in t.cells():
                src = FACE_POSITION.get(cell, cell) if mapping == 'position' else cell
                key = (t.name, cell, 'dpad' if cell in t.dpads else STYLES[style][src])
                if key not in patches:
                    patches[key] = render_cell(t, cell, key[2])
                s.append(key)
        sets.append(s)
    return patches, sets


def write_header(path, patches, sets, encoded):
    keys = list(patches)
    names = [t.name for t in TARGETS]
    blob, runs, rows = bytearray(), [], []
    for k in keys:
        blocks = patches[k][0]
        at = {b: i for i, b in enumerate(blocks)}
        r0 = len(runs)
        for bx, by in sorted(blocks, key=lambda b: (b[1], b[0])):
            if len(runs) > r0 and runs[-1][1] == by and runs[-1][0] + runs[-1][2] == bx:
                runs[-1][2] += 1
            else:
                runs.append([bx, by, 1, len(blob) // 16])
            blob += encoded[k][at[(bx, by)]]
        rows.append((r0, len(runs) - r0, names.index(k[0])))
    n = len(sets[0])
    out = ['// Generated by tools/make_prompts.py: the Xbox and PlayStation button icons runtime/mlb/prompts.c copies over',
           "// the game's Switch ones. Drawn from shapes and Windows' Bahnschrift font; nothing of the game. Do not edit.",
           '#pragma once', '#include <stdint.h>', '',
           f'enum {{ PROMPT_BLOCKS = {len(blob) // 16}, PROMPT_RUNS = {len(runs)}, PROMPT_PATCHES = {len(keys)}, '
           f'PROMPT_SETS = {len(sets)}, PROMPT_SET_PATCHES = {n}, PROMPT_TARGETS = {len(TARGETS)} }};', '',
           "// The game's textures the icons go over: FNV-1a of the first 16 KiB of the storage, and the width (= height).",
           'static const uint64_t prompt_target_hash[PROMPT_TARGETS] = {']
    out += ['    0x%016xull,   // %s' % (t.hash, t.name) for t in TARGETS]
    out += ['};', 'static const uint16_t prompt_target_size[PROMPT_TARGETS] = {' +
            ', '.join(str(t.size) for t in TARGETS) + '};', '',
            '// The BC7 blocks of every patch, run after run (16 bytes each).',
            'static const uint8_t prompt_blocks[PROMPT_BLOCKS * 16] = {']
    out += ['    ' + ','.join(map(str, blob[i:i + 48])) + ',' for i in range(0, len(blob), 48)]
    out += ['};', '// Runs of blocks along a row: x and y of the first (in blocks), count, its index in prompt_blocks.',
            'static const uint16_t prompt_runs[PROMPT_RUNS][4] = {']
    out += ['    {%d, %d, %d, %d},' % tuple(r) for r in runs]
    out += ['};', '// One icon in one cell: first run, number of runs, target.',
            'static const uint16_t prompt_patches[PROMPT_PATCHES][3] = {']
    out += ['    {%d, %d, %d},   // %s %s: %s' % (r[0], r[1], r[2], *k) for k, r in zip(keys, rows)]
    out += ['};', '// The patches of each set: ' + ', '.join(f'{st} by {mp}' for st, mp in SETS) + '.',
            'static const uint16_t prompt_sets[PROMPT_SETS][PROMPT_SET_PATCHES] = {']
    out += ['    {' + ', '.join(str(keys.index(k)) for k in s) + '},' for s in sets]
    out += ['};']
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(out) + '\n')   # always: build.py compares its time with this script's


def check(png, patches, sets, encoded):
    """--check: the replaced blocks against the real texture, and a preview of each set."""
    tex = np.asarray(Image.open(png).convert('RGBA')).copy()
    lum = tex[..., :3].astype(int).mean(-1)
    problems = 0
    for (tname, cell, g), (blocks, _) in patches.items():
        if tname != ATLAS.name or cell in DPADS:
            continue
        cover = np.zeros(lum.shape, bool)
        for bx, by in blocks:
            if (tex[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4, 3] < 255).any():
                print(f'{cell}: block {bx},{by} covers transparent texels')
                problems += 1
            cover[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4] = True
        # the Switch letter: bright texels 4 texels or more inside the ring, all under replaced blocks
        ys, xs = np.nonzero(lum > 80)
        inside = inside_disc if cell in DISCS else inside_tab
        inner = np.array([inside(ATLAS, x + 0.5, y + 0.5, cell, 4.0) for x, y in zip(xs, ys)], bool)
        miss = inner & ~cover[ys, xs]
        if miss.any():
            print(f'{cell}: {miss.sum()} letter texels outside the replaced blocks')
            problems += 1
    for cell, (x0, y0, _) in DPADS.items():
        # nothing but the cell inside a d-pad square: the cells beside it would show in its outer columns (the rows
        # above and below are 4 texels or more away)
        sq = tex[y0:y0 + DPAD_SIZE, x0:x0 + DPAD_SIZE, 3] > 0
        if sq[:, :2].any() or sq[:, -2:].any():
            print(f'{cell}: opaque texels in the outer columns of its square (a neighbour)')
            problems += 1
    print('check:', 'ok' if not problems else f'{problems} problems')
    for (style, mapping), s in zip(SETS, sets):
        out = tex.copy()
        for k in s:
            if k[0] != ATLAS.name:
                continue
            blocks = patches[k][0]
            dec = decode(encoded[k], len(blocks), 1)
            for i, (bx, by) in enumerate(blocks):
                out[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4] = dec[:, i * 4:i * 4 + 4]
        name = png.with_name(f'{png.stem}-{style}-{mapping}.png')
        Image.fromarray(out).save(name)
        print('wrote', name)


def tx12_entry(pack, name):
    """The storage of one texture of a TX12NSW pack (the game's texture files) as the game loads it: entries of 0x140
    bytes from 0x30 (name, ..., +0x50 size, width, height, +0xc0 size, stored size, +0x124 offset), LZ4 blocks when
    the stored size is smaller. Local diagnosis only (needs the lz4 module)."""
    import lz4.block
    d = pack.read_bytes()
    for i in range(8192):
        o = 0x30 + i * 0x140
        nm = d[o:o + 0x40].split(b'\0')[0]
        if not nm:
            break
        if nm.decode('latin1') != name:
            continue
        size, csize = struct.unpack_from('<II', d, o + 0xc0)
        off = struct.unpack_from('<I', d, o + 0x124)[0]
        raw = d[off:off + csize]
        return lz4.block.decompress(raw, uncompressed_size=size) if csize < size else raw
    return None


def deswizzle(raw, w_blocks, h_blocks):
    """Block-linear BC storage (16-byte blocks, GOBs of 64 bytes x 8 rows, the tallest block height that fits) to
    row-major blocks."""
    row = w_blocks * 16
    gobs_w, bh = (row + 63) // 64, 1
    while bh < 16 and bh * 8 < h_blocks:
        bh *= 2
    out = bytearray(row * h_blocks)
    for y in range(h_blocks):
        for xb in range(row):
            x = xb % 64
            off = ((y // (8 * bh)) * gobs_w + xb // 64) * 512 * bh + ((y % (8 * bh)) // 8) * 512
            off += (x // 32) * 256 + ((y % 8) // 2) * 64 + ((x % 32) // 16) * 32 + (y % 2) * 16 + (x % 16)
            out[y * row + xb] = raw[off]
    return bytes(out)


def fnv16k(b):
    h = 0xcbf29ce484222325
    for c in b[:16384]:
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


def check_hud(pack, patches, sets, encoded):
    """--check-hud: each play HUD target read from the game's pack (NSW/UI/GAME/GAME.TEX): its hash, the replaced
    blocks against it (dark fill or the old letter only, every letter texel covered), and a preview of every set next
    to the pack's name under work/scratch (local only)."""
    problems, tiles = 0, {}
    for t in TARGETS[1:]:
        raw = tx12_entry(pack, t.name)
        if raw is None:
            print(f'{t.name}: not in {pack}')
            problems += 1
            continue
        if fnv16k(raw) != t.hash:
            print(f'{t.name}: hash {fnv16k(raw):016x}, expected {t.hash:016x}')
            problems += 1
        n = t.size // 4
        lin = deswizzle(raw, n, n)
        tex = decode([lin[i:i + 16] for i in range(0, len(lin), 16)], n, n).copy()
        lum = tex[..., :3].astype(int).mean(-1)
        (cell,) = t.cells()
        inside = inside_disc if cell in t.discs else inside_tab
        blocks = cell_blocks(t, cell)
        cover = np.zeros(lum.shape, bool)
        for bx, by in blocks:
            cover[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4] = True
        ys, xs = np.nonzero(cover)
        bad = [(x, y) for x, y in zip(xs, ys) if tex[y, x, 3] < 255 or
               (lum[y, x] > 80 and not inside(t, x + 0.5, y + 0.5, cell, 4.0))]
        if bad:
            print(f'{t.name}: {len(bad)} replaced texels are ring or transparent, first {bad[0]}')
            problems += 1
        ys, xs = np.nonzero((lum > 80) & (tex[..., 3] == 255))
        letter = [(x, y) for x, y in zip(xs, ys) if inside(t, x + 0.5, y + 0.5, cell, 4.0)]
        miss = [p for p in letter if not cover[p[1], p[0]]]
        if miss:
            print(f'{t.name}: {len(miss)} letter texels outside the replaced blocks, first {miss[0]}')
            problems += 1
        for si, s in enumerate(sets):
            out = tex.copy()
            for k in s:
                if k[0] == t.name:
                    dec = decode(encoded[k], len(patches[k][0]), 1)
                    for i, (bx, by) in enumerate(patches[k][0]):
                        out[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4] = dec[:, i * 4:i * 4 + 4]
            tiles.setdefault(si, [tex]).append(out)
    print('check-hud:', 'ok' if not problems else f'{problems} problems')
    sheet = Image.new('RGBA', (len(TARGETS) * 136, len(SETS) * 136), (60, 90, 60, 255))
    for si, row in tiles.items():
        for j, im in enumerate(row[1:]):
            sheet.alpha_composite(Image.fromarray(im), (j * 136 + 4, si * 136 + 4))
    path = ROOT / 'work' / 'scratch' / 'hud-icons-check.png'
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)
    print('wrote', path, '(rows:', ', '.join(f'{st} by {mp}' for st, mp in SETS) + ')')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, default=ROOT / 'work' / 'gen' / 'prompt_icons.h')
    ap.add_argument('--check', type=Path, help='the symbols texture exported from a capture (diagnosis)')
    ap.add_argument('--check-hud', type=Path, help="the game's NSW/UI/GAME/GAME.TEX (diagnosis)")
    a = ap.parse_args()
    if not FONT.is_file():
        sys.exit(f'{FONT} not found (Windows 10 and later have it)')
    patches, sets = build()
    keys = list(patches)
    texels = np.concatenate([np.stack(patches[k][1]) for k in keys])
    blocks = encode(texels)
    encoded, i = {}, 0
    for k in keys:
        encoded[k] = blocks[i:i + len(patches[k][0])]
        i += len(patches[k][0])
    # the encoder checked against Pillow's decoder: mean error per texel (colour weighted by coverage), worst icon
    worst = (0.0, None)
    for k in keys:
        dec = decode(encoded[k], len(encoded[k]), 1).astype(float)
        src = np.concatenate([t.reshape(4, 4, 4) for t in patches[k][1]], axis=1).astype(float)
        dif = np.abs(dec - src)
        dif[..., :3] *= src[..., 3:4] / 255.0
        worst = max(worst, (float(dif.mean()), k), key=lambda w: w[0])
    print(f'make_prompts: {len(keys)} icons, {len(blocks)} blocks, worst mean texel error {worst[0]:.2f} '
          f'({" ".join(worst[1])})')
    write_header(a.out, patches, sets, encoded)
    if a.check:
        check(a.check, patches, sets, encoded)
    if a.check_hud:
        check_hud(a.check_hud, patches, sets, encoded)


if __name__ == '__main__':
    main()
