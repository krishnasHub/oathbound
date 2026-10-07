"""
Pixel art for the world's mood and the Scholar's quests (TODO.md phases 2-5). All facing right (the game mirrors them).

  Ambient life (one row of frames; frame 0 doubles as idle):
    SPR_ChildA / SPR_ChildB   64x64 x4   children running and skipping (good mood, by day)
    SPR_Grumpy                64x64 x4   a hunched, scowling villager trudging along (bad mood, by day)
    SPR_Puppy                 48x48 x4   a trotting puppy, tail wagging (good mood)
    SPR_ButterflyA / B        32x32 x4   wing flap: open, half, edge-on, half (good mood)
    SPR_Wolf                  64x48 x4   a grey wolf at a gallop (bad mood, at night)
    SPR_Bat                   48x32 x4   flap: up, mid, down, mid (bad mood, at night)
    SPR_Snake                 48x24 x4   slithering, tongue flicking (bad mood, at night)

  Props (one image; the bottom edge is the ground line):
    PR_GraveMound 64x40, PR_GraveMarker 48x64, PR_Cave 160x128, PR_GoldSack 24x24, PR_Signet 32x32, PR_Bread 32x32,
    PR_Puddle 64x32 (lies flat on a road), PR_WallCrack 48x48 and PR_Moss 48x32 (overlays for house walls),
    PR_Vines 48x48 and PR_VinesCorner 48x64 (good-mood wall accents), PR_Sunray 32x128 (a soft light shaft),
    PR_RoadCrack / PR_RoadCrackB 64x32 (cracks in an unkept road; lie flat)
"""
import math

import numpy as np

import tessera_path  # noqa: F401
import characters as ch
from characters import C, mix, shadow_of, light_of
from tspixel.canvas import blank, ellipse_mask, outline as _wrap_outline, disc, line, poly_mask, rng

INK = (34, 26, 44, 255)


def outline(a, col=INK):
    """tspixel outline on a 1-pixel padded copy, so nothing wraps round to the opposite edge."""
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)))
    return _wrap_outline(p, col)[1:-1, 1:-1]


def _row(frames):
    return np.concatenate(frames, axis=1)


def _fill(a, mask, col, shade=True):
    """Fill a mask with col, a darker rim along its bottom/right and a lighter top rim (the house style)."""
    if not mask.any():
        return
    a[mask] = col
    if not shade:
        return
    sh = mask & ~np.roll(mask, -1, axis=1) | mask & ~np.roll(mask, -2, axis=0)
    a[sh] = shadow_of(col)
    hl = mask & ~np.roll(mask, 1, axis=0) & ~sh
    a[hl] = light_of(col)


def _limb(a, x0, y0, x1, y1, col, width=3):
    line(a, x0, y0, x1, y1, col, width)


# --- children ------------------------------------------------------------------------------------------

def _child(f, cloth, hair, dress=False, pigtails=False):
    """A small child (about 60% of an adult) running right: big head, short limbs, a skip in the step."""
    a = blank(64, 64)
    skin = C("#f2c6a0")
    bob = (0, -3, -1, -3)[f]
    swing = (0, 1, 0, -1)[f]
    hip = (32, 46 + bob)
    # legs (back leg darker), knees flung forward / back
    legc, shoe = C("#5a4a6a") if not dress else skin, C("#4a2e20")
    for side, s in ((-1, swing), (1, -swing)):
        col = shadow_of(legc) if side < 0 else legc
        fx, fy = hip[0] + s * 7, 58 if s * side <= 0 else 55
        _limb(a, hip[0] + side, hip[1], fx, fy, col, 4)
        a[fy - 1:fy + 2, fx - 2:fx + 4] = shoe
    # the far arm swings behind the body
    _limb(a, 31, 36 + bob, 32 + swing * 7 - 1, 43 + bob - abs(swing) * 2, shadow_of(skin), 3)
    # body
    if dress:
        _fill(a, poly_mask([(25, 34 + bob), (39, 34 + bob), (43, 49 + bob), (21, 49 + bob)], 64), cloth)
    else:
        _fill(a, ellipse_mask(64, 64, 32, 40 + bob, 7, 8), cloth)
        a[46 + bob:48 + bob, 26:39] = shadow_of(cloth)
    # the near arm pumps the other way, in front
    _limb(a, 33, 36 + bob, 32 - swing * 7 + 1, 43 + bob - abs(swing) * 2, skin, 3)
    # head (big), hair, face looking right
    hy = 24 + bob
    _fill(a, ellipse_mask(64, 64, 33, hy, 9, 9), skin)
    hairm = ellipse_mask(64, 64, 31, hy - 3, 10, 7) & ~ellipse_mask(64, 64, 37, hy + 1, 6, 6)
    _fill(a, hairm, hair)
    if pigtails:
        for px in (22, 41):
            disc(a, px, hy + (2 if px < 30 else 0) + swing, 3.2, hair)
            a[hy - 1 + swing:hy + 1 + swing, px - 1:px + 1] = C("#e05070")
    a[hy, 37:39] = INK                      # eye
    a[hy + 4, 37:40] = C("#c0504a")         # smile
    a[hy + 3, 36] = C("#c0504a")
    a[hy + 2, 39:41] = C("#f0a0a0")         # cheek
    return outline(a)


def spr_child_a():
    return _row([_child(f, C("#c8402e"), C("#6a3a1a")) for f in range(4)])


def spr_child_b():
    return _row([_child(f, C("#4a7ac8"), C("#e8c860"), dress=True, pigtails=True) for f in range(4)])


# --- the grumpy villager ---------------------------------------------------------------------------------

GRUMPY = dict(torso="#6a5e52", sleeve="#5a5048", legs="#4a4440", boots="#2e2420", belt="#3a3028",
              hair="#5a5048", beard="#6a625a", brow="#2a2018", build="wide", helmet="cap", capColor="#4a4038")


def spr_grumpy():
    """An adult villager trudging along (the standard figure's side walk), hunched, with a scowl drawn on."""
    frames = []
    res = ch.RES
    ch.use_res(2)
    for f in range(4):
        fr = ch.humanoid(GRUMPY, "side", "walk", f)
        a = np.zeros((64, 64, 4), np.uint8)
        # Hunch: shift the head and shoulders forward and down a little.
        a[2:, :] = fr[:-2, :]
        top = a[:30].copy()
        a[:30] = 0
        a[2:32, 2:] = top[:30, :-2][:30]
        m = a[..., 3] > 0
        # Scowl: a heavy brow line and a turned-down mouth on the face (side view, facing right).
        ys, xs = np.nonzero(m[:34])
        if len(xs):
            fx = xs.max()
            face_y = ys[xs >= fx - 3].mean() if (xs >= fx - 3).any() else 20
            fy = int(face_y)
            a[fy - 3, fx - 6:fx - 1] = INK
            a[fy + 4, fx - 5:fx - 2] = C("#5a2a20")
            a[fy + 5, fx - 6] = C("#5a2a20")
        frames.append(a)
    ch.use_res(res)
    return _row(frames)


# --- puppy ------------------------------------------------------------------------------------------------

def spr_puppy():
    frames = []
    fur, dark, belly = C("#c89a60"), C("#7a5232"), C("#e8c896")
    for f in range(4):
        a = blank(48, 48)
        bob = (0, -1, 0, -1)[f]
        _fill(a, ellipse_mask(48, 48, 22, 32 + bob, 10, 6), fur)                      # body
        a[ellipse_mask(48, 48, 22, 35 + bob, 7, 3)] = belly
        # legs trot in diagonal pairs
        st = (0, 2, 0, -2)[f]
        for x, s in ((15, st), (19, -st), (26, -st), (30, st)):
            col = shadow_of(fur) if x in (19, 30) else fur
            a[36 + bob:42, x + s:x + s + 3] = col
            a[41:43, x + s:x + s + 3] = dark
        # tail: wags up and down
        ty = (24, 21, 24, 27)[f]
        _limb(a, 12, 30 + bob, 7, ty + bob, fur, 3)
        # head, ear, snout, eye, tongue
        _fill(a, ellipse_mask(48, 48, 34, 24 + bob, 7, 6), fur)
        _fill(a, ellipse_mask(48, 48, 40, 27 + bob, 4, 3), belly)
        a[25 + bob:27 + bob, 42:44] = INK                                           # nose
        _fill(a, ellipse_mask(48, 48, 31, 21 + bob + (f % 2), 3, 6), dark)          # floppy ear
        a[22 + bob, 36:38] = INK                                                    # eye
        a[23 + bob, 37] = (255, 255, 255, 255)
        if f % 2 == 0:
            a[30 + bob:32 + bob, 40:42] = C("#e05a6a")                              # tongue
        frames.append(outline(a))
    return _row(frames)


# --- butterflies ----------------------------------------------------------------------------------------

def _butterfly(f, wing, edge, spots):
    a = blank(32, 32)
    span = (1.0, 0.55, 0.12, 0.55)[f]          # open, half, edge-on, half
    cx, cy = 16, 16
    for up, ry, rx in ((True, 7, 7), (False, 5, 5)):
        w = max(1, int(rx * span))
        oy = -4 if up else 3
        for side in (-1, 1):
            m = ellipse_mask(32, 32, cx + side * (w * 0.8 + 1), cy + oy, max(w, 1), ry if up else ry)
            a[m] = wing
            if span > 0.3:
                a[m & ~ellipse_mask(32, 32, cx + side * (w * 0.8 + 1), cy + oy, max(w - 2, 1), max(ry - 2, 1))] = edge
                disc(a, cx + side * (w * 1.1 + 1), cy + oy - (1 if up else 0), 1.0, spots)
    a[cy - 6:cy + 6, cx - 1:cx + 1] = INK                     # body
    a[cy - 9, cx - 3] = INK; a[cy - 9, cx + 2] = INK           # antennae tips
    line(a, cx - 1, cy - 6, cx - 3, cy - 9, INK)
    line(a, cx, cy - 6, cx + 2, cy - 9, INK)
    return outline(a)


def spr_butterfly_a():
    return _row([_butterfly(f, C("#f08a20"), C("#2a1a14"), C("#fff4d8")) for f in range(4)])


def spr_butterfly_b():
    return _row([_butterfly(f, C("#a8d4f4"), C("#3a5a8a"), C("#ffffff")) for f in range(4)])


# --- wolf ------------------------------------------------------------------------------------------------

def spr_wolf():
    frames = []
    fur, dark, pale = C("#8a8a92"), C("#4a4a54"), C("#c8c8cc")
    for f in range(4):
        a = blank(64, 48)
        stretch = (1.0, 0.8, 0.6, 0.85)[f]                      # gallop: extended, gathering, bunched, pushing
        rise = (0, -2, -1, 1)[f]
        _fill(a, ellipse_mask(64, 48, 30, 26 + rise, 15, 6), fur)           # lean body
        a[ellipse_mask(64, 48, 28, 29 + rise, 10, 2)] = pale              # underbelly
        a[ellipse_mask(64, 48, 26, 21 + rise, 9, 2)] = dark               # back stripe
        # legs: front pair forward/back with the stride, hind pair opposite
        reach = int(10 * stretch)
        for (x, dx, col) in ((40, reach, fur), (37, reach // 2, dark), (20, -reach, fur), (23, -reach // 2, dark)):
            _limb(a, x, 29 + rise, x + dx * 0.6, 40, col, 3)
            a[39:41, int(x + dx * 0.6) - 1:int(x + dx * 0.6) + 3] = dark
        # tail streaming behind
        _limb(a, 15, 24 + rise, 6, 22 + rise + (f % 2) * 2, fur, 3)
        a[21 + rise:23 + rise, 4:7] = pale
        # head, snout, ear, eye
        _fill(a, ellipse_mask(64, 48, 47, 20 + rise, 6, 5), fur)
        _fill(a, poly_mask([(49, 18 + rise), (59, 21 + rise), (58, 24 + rise), (49, 24 + rise)], 64, 48), fur)
        a[21 + rise:23 + rise, 57:59] = INK                                # nose
        a[poly_mask([(44, 16 + rise), (46, 9 + rise), (49, 15 + rise)], 64, 48)] = dark   # ear
        a[18 + rise, 49:51] = C("#e8d870")                                  # pale yellow eye
        if f == 2:
            a[24 + rise, 52:57] = C("#e8e0d8")                              # teeth flash
        frames.append(outline(a))
    return _row(frames)


# --- bat ---------------------------------------------------------------------------------------------------

def spr_bat():
    frames = []
    body, wing = C("#4a3040"), C("#2e1e2a")
    for f in range(4):
        a = blank(48, 32)
        tip = (-11, -2, 9, -2)[f]                                    # wing tips: up, mid, down, mid
        for side in (-1, 1):
            pts = [(24, 14), (24 + side * 8, 12 + tip // 2), (24 + side * 20, 14 + tip),
                   (24 + side * 15, 18 + tip // 2), (24 + side * 10, 17), (24 + side * 5, 19)]
            a[poly_mask(pts, 48, 32)] = wing
            line(a, 24, 14, 24 + side * 20, 14 + tip, shadow_of(wing))
        _fill(a, ellipse_mask(48, 32, 24, 16, 4, 5), body)
        a[poly_mask([(21, 12), (22, 7), (23, 12)], 48, 32)] = body       # ears
        a[poly_mask([(25, 12), (26, 7), (27, 12)], 48, 32)] = body
        a[14, 22] = C("#f4e8c8"); a[14, 26] = C("#f4e8c8")               # tiny pale eyes
        frames.append(outline(a))
    return _row(frames)


# --- snake --------------------------------------------------------------------------------------------------

def spr_snake():
    frames = []
    green, belly, dark = C("#6a7a3a"), C("#c8b878"), C("#3a4420")
    for f in range(4):
        a = blank(48, 24)
        ph = f * math.pi / 2
        pts = [(x, 15 + 3.2 * math.sin(x / 5.5 - ph) * (0.4 + 0.6 * x / 36)) for x in range(3, 36)]
        for i, (x, y) in enumerate(pts):
            r = 1.2 + 1.8 * min(1.0, i / 10)                     # tapering tail
            disc(a, x, y, r, green)
        for i, (x, y) in enumerate(pts[6::5]):
            a[int(y), int(x)] = dark                               # pattern
        hx, hy = 38, int(pts[-1][1])
        _fill(a, ellipse_mask(48, 24, hx, hy, 4, 3), green)
        a[hy - 1, hx + 1] = C("#f0d040")                           # eye
        a[hy + 1:hy + 2, hx - 3:hx + 2] = belly
        if f in (1, 2):                                            # tongue flick
            a[hy + 1, hx + 4:hx + 7] = C("#d03040")
            a[hy, hx + 7] = C("#d03040"); a[hy + 2, hx + 7] = C("#d03040")
        frames.append(outline(a))
    return _row(frames)


# --- props --------------------------------------------------------------------------------------------------

def pr_grave_mound():
    a = blank(64, 40)
    r = rng(71)
    earth = C("#6a4a30")
    m = ellipse_mask(64, 40, 32, 36, 26, 11) & (np.mgrid[0:40, 0:64][0] < 39)
    _fill(a, m, earth)
    ys, xs = np.nonzero(m)
    for _ in range(40):
        k = r.integers(0, len(xs))
        a[ys[k], xs[k]] = (C("#5a3e28") if r.random() < 0.6 else C("#8a6a48"))
    for wx in (14, 22, 41, 50):                                    # a few weeds
        h = int(r.integers(4, 7))
        for k in range(h):
            a[30 - k, wx + (k % 2)] = C("#5a8a3a")
        a[31 - h, wx - 1] = C("#7aaa4a")
    return outline(a)


def pr_grave_marker():
    a = blank(48, 64)
    stone = C("#9a968c")
    m = poly_mask([(12, 60), (12, 18), (16, 10), (24, 6), (32, 10), (36, 18), (36, 60)], 48, 64)
    _fill(a, m, stone)
    r = rng(72)
    ys, xs = np.nonzero(m)
    for _ in range(50):
        k = r.integers(0, len(xs))
        a[ys[k], xs[k]] = mix(stone, C("#5a6a4a"), 0.5) if ys[k] > 48 else shadow_of(stone)
    # a carved rune: a circle crossed by a line, and two ticks
    for t in range(0, 360, 20):
        x = 24 + 6 * math.cos(math.radians(t)); y = 26 + 6 * math.sin(math.radians(t))
        a[int(y), int(x)] = C("#4a463e")
    line(a, 24, 17, 24, 38, C("#4a463e"))
    line(a, 18, 34, 22, 30, C("#4a463e")); line(a, 30, 34, 26, 30, C("#4a463e"))
    # fresh flowers at the foot
    for fx, col in ((9, "#f0d040"), (15, "#e05070"), (33, "#f4f4f4"), (39, "#a070e0")):
        line(a, fx, 63, fx, 56, C("#4a8a3a"))
        disc(a, fx, 55, 2, C(col)); a[55, fx] = C("#f0c030")
    return outline(a)


def pr_cave():
    a = blank(160, 128)
    r = rng(73)
    rock, rock2, moss = C("#7a7068"), C("#5e564e"), C("#5a7a3a")
    # boulders piled into an arch
    boulders = [(28, 108, 26, 20), (132, 108, 26, 20), (34, 76, 22, 20), (126, 76, 22, 20), (52, 48, 24, 20),
                (108, 48, 24, 20), (80, 34, 30, 18), (16, 92, 16, 14), (146, 92, 14, 14)]
    for i, (bx, by, rx, ry) in enumerate(boulders):
        _fill(a, ellipse_mask(160, 128, bx, by, rx, ry), rock if i % 2 else rock2)
    # the dark opening, wider at the bottom
    hole = ellipse_mask(160, 128, 80, 118, 36, 52) & (np.mgrid[0:128, 0:160][0] < 128)
    a[hole] = C("#0c0a10")
    a[hole & ~ellipse_mask(160, 128, 80, 122, 30, 46)] = C("#1e1a22")
    # cracks and moss on the rocks
    m = (a[..., 3] > 0) & ~hole
    ys, xs = np.nonzero(m)
    for _ in range(220):
        k = r.integers(0, len(xs))
        a[ys[k], xs[k]] = shadow_of(rock2) if r.random() < 0.6 else light_of(rock)
    for _ in range(18):
        k = r.integers(0, len(xs))
        if ys[k] < 70:
            disc(a, xs[k], ys[k], 2.2, moss)
    return outline(a)


def pr_gold_sack():
    a = blank(24, 24)
    cloth, gold = C("#b08a5a"), C("#f0c030")
    _fill(a, ellipse_mask(24, 24, 11, 15, 7, 7), cloth)
    a[6:9, 9:14] = cloth                                       # neck
    a[8, 8:15] = C("#6a4a2a")                                  # tie
    disc(a, 11, 6, 3, gold); a[5, 10] = light_of(gold)         # gold bulging out the top
    for cx, cy in ((19, 21), (5, 22)):                          # spilled coins
        a[cy - 1:cy + 1, cx - 2:cx + 2] = gold; a[cy - 1, cx - 1] = light_of(gold)
    return outline(a)


def pr_signet():
    a = blank(32, 32)
    for k in range(6):                                         # grass blades
        x = 5 + k * 5
        line(a, x, 31, x + (1 if k % 2 else -1), 24, C("#5a9a3a"))
    gold = C("#e8b830")
    ring = ellipse_mask(32, 32, 16, 22, 7, 4) & ~ellipse_mask(32, 32, 16, 22, 4, 2)
    _fill(a, ring, gold)
    _fill(a, ellipse_mask(32, 32, 16, 17, 4, 3), gold)          # the seal
    a[16:18, 15:18] = C("#8a2a2a")                              # red stone
    a[13, 22] = (255, 255, 240, 255); a[12, 23] = (255, 255, 240, 255); a[14, 21] = (255, 255, 240, 255)   # glint
    a[11, 22] = (255, 255, 240, 255); a[13, 24] = (255, 255, 240, 255)
    return outline(a)


def pr_bread():
    a = blank(32, 32)
    wicker = C("#a8743a")
    m = poly_mask([(4, 18), (28, 18), (25, 30), (7, 30)], 32)
    _fill(a, m, wicker)
    for y in range(20, 30, 3):
        a[y, 6:27] = shadow_of(wicker)
    for bx, by in ((11, 16), (20, 15), (15, 12)):              # loaves
        _fill(a, ellipse_mask(32, 32, bx, by, 6, 4), C("#d89a4a"))
        a[by - 1, bx - 2:bx + 3:2] = C("#f0d0a0")
    return outline(a)


def pr_puddle():
    a = blank(64, 32)
    _fill(a, ellipse_mask(64, 32, 32, 17, 29, 12), C("#6a5038"), shade=False)    # wet mud rim
    water = ellipse_mask(64, 32, 32, 17, 25, 9)
    a[water] = C("#5a7a98")
    a[water & ellipse_mask(64, 32, 27, 14, 15, 4)] = C("#9ac0dc")               # sky reflection
    a[13, 20:26] = C("#e0f0f8"); a[19, 38:42] = C("#e0f0f8")                     # glints
    return a   # no outline: it lies flat in the road


def pr_wall_crack():
    a = blank(48, 48)
    r = rng(74)
    patch = poly_mask([(10, 14), (24, 9), (36, 16), (38, 30), (27, 38), (13, 33), (8, 24)], 48)
    a[patch] = C("#9a5a40")                                     # bricks showing through
    for y in range(10, 40, 4):
        a[y, :][patch[y]] = C("#6a3a2a")
        for x in range(8 + (y // 4 % 2) * 4, 40, 8):
            if patch[y + 1 if y + 1 < 48 else y, x]:
                a[y:y + 4, x][patch[y:y + 4, x]] = C("#6a3a2a")
    edge = patch & ~np.roll(patch, 1, 0) | patch & ~np.roll(patch, -1, 0) | patch & ~np.roll(patch, 1, 1) | patch & ~np.roll(patch, -1, 1)
    a[edge] = C("#d8cbb0")                                      # broken plaster edge
    for (x0, y0), (dx, dy) in (((36, 16), (8, -9)), ((13, 33), (-7, 10)), ((38, 30), (8, 6))):
        x, y = x0, y0
        for _ in range(4):
            nx, ny = x + dx / 4 + r.integers(-1, 2), y + dy / 4 + r.integers(-1, 2)
            line(a, x, y, nx, ny, C("#5a4a3a"))
            x, y = nx, ny
    return a


def pr_moss():
    a = blank(48, 32)
    r = rng(75)
    for _ in range(9):                                          # streaks running down
        x = int(r.integers(4, 44)); y0 = int(r.integers(0, 10)); h = int(r.integers(8, 24))
        for y in range(y0, min(31, y0 + h)):
            a[y, x + (1 if (y // 5) % 2 else 0)] = C("#2a3a22") if r.random() < 0.7 else C("#3a2a22")
    for _ in range(6):                                          # moss clumps along the bottom
        disc(a, int(r.integers(4, 44)), int(r.integers(22, 30)), float(r.uniform(2, 4)), C("#4a6a2e"))
    stain = ellipse_mask(48, 32, 24, 14, 14, 8)
    a[stain & (a[..., 3] == 0)] = (60, 50, 40, 90)              # a faint grime stain
    return a


LEAF, LEAF_DK, LEAF_LT, STEM = C("#5aa040"), C("#3a7a2e"), C("#8ccf5a"), C("#4a6a2a")


def _leaf(a, x, y, right):
    """A tiny two-tone leaf (3x2) off a stem point, to the left or right."""
    h, w, _ = a.shape
    d = 1 if right else -1
    for dx, dy, col in ((d, 0, LEAF), (2 * d, 0, LEAF), (d, -1, LEAF_LT), (2 * d, 1, LEAF_DK), (3 * d, 0, LEAF_DK)):
        if 0 <= x + dx < w and 0 <= y + dy < h:
            a[y + dy, x + dx] = col


def _flower(a, x, y, petal):
    h, w, _ = a.shape
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if 0 <= x + dx < w and 0 <= y + dy < h:
            a[y + dy, x + dx] = petal
    a[y, x] = C("#f0d040")


def _vine(a, r, x, y0, y1, step, sway, leaf_every=3):
    """A wavy stem from row y0 toward y1 (step +1 down / -1 up), leaves alternating sides; returns the stem points."""
    pts = []
    ph = float(r.uniform(0, 6.28))
    for i, y in enumerate(range(y0, y1, step)):
        xx = int(round(x + math.sin(i * 0.35 + ph) * sway))
        a[y, xx] = STEM
        pts.append((xx, y))
        if i % leaf_every == 1:
            _leaf(a, xx, y, (i // leaf_every) % 2 == 0)
    return pts


def pr_vines():
    """A few leafy tendrils climbing a wall from the bottom edge; mostly see-through, a couple of tiny flowers."""
    a = blank(48, 48)
    r = rng(76)
    for x, top, sway in ((9, 14, 2.0), (22, 24, 1.5), (36, 18, 2.2)):
        pts = _vine(a, r, x, 47, top, -1, sway)
        tx, ty = pts[-1]
        a[ty - 1, tx + 1] = STEM; a[ty - 2, tx + 2] = LEAF_LT          # a curled tip
    _flower(a, 11, 22, C("#f4a0c0")); _flower(a, 34, 27, C("#fbf4f0")); _flower(a, 24, 33, C("#f4a0c0"))
    return a


def pr_vines_corner():
    """A vine draping down from the top edge (eaves, a window lintel), thinning out as it falls."""
    a = blank(48, 64)
    r = rng(77)
    line(a, 2, 1, 45, 2, STEM)                                         # the run along the top
    for x in range(4, 46, 5):
        _leaf(a, x, 2, x % 2 == 0)
    for x, bottom, sway in ((8, 40, 1.8), (21, 56, 2.0), (33, 30, 1.4), (42, 46, 1.6)):
        _vine(a, r, x, 3, bottom, 1, sway)
    _flower(a, 22, 30, C("#f4a0c0")); _flower(a, 41, 20, C("#fbf4f0"))
    return a


def pr_sunray():
    """A soft vertical shaft of warm light: brightest in the middle, fading to nothing at the sides and both ends."""
    w, h = 32, 128
    x = (np.arange(w) - (w - 1) / 2) / (w / 2)
    y = np.arange(h) / (h - 1)
    across = np.clip(1 - x ** 2, 0, 1) ** 1.5
    along = np.clip(np.minimum(y / 0.3, (1 - y) / 0.25), 0, 1) ** 1.2
    alpha = np.outer(along, across) * 110
    a = blank(w, h)
    a[..., 0], a[..., 1], a[..., 2] = 255, 228, 150
    a[..., 3] = (np.round(alpha / 10) * 10).clip(0, 110).astype(np.uint8)   # stepped, pixel-art banding
    return a


def _road_crack(seed, start, heading, branches):
    """Cracks in packed dirt seen from above: a jagged main crack with a few branches, chips beside it. Lies flat."""
    a = blank(64, 32)
    r = rng(seed)
    dark, edge, chip = C("#2a1c14"), C("#7a5a3c"), C("#a88a64")

    def crack(x, y, ang, steps, step=4.0, width=2):
        pts = [(x, y)]
        for _ in range(steps):
            ang += float(r.uniform(-0.6, 0.6))
            x, y = x + math.cos(ang) * step, y + math.sin(ang) * step * 0.6
            if not 2 <= y <= 29:                                  # glance off the top / bottom edge
                y, ang = min(max(y, 2), 29), -ang
            if not 1 <= x <= 62:
                break
            pts.append((x, y))
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if width > 1:
                line(a, int(x0), int(y0) + 1, int(x1), int(y1) + 1, edge)    # the lit lip under the dark split
            line(a, int(x0), int(y0), int(x1), int(y1), dark)
        return pts

    main = crack(start[0], start[1], heading, 16)
    for i, side, n in branches:
        if i < len(main):
            bx, by = main[i]
            crack(bx, by, heading + side, n, step=3.0, width=1)
    for _ in range(int(r.integers(4, 7))):                  # loose chips of dirt near the crack
        bx, by = main[int(r.integers(0, len(main)))]
        cx, cy = int(bx + r.integers(-5, 6)), int(by + r.integers(-4, 5))
        if 0 <= cx < 63 and 0 <= cy < 31 and a[cy, cx, 3] == 0:
            a[cy, cx] = chip
            if r.random() < 0.4:
                a[cy, cx + 1] = edge
    return a   # no outline: it lies flat in the road


def pr_road_crack():
    return _road_crack(81, (3, 18), -0.15, [(4, -1.1, 4), (9, 1.0, 5), (13, -0.9, 3)])


def pr_road_crack_b():
    return _road_crack(82, (4, 25), -0.35, [(3, -1.3, 4), (7, 1.2, 4), (11, -1.1, 5)])


SHEETS = {
    "SPR_ChildA": spr_child_a, "SPR_ChildB": spr_child_b, "SPR_Grumpy": spr_grumpy, "SPR_Puppy": spr_puppy,
    "SPR_ButterflyA": spr_butterfly_a, "SPR_ButterflyB": spr_butterfly_b, "SPR_Wolf": spr_wolf, "SPR_Bat": spr_bat,
    "SPR_Snake": spr_snake,
    "PR_GraveMound": pr_grave_mound, "PR_GraveMarker": pr_grave_marker, "PR_Cave": pr_cave, "PR_GoldSack": pr_gold_sack,
    "PR_Signet": pr_signet, "PR_Bread": pr_bread, "PR_Puddle": pr_puddle, "PR_WallCrack": pr_wall_crack, "PR_Moss": pr_moss,
    "PR_Vines": pr_vines, "PR_VinesCorner": pr_vines_corner, "PR_Sunray": pr_sunray,
    "PR_RoadCrack": pr_road_crack, "PR_RoadCrackB": pr_road_crack_b,
}
