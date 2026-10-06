"""
Procedural pixel-art environment for the 2D look tests.

  textures   32x32 seamless tiles, used two ways:
               - flat 2D: baked into one big ground image of the map (32 px per map tile)
               - HD-2D:   world-aligned on the 3D terrain / houses / ruins (pixel-art materials)
  props      3/4-view sprites: trees, bushes, rocks, ruin wall blocks, houses (one per footprint size)
"""
import numpy as np

from characters import C, mix, shadow_of, light_of

T = 32


def rng(seed):
    return np.random.default_rng(seed)


def solid(col):
    a = np.zeros((T, T, 4), np.uint8)
    a[...] = col
    return a


def speckle(a, r, cols, p):
    m = r.random((a.shape[0], a.shape[1])) < p
    idx = r.integers(0, len(cols), size=m.sum())
    a[m] = np.array(cols, np.uint8)[idx]
    return a


def grass(seed=1, flowers=True):
    r = rng(seed)
    base = C("#6aa84f")
    a = solid(base)
    speckle(a, r, [C("#5e9a46"), C("#78b85a")], 0.35)
    # little blades: dark tick with a light tip
    for _ in range(14):
        x, y = r.integers(0, T), r.integers(1, T)
        a[y, x] = C("#4e8a3c"); a[y - 1, x] = C("#86c466")
    if flowers:
        for _ in range(2):
            x, y = r.integers(1, T - 1), r.integers(1, T - 1)
            c = [C("#f4e070"), C("#f0f0f0"), C("#e88aa8")][r.integers(0, 3)]
            a[y, x] = c
    return a


def dirt(seed=2):
    r = rng(seed)
    a = solid(C("#c8a26a"))
    speckle(a, r, [C("#b8925c"), C("#d4b07a")], 0.4)
    for _ in range(6):
        x, y = r.integers(0, T - 1), r.integers(0, T - 1)
        a[y, x] = C("#9a7a52"); a[y, x + 1] = C("#e0c48e")
    return a


def cobble(seed=3, base="#9a9488"):
    r = rng(seed)
    a = solid(C("#5a5650"))
    b = C(base)
    for by in range(0, T, 8):
        off = 4 if (by // 8) % 2 else 0
        for bx in range(-8, T, 8):
            x0 = (bx + off) % T
            col = mix(b, C("#ffffff"), r.random() * 0.12) if r.random() < 0.5 else mix(b, C("#3a3040"), r.random() * 0.15)
            for yy in range(by + 1, by + 7):
                for xx in range(x0 + 1, x0 + 7):
                    a[yy % T, xx % T] = col
            for xx in range(x0 + 1, x0 + 7):
                a[(by + 1) % T, xx % T] = light_of(col)
                a[(by + 6) % T, xx % T] = shadow_of(col)
    return a


def plaster(seed=4):
    r = rng(seed)
    a = solid(C("#e8dcc4"))
    speckle(a, r, [C("#ddd0b6"), C("#f2e8d4")], 0.25)
    return a


def timber(seed=5, base="#6a4428"):
    r = rng(seed)
    a = solid(C(base))
    for y in range(T):
        if r.random() < 0.25:
            a[y, :] = shadow_of(C(base))
    speckle(a, r, [light_of(C(base))], 0.05)
    return a


def planks(seed=6, base="#a0743e"):
    a = solid(C(base))
    for y in range(0, T, 8):
        a[y, :] = shadow_of(C(base))
        a[y + 1, :] = light_of(C(base))
    for y in range(0, T, 8):
        x = (y * 5) % T
        a[y + 2:y + 8, x] = shadow_of(C(base))
    return a


def roof(seed=7, base="#b4503a"):
    a = solid(C(base))
    b = C(base)
    for y in range(0, T, 6):
        a[y + 4:y + 6, :] = shadow_of(b)
        a[y, :] = light_of(b)
        off = 4 if (y // 6) % 2 else 0
        for x in range(off, T, 8):
            a[y:y + 6, x] = shadow_of(b)
    return a[:T]


def stone(seed=8, base="#8a8478"):
    r = rng(seed)
    a = solid(shadow_of(C(base)))
    b = C(base)
    for by in range(0, T, 10):
        off = 6 if (by // 10) % 2 else 0
        for bx in range(0, T + 12, 12):
            col = mix(b, C("#ffffff"), r.random() * 0.1)
            for yy in range(by + 1, min(by + 9, T)):
                for xx in range(bx + off + 1, bx + off + 11):
                    a[yy, xx % T] = col
            for xx in range(bx + off + 1, bx + off + 11):
                a[by + 1, xx % T] = light_of(col)
    return a


def water(seed=9):
    r = rng(seed)
    a = solid(C("#3a78b8"))
    speckle(a, r, [C("#346eac"), C("#4284c4")], 0.3)
    for _ in range(5):
        x, y = r.integers(0, T - 4), r.integers(0, T)
        a[y, x:x + 4] = C("#8ac4f0")
    return a


def forest_floor(seed=10):
    r = rng(seed)
    a = solid(C("#3f6e3a"))
    speckle(a, r, [C("#36623a"), C("#4a7c42")], 0.4)
    return a


TEXTURES = {
    "grass": grass, "dirt": dirt, "cobble": cobble, "plaster": plaster, "timber": timber, "planks": planks,
    "roof": roof, "stone": stone, "rock": lambda s=11: stone(s, "#7a7672"), "water": water, "forest": forest_floor,
    "door": lambda s=12: planks(s, "#8a5a32"),
}


# --- props (3/4 view) ---------------------------------------------------------------------------

def blank(w, h):
    return np.zeros((h, w, 4), np.uint8)


def ellipse_mask(w, h, cx, cy, rx, ry):
    Y, X = np.mgrid[0:h, 0:w]
    return ((X - cx) / rx) ** 2 + ((Y - cy) / ry) ** 2 <= 1


def outline(a, col=(34, 26, 44, 255)):
    m = a[..., 3] > 0
    d = m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
    a[d & ~m] = col
    return a


def tree(seed=20, w=56, h=72):
    r = rng(seed)
    a = blank(w, h)
    # trunk
    tx = w // 2
    a[h - 14:h - 2, tx - 3:tx + 3] = C("#6a4428")
    a[h - 14:h - 2, tx + 1:tx + 3] = C("#4e3020")
    a[h - 3:h - 1, tx - 5:tx + 5] = C("#5a3a22")
    # canopy: clusters of leafy circles, shaded from the top-left
    leaf, leaf_d, leaf_l = C("#4f9a3f"), C("#2f6a34"), C("#8ccc5e")
    cy = h - 31
    blobs = [(tx, cy - 6, 15, 13), (tx - 11, cy + 4, 11, 10), (tx + 11, cy + 4, 11, 10), (tx, cy + 8, 13, 9), (tx - 5, cy - 12, 9, 8), (tx + 7, cy - 10, 9, 8)]
    mask = np.zeros((h, w), bool)
    for (bx, by, rx, ry) in blobs:
        mask |= ellipse_mask(w, h, bx + r.integers(-1, 2), by, rx, ry)
    a[mask] = leaf
    Y, X = np.mgrid[0:h, 0:w]
    a[mask & ((Y - (cy - 14)) + (X - tx) * 0.4 > 14)] = leaf_d
    for (bx, by, rx, ry) in blobs:
        hl = ellipse_mask(w, h, bx - rx * 0.3, by - ry * 0.35, rx * 0.45, ry * 0.35) & mask
        a[hl] = leaf_l
    # leafy texture
    m2 = mask & (r.random((h, w)) < 0.12)
    a[m2] = leaf_d
    return outline(a)


def bush(seed=21):
    r = rng(seed)
    w, h = 24, 18
    a = blank(w, h)
    mask = ellipse_mask(w, h, 12, 10, 10, 7) | ellipse_mask(w, h, 7, 9, 6, 6) | ellipse_mask(w, h, 17, 9, 6, 6)
    a[mask] = C("#4f9a3f")
    Y, X = np.mgrid[0:h, 0:w]
    a[mask & (Y > 11)] = C("#2f6a34")
    a[ellipse_mask(w, h, 9, 6, 4, 2.5) & mask] = C("#8ccc5e")
    if r.random() < 0.5:
        a[7, 14] = C("#e05050"); a[10, 6] = C("#e05050")
    return outline(a)


def rock(seed=22):
    w, h = 24, 18
    a = blank(w, h)
    mask = ellipse_mask(w, h, 12, 10, 10, 7)
    a[mask] = C("#8a8680")
    Y, X = np.mgrid[0:h, 0:w]
    a[mask & (Y > 11)] = C("#5e5a58")
    a[ellipse_mask(w, h, 9, 7, 5, 3) & mask] = C("#b4b0a8")
    return outline(a)


def wall_block():
    """One ruin-wall tile in 3/4 view: top face (32x16) above a front face (32x24)."""
    a = blank(T, 40)
    top = stone(31, "#a09a8c")[:16]
    front = stone(32, "#7a7468")[:24]
    a[0:16] = top
    a[16:40] = front
    a[16, :] = C("#4a4440")
    return a


def house(wt, ht, seed=40):
    """A cottage covering wt x ht map tiles, seen 3/4 from the south: roof on top, front wall with a door below."""
    w = wt * T
    wall_h = 40
    roof_h = ht * T + 8
    h = roof_h + wall_h - 8
    a = blank(w, h)
    rf = roof(seed)
    for y in range(roof_h):
        for x in range(0, w, T):
            a[y, x:x + T] = rf[y % T, : min(T, w - x)]
    a[0:3, :] = light_of(C("#b4503a"))          # ridge
    a[roof_h - 3:roof_h, :] = shadow_of(C("#7a2e22"))  # eave shadow
    wall_top = roof_h - 2
    pl = plaster(seed + 1)
    for y in range(wall_top, h):
        for x in range(0, w, T):
            a[y, x:x + T] = pl[(y - wall_top) % T, : min(T, w - x)]
    # timber frame
    tb = C("#5a3a22")
    a[wall_top:wall_top + 2, :] = tb
    a[h - 3:h, :] = C("#7a7468")
    for x in list(range(0, w, 24)) + [w - 3]:
        a[wall_top:h - 3, x:x + 3] = tb
    a[wall_top + 16:wall_top + 18, :] = tb
    # door + windows
    cx = w // 2
    a[h - 26:h - 3, cx - 7:cx + 7] = C("#8a5a32")
    a[h - 26:h - 3, cx - 7:cx - 6] = shadow_of(C("#8a5a32"))
    a[h - 15, cx + 4] = C("#e0c050")
    for wx in (w // 5, w - w // 5):
        a[h - 30:h - 19, wx - 6:wx + 6] = C("#2a3448")
        a[h - 29:h - 27, wx - 5:wx - 2] = C("#8ab0d8")
        a[h - 30:h - 19, wx] = tb
        a[h - 19:h - 17, wx - 7:wx + 7] = C("#6a4428")
    # chimney
    a[2:14, w - 30:w - 22] = C("#7a7468")
    a[2:4, w - 31:w - 21] = C("#5a5450")
    return outline(a)


def blob_shadow(w=28, h=10):
    """A soft-looking ground shadow in pixel style: a dithered dark ellipse (masked materials have no half alpha)."""
    a = blank(w, h)
    Y, X = np.mgrid[0:h, 0:w]
    inner = ellipse_mask(w, h, w / 2 - 0.5, h / 2 - 0.5, w / 2 - 1.5, h / 2 - 1)
    core = ellipse_mask(w, h, w / 2 - 0.5, h / 2 - 0.5, w / 2 - 5, h / 2 - 2.5)
    dark = (24, 30, 22, 255)
    a[core] = dark
    a[inner & ~core & (((X + Y) % 2) == 0)] = dark
    return a
