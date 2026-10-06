"""
Small pixel-art pieces for the UI (title and character select):

  FX_Arrow / FX_Orb / FX_Heal / FX_Ring    effects the character-select showcase flies and bursts (tinted in game)
  ICO_<name>                               16x16 kit icons: sword, shield, bow, staff, dagger, buckler, speech
  UI_Stage                                 a little grass stage for the hero to stand on
"""
import numpy as np

from characters import C, mix, shadow_of, light_of
from environment import blank, ellipse_mask, outline, grass


def fx_arrow():
    a = blank(24, 7)
    a[3, 3:19] = C("#8a5a30")                       # shaft
    a[2, 19:21] = a[4, 19:21] = C("#c9d3df")         # head
    a[3, 19:23] = C("#e8eef6")
    a[2, 0:4] = a[4, 0:4] = C("#ffffff")             # fletching (white: tinted in game)
    a[1, 0:2] = a[5, 0:2] = C("#ffffff")
    return outline(a)


def fx_orb():
    a = blank(12, 12)
    Y, X = np.mgrid[0:12, 0:12]
    d = np.hypot(X - 5.5, Y - 5.5)
    a[d <= 5.2] = (255, 255, 255, 140)
    a[d <= 3.8] = (255, 255, 255, 230)
    a[d <= 1.8] = (255, 255, 255, 255)
    return a


def fx_heal():
    a = blank(9, 9)
    a[1:8, 3:6] = C("#ffffff")
    a[3:6, 1:8] = C("#ffffff")
    return outline(a, (30, 60, 30, 255))


def fx_ring():
    a = blank(48, 48)
    Y, X = np.mgrid[0:48, 0:48]
    d = np.hypot(X - 23.5, Y - 23.5)
    a[(d <= 23) & (d >= 19)] = (255, 255, 255, 255)
    a[(d < 19) & (d >= 16) & (((X + Y) % 2) == 0)] = (255, 255, 255, 200)
    return a


def icon(name):
    a = blank(16, 16)
    steel, wood, gold = C("#d4dce6"), C("#8a5a30"), C("#e0c050")
    if name == "sword":
        for i in range(10): a[12 - i, 3 + i] = steel; a[11 - i, 3 + i] = light_of(steel)
        a[10:14, 2] = gold; a[12, 1:5] = gold; a[13:15, 1:3] = wood
    elif name == "dagger":
        for i in range(6): a[11 - i, 5 + i] = steel
        a[11, 3:7] = gold; a[12:14, 3:5] = wood
    elif name == "shield":
        m = ellipse_mask(16, 16, 7.5, 7, 6, 7)
        a[m] = C("#3a5a9a"); a[2:13, 7:9] = gold; a[5:7, 3:13] = gold
    elif name == "buckler":
        m = ellipse_mask(16, 16, 7.5, 7.5, 5.5, 5.5)
        a[m] = C("#a07840"); a[6:10, 6:10] = C("#e0d090")
    elif name == "bow":
        for t in range(-6, 7):
            a[7 + t, int(round(5 + (1 - (t / 6.5) ** 2) * 4))] = wood
        a[1:14, 5] = C("#e8e0d0")
        a[7, 5:14] = C("#c9a070"); a[6:9, 13] = steel
    elif name == "staff":
        for i in range(12): a[14 - i, 3 + i // 2 + 2] = wood
        m = ellipse_mask(16, 16, 10, 3, 2.6, 2.6)
        a[m] = C("#b9a4ff"); a[2, 9] = C("#ffffff")
    elif name == "speech":
        m = ellipse_mask(16, 16, 7.5, 6.5, 7, 5.5)
        a[m] = C("#e8e6dc"); a[11:14, 4:6] = C("#e8e6dc"); a[13, 3] = C("#e8e6dc")
        a[6, 4:12:3] = C("#3a3440")
    return outline(a)


def stage(w=96, h=24):
    """A grass platform (front edge of dirt) the hero stands on."""
    a = blank(w, h)
    g = np.tile(grass(5, flowers=True), (1, w // 32 + 1, 1))[:, :w]
    a[0:10] = g[0:10]
    a[10:h] = C("#8a6a44")
    a[10, :] = C("#4e8a3c")
    for y in range(12, h, 4):
        a[y, ::5] = shadow_of(C("#8a6a44"))
    return a


ICONS = ("sword", "dagger", "shield", "buckler", "bow", "staff", "speech")


def fade(w=256):
    """A horizontal black-to-clear ramp (title screen: darkens the left side so the text reads)."""
    a = np.zeros((4, w, 4), np.uint8)
    t = np.linspace(0, 1, w)
    alpha = np.clip(1.0 - (t - 0.45) / 0.55, 0, 1) ** 1.6
    a[..., 3] = (alpha * 255).astype(np.uint8)
    a[..., 0:3] = (6, 6, 12)
    return a


# --- minimap: a clean map image + a frame (and its mask) per class ------------------------------------

MINI_TILE = 8   # px per map tile in MAP_Mini


def minimap_image(rows, houses, pad):
    """The map as a readable minimap: flat colours per tile, trees as dark dots, houses as red roofs."""
    H, W = len(rows), len(rows[0])
    T = MINI_TILE
    img = np.zeros(((H + 2 * pad) * T, (W + 2 * pad) * T, 4), np.uint8)
    cols = {".": C("#5f9e47"), ",": C("#c9a46c"), "r": C("#8f8a80"), "~": C("#3a78b8"), "=": C("#a0743e"),
            "#": C("#5a5650"), "T": C("#5f9e47"), "H": C("#5f9e47"), "F": C("#2f5a2c")}
    r = np.random.default_rng(3)
    for ty in range(-pad, H + pad):
        for tx in range(-pad, W + pad):
            inside = 0 <= tx < W and 0 <= ty < H
            c = rows[ty][tx] if inside else "F"
            border = inside and (tx in (0, W - 1) or ty in (0, H - 1))
            k = "F" if (c == "#" and border) else c
            y0, x0 = (ty + pad) * T, (tx + pad) * T
            img[y0:y0 + T, x0:x0 + T] = cols.get(k, cols["."])
            if k in ("T", "F") and (k == "T" or r.random() < 0.55):   # tree tops
                cy, cx = y0 + T // 2 + r.integers(-1, 2), x0 + T // 2 + r.integers(-1, 2)
                img[cy - 2:cy + 3, cx - 2:cx + 3] = C("#2a5a26") if k == "T" else C("#1f4420")
                img[cy - 2, cx - 1:cx + 1] = C("#4a8a3c")
    for (x, y, w, h) in houses:
        y0, x0 = (y + pad) * T + 1, (x + pad) * T + 1
        img[y0:y0 + h * T - 2, x0:x0 + w * T - 2] = C("#b4503a")
        img[y0:y0 + 2, x0:x0 + w * T - 2] = C("#d8785a")
    return img


def _shape(kind, n):
    Y, X = np.mgrid[0:n, 0:n]
    x, y = (X + 0.5) / n * 2 - 1, (Y + 0.5) / n * 2 - 1      # -1..1
    if kind == "orb" or kind == "coin":
        return np.hypot(x, y) <= 0.86
    if kind == "shield":   # a heater shield: straight top, sides curving in to a point
        w = np.where(y <= 0.05, 0.84, 0.84 * np.clip(1 - (y - 0.05) / 0.8, 0, 1) ** 0.55)
        return (y >= -0.8) & (np.abs(x) <= w)
    if kind == "book":     # an open book seen from above
        return (np.abs(x) <= 0.86) & (np.abs(y) <= 0.74)
    raise ValueError(kind)


def minimap_mask(kind, n=160):
    a = np.zeros((n, n, 4), np.uint8)
    a[_shape(kind, n)] = (255, 255, 255, 255)
    return a


def minimap_frame(kind, n=160):
    """The decorative border around the map (transparent inside)."""
    inner = _shape(kind, n)
    # a band just outside the shape
    grow = inner.copy()
    for _ in range(9):
        grow = grow | np.roll(grow, 1, 0) | np.roll(grow, -1, 0) | np.roll(grow, 1, 1) | np.roll(grow, -1, 1)
    band = grow & ~inner
    a = np.zeros((n, n, 4), np.uint8)
    Y, X = np.mgrid[0:n, 0:n]
    if kind == "orb":        # violet glass rim with a glint
        a[band] = C("#5a46a8")
        a[band & (Y < n * 0.45)] = C("#8a74e0")
        glint = np.hypot(X - n * 0.3, Y - n * 0.24) < n * 0.06
        a[glint & grow] = C("#f0eaff")
    elif kind == "shield":   # steel rim with rivets
        a[band] = C("#9aa6b4")
        a[band & (Y < n * 0.2)] = C("#c8d2de")
        for (rx, ry) in ((0.11, 0.13), (0.89, 0.13), (0.11, 0.5), (0.89, 0.5), (0.5, 0.95)):
            a[np.hypot(X - rx * n, Y - ry * n) < 3.2] = C("#e8eef6")
    elif kind == "coin":     # gold rim with a milled edge
        a[band] = C("#d8a838")
        milled = np.floor(np.arctan2(Y - n / 2, X - n / 2) * 30 / np.pi).astype(int) % 2 == 0
        a[band & milled] = C("#f0cc60")
    elif kind == "book":     # leather cover, page edges, spine and a ribbon
        a[band] = C("#7a4a2a")
        a[band & (Y > n * 0.85)] = C("#5a3420")
        spine = (np.abs(X - n / 2) < 2) & inner
        a[spine] = (40, 24, 14, 200)
        rib = (np.abs(X - n * 0.62) < 3) & (Y > n * 0.8) & (Y < n * 0.99)
        a[rib] = C("#c03040")
    edge = band & ~(np.roll(band, 1, 0) & np.roll(band, -1, 0) & np.roll(band, 1, 1) & np.roll(band, -1, 1))
    a[edge & (a[..., 3] > 0)] = (34, 26, 44, 255)
    return a


MINIMAP_SHAPES = ("orb", "shield", "coin", "book")


def vignette(n=256):
    """A radial ramp: clear in the middle, opaque at the edges (low-health warning, tinted red in game)."""
    Y, X = np.mgrid[0:n, 0:n]
    d = np.hypot((X + 0.5) / n * 2 - 1, (Y + 0.5) / n * 2 - 1) / np.sqrt(2)
    a = np.zeros((n, n, 4), np.uint8)
    a[..., 0:3] = 255
    a[..., 3] = (np.clip((d - 0.38) / 0.62, 0, 1) ** 1.5 * 255).astype(np.uint8)
    return a


def fade_up(h=256):
    """A vertical ramp: dark at the bottom, clear by about halfway up (behind the dialogue box)."""
    a = np.zeros((h, 4, 4), np.uint8)
    t = np.linspace(0, 1, h)[:, None]          # 0 at the top, 1 at the bottom
    alpha = np.clip((t - 0.35) / 0.65, 0, 1) ** 1.4
    a[..., 3] = (alpha * 255).astype(np.uint8)
    a[..., 0:3] = (6, 6, 12)
    return a


def fade_radial(n=256, box_w=2.6, box_h=4.2):
    """Dark in a rectangle matching the dialogue box (drawn box_w x box_h times the box's size), fading to clear
    toward the edges, with softly rounded corners."""
    Y, X = np.mgrid[0:n, 0:n]
    x, y = np.abs((X + 0.5) / n * 2 - 1), np.abs((Y + 0.5) / n * 2 - 1)
    a, b = 1.1 / box_w, 1.2 / box_h               # the box (plus a little margin) stays fully dark
    tx = np.clip((x - a) / (1 - a), 0, 1)
    ty = np.clip((y - b) / (1 - b), 0, 1)
    t = np.clip(np.sqrt(tx ** 2 + ty ** 2), 0, 1)  # rectangular falloff; corners just rounded
    alpha = (1 - t * t * (3 - 2 * t)) * 0.85
    out = np.zeros((n, n, 4), np.uint8)
    out[..., 0:3] = (6, 6, 12)
    out[..., 3] = (alpha * 255).astype(np.uint8)
    return out
