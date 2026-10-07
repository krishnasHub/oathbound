"""
Pixel art for cursors, the character-select backdrops and the ambient wildlife.

  CUR_<name>      40x40 cursors (hotspot top-left, except talk: centre)
  BD_<class>      240x200 character-select backdrops; the floor line is at y = 160
  FX_*            effects the backdrops animate: petal, ray, coin, bolt, runes, page, spark
  SPR_Ninja       1 x 5 frames (64 x 64): 4 run + 1 leap  (a dark, masked humanoid)
  SPR_Chest       1 x 3 frames (64 x 48): closed, cracking, open with gold
  SPR_Book        1 x 4 frames (64 x 40): closed, opening, open, glowing
  SPR_Bird        1 x 4 frames (32 x 32): sit, peck, wings up, wings down
  SPR_Goose       1 x 4 frames (48 x 48): walk, walk, honk, graze
  SPR_Ghost       1 x 4 frames (64 x 64): a puff of ghostly gas with two big eyes, its wavy tail swaying
                  (drawn opaque; the game shows it see-through)
  SPR_Sneak       the standard 4 x 13 character sheet: a hooded night prowler
"""
import math

import numpy as np

import characters as ch
from characters import C, mix, shadow_of, light_of
from environment import blank, ellipse_mask, outline
from tspixel.canvas import disc, line, poly_mask, vgrad, noise_band  # noqa: F401

OUT_LINE = (34, 26, 44, 255)


# --- cursors ----------------------------------------------------------------------------------------

def _poly_mask(pts, n=40):
    return poly_mask(pts, n)


def cur_pointer():
    a = blank(40, 40)
    m = _poly_mask([(2, 2), (2, 29), (9, 23), (14, 34), (19, 32), (14, 21), (24, 21)])
    a[m] = (250, 248, 240, 255)
    a[m & ~np.roll(m, -2, 1)] = (200, 196, 186, 255)
    return outline(outline(a, OUT_LINE), (0, 0, 0, 80))


def cur_blade(length, guard_col="#e0c050", blade="#dfe6ee"):
    """A blade pointing up-left (tip = hotspot)."""
    a = blank(40, 40)
    tip = 3
    end = tip + length
    for i in range(tip, end):
        for w in (-1, 0, 1):
            x, y = i + w, i - w
            if 0 <= x < 40 and 0 <= y < 40:
                a[y, x] = C(blade) if w <= 0 else C("#9aa6b4")
    a[tip, tip] = C("#ffffff")
    # guard across the blade
    g = end
    for k in range(-5, 6):
        x, y = g + k, g - k
        if 0 <= x < 40 and 0 <= y < 40:
            a[y, x] = C(guard_col)
            if 0 <= y + 1 < 40: a[y + 1, x] = shadow_of(C(guard_col))
    # grip + pommel
    for i in range(g + 1, min(g + 8, 37)):
        a[i, i] = C("#6a4428"); a[i, i - 1] = C("#5a3420")
    disc(a, min(g + 8, 36), min(g + 8, 36), 1.8, C(guard_col))
    return outline(outline(a, OUT_LINE), (0, 0, 0, 80))


def cur_sword():
    return cur_blade(19)


def cur_dagger():
    return cur_blade(11, guard_col="#c8a060", blade="#e8eef6")


def cur_wand():
    a = blank(40, 40)
    for i in range(9, 34):
        a[i, i] = C("#7a4a2a"); a[i, i + 1] = C("#5a3420")
    # star at the tip
    cx, cy = 6, 6
    star = C("#fff4a0")
    for k in range(-5, 6):
        for (x, y) in ((cx + k, cy), (cx, cy + k)):
            if 0 <= x < 40 and 0 <= y < 40: a[y, x] = star
    for k in range(-3, 4):
        for (x, y) in ((cx + k, cy + k), (cx + k, cy - k)):
            if 0 <= x < 40 and 0 <= y < 40: a[y, x] = C("#ffe060")
    a[cy, cx] = (255, 255, 255, 255)
    for (x, y) in ((12, 3), (3, 13), (14, 10)):
        a[y, x] = C("#c0b0ff")
    return outline(outline(a, OUT_LINE), (0, 0, 0, 80))


def cur_arrow():
    a = blank(40, 40)
    for i in range(6, 33):
        a[i, i] = C("#8a5a30"); a[i, i + 1] = C("#6a4020")
    head = _poly_mask([(1, 1), (13, 5), (8, 8), (5, 13)])
    a[head] = C("#d4dce6"); a[1:3, 1:3] = C("#ffffff")
    for k in range(0, 7):   # fletching
        for (x, y) in ((30 - k + 3, 30 + k - 2), (30 + k - 2, 30 - k + 3)):
            if 0 <= x < 40 and 0 <= y < 40: a[y, x] = C("#e05a40")
    return outline(outline(a, OUT_LINE), (0, 0, 0, 80))


def cur_talk(enabled=True):
    a = blank(40, 40)
    m = ellipse_mask(40, 40, 20, 16, 16, 12)
    tail = _poly_mask([(10, 24), (8, 35), (19, 26)])
    body = C("#f4f1e6") if enabled else C("#8a8a8e")
    a[m | tail] = body
    dot = C("#3a3440") if enabled else C("#5a5a60")
    for x in (12, 20, 28):
        disc(a, x, 16, 2.2, dot)
    a = outline(outline(a, OUT_LINE), (0, 0, 0, 80))
    if not enabled:
        line(a, 6, 34, 34, 4, C("#c03030"), 3)
        a[..., 3] = (a[..., 3] * 0.75).astype(np.uint8)
    return a


CURSORS = {"pointer": cur_pointer, "sword": cur_sword, "dagger": cur_dagger, "wand": cur_wand, "arrow": cur_arrow,
           "talk": lambda: cur_talk(True), "talk_off": lambda: cur_talk(False)}


# --- backdrops -------------------------------------------------------------------------------------

W, H, FLOOR = 240, 200, 160


def bd_knight():
    a = blank(W, H)
    vgrad(a, 0, 120, "#2a3f7a", "#f6c79a")          # dawn sky
    r = np.random.default_rng(1)
    disc(a, 52, 112, 22, mix(C("#ffe6b0"), C("#f6c79a"), 0.3))   # low sun glow
    disc(a, 52, 112, 14, C("#fff3d0"))
    for _ in range(5):                                # soft clouds
        cx, cy = r.uniform(0, W), r.uniform(20, 70)
        for k in range(4):
            a[ellipse_mask(W, H, cx + k * 9, cy + r.uniform(-2, 2), 10, 3.5)] = mix(C("#f3d8c4"), C("#8a90b8"), cy / 90)
    noise_band(a, 108, H, "#6a7aa8", 2, jag=8)      # far hills
    noise_band(a, 124, H, "#4f7a5a", 3, jag=6)      # near hills
    # castle
    stone, dark = C("#c9c4bc"), C("#8f8a84")
    cx = 150
    a[70:142, cx - 46:cx + 46] = stone                # curtain wall
    for x in range(cx - 46, cx + 46, 8):              # crenellations
        a[66:70, x:x + 5] = stone
    for tx in (cx - 46, cx + 38):                     # side towers
        a[50:142, tx - 6:tx + 14] = stone
        a[50:142, tx + 10:tx + 14] = dark
        for y in range(26, 50):
            half = (y - 26) * 0.5 + 1
            a[y, int(tx + 4 - half):int(tx + 4 + half) + 1] = C("#b4483a")
        a[14:26, tx + 4] = C("#5a3a22")
        a[14:19, tx + 5:tx + 12] = C("#d03838")       # pennant
    a[34:142, cx - 14:cx + 14] = stone                # keep
    a[34:142, cx + 9:cx + 14] = dark
    for y in range(10, 34):
        half = (y - 10) * 0.75 + 1
        a[y, int(cx - half):int(cx + half) + 1] = C("#c05040")
    a[0:10, cx] = C("#5a3a22"); a[0:6, cx + 1:cx + 12] = C("#e8c040")   # royal banner
    for (wx, wy) in ((cx - 6, 50), (cx + 3, 50), (cx - 6, 72), (cx + 3, 72), (cx - 40, 92), (cx + 34, 92)):
        a[wy:wy + 8, wx:wx + 4] = C("#3a3448")
    a[112:142, cx - 10:cx + 10] = C("#4a3424")        # gate
    a[ellipse_mask(W, H, cx, 112, 10, 8) & (np.mgrid[0:H, 0:W][0] < 112)] = C("#4a3424")
    a[106:112, cx - 46:cx - 14] = dark; a[106:112, cx + 14:cx + 46] = dark
    # meadow + floor
    vgrad(a, 140, H, "#5f9e47", "#3f6e36")
    for _ in range(140):
        x, y = int(r.uniform(0, W)), int(r.uniform(142, H))
        a[y, x] = r.choice([C("#f4b8c8"), C("#ffffff"), C("#f4e070"), C("#4e8a3c")])
    return a


def bd_thief():
    a = blank(W, H)
    vgrad(a, 0, FLOOR, "#05070f", "#1c1838")
    r = np.random.default_rng(4)
    for _ in range(120):
        x, y = int(r.uniform(0, W)), int(r.uniform(0, 110))
        a[y, x] = mix(C("#ffffff"), C("#8090c0"), r.uniform(0, 0.6))
    disc(a, 190, 40, 20, C("#e6e4d4"))                # moon
    for (mx, my, mr) in ((183, 35, 4), (196, 48, 3), (193, 33, 2)):
        disc(a, mx, my, mr, C("#c8c6b8"))
    a[ellipse_mask(W, H, 190, 40, 26, 26) & ~ellipse_mask(W, H, 190, 40, 20, 20)] = (230, 228, 212, 30)
    # far roofs
    x = 0
    while x < W:
        w = int(r.uniform(18, 34)); h = int(r.uniform(18, 40))
        top = 120 - h
        a[top:FLOOR, x:x + w] = C("#141a30")
        for y in range(top - 8, top):
            half = (y - (top - 8)) * w / 16
            a[y, max(0, int(x + w / 2 - half)):int(x + w / 2 + half)] = C("#141a30")
        if r.uniform() < 0.6:
            wx = x + int(r.uniform(3, w - 6)); a[top + 8:top + 12, wx:wx + 3] = C("#f0c050")
        x += w + int(r.uniform(-2, 3))
    # near roofline the ninjas run along (y = 112)
    a[112:FLOOR, :] = C("#0a0d18")
    for x in range(0, W, 6):
        a[110:112, x:x + 4] = C("#0e1222")
    for cx in (40, 120, 205):
        a[96:112, cx:cx + 8] = C("#0a0d18"); a[94:96, cx - 1:cx + 9] = C("#121628")
    # floor: cobbles in moonlight
    vgrad(a, FLOOR - 20, H, "#1a1f30", "#0c0f18")
    for y in range(FLOOR - 18, H, 5):
        off = 3 if (y // 5) % 2 else 0
        for x in range(off, W, 7):
            a[y, x:x + 5] = C("#222838")
    return a


def bd_mage():
    """A fiery sunset storm over a plain, mountains behind, and (an easter egg) a black four-horned tower in a ring
    of stone on the right - a nod to a certain wizard's tower."""
    a = blank(W, H)
    vgrad(a, 0, 50, "#141c4a", "#4a2a7a")
    vgrad(a, 50, 96, "#4a2a7a", "#c04a6a")
    vgrad(a, 96, 128, "#c04a6a", "#f8a050")            # glowing horizon
    r = np.random.default_rng(6)
    for i in range(8):                                  # storm clouds, lit orange from below
        y = 8 + i * 10
        for k in range(14):
            cx, cyy = r.uniform(-10, W + 10), y + r.uniform(-3, 3)
            rx, ry = r.uniform(12, 26), r.uniform(4, 7)
            base = mix(C("#2a1a4a"), C("#7a3a6a"), i / 8)
            a[ellipse_mask(W, H, cx, cyy, rx, ry)] = base
            a[ellipse_mask(W, H, cx, cyy + ry * 0.55, rx * 0.8, ry * 0.35)] = mix(base, C("#f8904a"), 0.35 + i * 0.04)
    disc(a, 96, 118, 9, C("#ffe0a0"))                   # the sinking sun
    noise_band(a, 104, H, "#5a4a7a", 7, jag=16, step=4)  # far mountains
    noise_band(a, 120, H, "#3a3058", 9, jag=8, step=3)
    vgrad(a, 132, H, "#4a6a3a", "#2a3a22")              # the plain
    for _ in range(90):
        x, y = int(r.uniform(0, W)), int(r.uniform(134, H))
        a[y, x] = r.choice([C("#6a8a4a"), C("#3a4a2a"), C("#8a8a4a")])
    # The ring of stone round the tower.
    cx = 196
    a[ellipse_mask(W, H, cx, 131, 34, 6) & ~ellipse_mask(W, H, cx, 131, 30, 4)] = C("#4a4650")
    a[126:131, cx - 34:cx + 35] = np.where(ellipse_mask(W, H, cx, 131, 34, 6)[126:131, cx - 34:cx + 35, None], C("#4a4650"), a[126:131, cx - 34:cx + 35])
    # The tower: black, faceted, tapering, crowned with four horns.
    black, edge = C("#141218"), C("#3a3644")
    for y in range(30, 130):
        half = 5 + (y - 30) * 0.06
        a[y, int(cx - half):int(cx + half) + 1] = black
        a[y, int(cx - half)] = edge
        a[y, int(cx - half * 0.3)] = edge
        a[y, int(cx + half * 0.4)] = edge
    a[127:131, cx - 9:cx + 10] = black                 # the base flares
    for hx, top in ((-5, 14), (-2, 20), (2, 20), (5, 14)):   # four horns
        for y in range(top, 31):
            w = max(0, int((y - top) / 8))
            a[y, cx + hx - w:cx + hx + w + 1] = black
    a[30:33, cx - 6:cx + 7] = edge                      # the platform between the horns
    a[46:49, cx - 1:cx + 1] = C("#f0a040")              # a single lit window
    return a


def bd_scholar():
    a = blank(W, H)
    a[:, :] = C("#3a2418")                            # wall
    r = np.random.default_rng(8)
    # arched window (left) with sky
    wx0, wx1 = 14, 58
    a[24:110, wx0:wx1] = C("#9ac0e0")
    a[ellipse_mask(W, H, (wx0 + wx1) / 2, 24, (wx1 - wx0) / 2, 18) & (np.mgrid[0:H, 0:W][0] < 24)] = C("#b8d4ec")
    a[24:110, (wx0 + wx1) // 2 - 1:(wx0 + wx1) // 2 + 1] = C("#4a3020")
    a[64:66, wx0:wx1] = C("#4a3020")
    # shelves (centre and right)
    for sy in range(18, FLOOR - 10, 26):
        a[sy + 22:sy + 26, 70:W] = C("#6a4428")       # shelf board
        x = 72
        while x < W - 4:
            bw = int(r.uniform(3, 6)); bh = int(r.uniform(14, 21))
            col = r.choice(["#8a2a2a", "#2a4a8a", "#2a6a3a", "#8a6a2a", "#5a2a6a", "#a07a3a", "#3a3a3a"])
            a[sy + 22 - bh:sy + 22, x:x + bw] = C(col)
            a[sy + 22 - bh + 2, x:x + bw] = light_of(C(col))
            x += bw + (1 if r.uniform() < 0.3 else 0)
    a[0:FLOOR, 66:70] = C("#5a3420")
    # floor boards
    vgrad(a, FLOOR - 4, H, "#6a4428", "#4a2c18")
    for y in range(FLOOR, H, 6):
        a[y, :] = C("#3a2214")
    # lectern (right of the hero; the book sits on top at y ~ 128)
    a[130:FLOOR, 160:168] = C("#5a3420")
    a[126:130, 148:180] = C("#6a4428")
    a[FLOOR - 3:FLOOR, 152:176] = C("#4a2c18")
    return a


# --- backdrop effects --------------------------------------------------------------------------------

def fx_petal():
    a = blank(12, 10)
    a[ellipse_mask(12, 10, 6, 5, 5, 3.2)] = C("#ffffff")
    a[ellipse_mask(12, 10, 4.5, 4, 2, 1.2)] = C("#ffe8f0")
    return a


def fx_ray():
    a = blank(32, 128)
    Y, X = np.mgrid[0:128, 0:32]
    half = 2 + Y / 128 * 14
    inside = np.abs(X - 16) <= half
    alpha = (1 - Y / 128) * np.clip(1 - np.abs(X - 16) / half, 0, 1)
    a[inside] = (255, 255, 255, 0)
    a[..., 3] = (np.where(inside, alpha, 0) * 255).astype(np.uint8)
    a[..., 0:3] = 255
    return a


def fx_coin():
    a = blank(12, 12)
    disc(a, 6, 6, 5, C("#e8b830"))
    disc(a, 6, 6, 3, C("#f8d860"))
    a[3:5, 4:6] = C("#ffffff")
    return outline(a, C("#8a5a10"))


def fx_bolt():
    a = blank(48, 160)
    r = np.random.default_rng(11)
    pts = [(24, 0)]
    while pts[-1][1] < 158:
        x, y = pts[-1]
        pts.append((int(np.clip(x + r.uniform(-9, 9), 4, 44)), y + int(r.uniform(8, 16))))
    glow, core = (190, 160, 255, 120), (255, 255, 255, 255)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        line(a, x0, y0, x1, y1, glow, 5)
    for i, ((x0, y0), (x1, y1)) in enumerate(zip(pts, pts[1:])):
        line(a, x0, y0, x1, y1, core, 2)
        if i in (3, 6):   # branches
            bx, by = x1, y1
            for k in range(3):
                nx, ny = bx + int(r.uniform(-12, 12)), by + int(r.uniform(6, 12))
                line(a, bx, by, nx, ny, core, 1); bx, by = nx, ny
    return a


def fx_runes():
    n = 128
    a = blank(n, n)
    Y, X = np.mgrid[0:n, 0:n]
    d = np.hypot(X - 63.5, Y - 63.5)
    ang = np.arctan2(Y - 63.5, X - 63.5)
    a[(d <= 62) & (d >= 59)] = (255, 255, 255, 255)
    a[(d <= 50) & (d >= 48)] = (255, 255, 255, 230)
    glyph = (d > 51) & (d < 58) & ((np.floor(ang * 24 / np.pi) % 3) == 0) & ((np.floor(d) % 3) != 1)
    a[glyph] = (255, 255, 255, 220)
    for k in range(6):   # hexagram
        t0, t1 = k * math.pi / 3, (k + 2) * math.pi / 3
        line(a, 63.5 + 47 * math.cos(t0), 63.5 + 47 * math.sin(t0), 63.5 + 47 * math.cos(t1), 63.5 + 47 * math.sin(t1), (255, 255, 255, 200), 1)
    return a


def fx_runes_at(offset):
    n = 128
    a = blank(n, n)
    Y, X = np.mgrid[0:n, 0:n]
    d = np.hypot(X - 63.5, Y - 63.5)
    ang = np.arctan2(Y - 63.5, X - 63.5) - offset
    a[(d <= 62) & (d >= 59)] = (255, 255, 255, 255)
    a[(d <= 50) & (d >= 48)] = (255, 255, 255, 230)
    glyph = (d > 51) & (d < 58) & ((np.floor(ang * 24 / np.pi) % 3) == 0) & ((np.floor(d) % 3) != 1)
    a[glyph] = (255, 255, 255, 220)
    for k in range(6):   # hexagram
        t0, t1 = k * math.pi / 3 + offset, (k + 2) * math.pi / 3 + offset
        line(a, 63.5 + 47 * math.cos(t0), 63.5 + 47 * math.sin(t0), 63.5 + 47 * math.cos(t1), 63.5 + 47 * math.sin(t1), (255, 255, 255, 200), 1)
    return a


def fx_runes_spin(frames=12):
    """The rune circle turning through one 60-degree step of its six-fold pattern (a seamless loop), for drawing
    flat on the ground: Slate can't rotate a squashed image without tilting it, so the spin is in the frames."""
    return np.concatenate([fx_runes_at(f / frames * math.pi / 3) for f in range(frames)], axis=1)


def fx_page():
    a = blank(16, 20)
    a[1:19, 2:15] = C("#f4ecd8")
    a[1:19, 2] = C("#d8cdb0")
    for y in range(4, 17, 3):
        a[y, 4:13] = C("#9a8a70")
    return outline(a, C("#6a5a40"))


def fx_spark():
    a = blank(9, 9)
    a[4, :] = (255, 255, 255, 255); a[:, 4] = (255, 255, 255, 255)
    a[3:6, 3:6] = (255, 255, 255, 255)
    return a


# --- animated props -----------------------------------------------------------------------------------

NINJA = dict(skin="#d8b090", torso="#16161e", sleeve="#16161e", legs="#101016", boots="#0a0a0e", belt="#a02020",
             helmet="hood", hoodColor="#121218", mask="#121218", weapon="dagger", slash="#ff6060")


def spr_ninja():
    frames = [ch.humanoid(NINJA, "side", "walk", f) for f in range(4)] + [ch.humanoid(NINJA, "side", "attack", 2)]
    return np.concatenate(frames, axis=1)


def spr_chest():
    frames = []
    for f in range(3):
        a = blank(64, 48)
        wood, band, gold = C("#8a5a30"), C("#d8b040"), C("#f8d050")
        a[24:44, 10:54] = wood
        a[24:44, 50:54] = shadow_of(wood)
        for x in (10, 30, 50):
            a[24:44, x:x + 3] = band
        if f == 0:
            a[12:24, 10:54] = light_of(wood); a[12:16, 10:54] = wood
            a[20:28, 29:35] = band
        elif f == 1:
            a[8:22, 10:54] = light_of(wood)
            a[22:25, 12:52] = (255, 230, 140, 255)   # glow through the crack
        else:
            a[0:14, 10:54] = shadow_of(wood); a[0:3, 10:54] = wood
            a[18:26, 12:52] = gold
            for x in range(14, 50, 5):
                a[16:19, x:x + 3] = C("#fff0a0")
            a[17, 22] = a[16, 40] = (255, 255, 255, 255)
        frames.append(outline(a))
    return np.concatenate(frames, axis=1)


def spr_book():
    frames = []
    cover, page, edge = C("#7a2a2a"), C("#f4ecd8"), C("#d8cdb0")
    for f in range(4):
        a = blank(64, 40)
        if f == 0:
            a[18:36, 14:50] = cover; a[18:21, 14:50] = light_of(cover); a[33:36, 16:50] = page
            a[25:29, 28:36] = C("#e0c050")
        elif f == 1:
            a[18:36, 14:32] = page; a[4:36, 32:36] = cover   # cover standing up
            a[22:34, 16:30] = edge
        else:
            a[18:36, 6:32] = page; a[18:36, 32:58] = page
            a[18:36, 31:33] = edge
            for y in range(22, 34, 3):
                a[y, 9:28] = C("#9a8a70"); a[y, 36:55] = C("#9a8a70")
            a[36:38, 6:58] = cover
            if f == 3:
                a[14:18, 10:54] = (255, 240, 160, 180)
        frames.append(outline(a))
    return np.concatenate(frames, axis=1)


# --- ambient life -------------------------------------------------------------------------------------

def spr_bird():
    frames = []
    body, wing, beak = C("#7a5a3a"), C("#5a4028"), C("#e0a030")
    for f in range(4):
        a = blank(32, 32)
        if f in (0, 1):   # on the ground (side view, facing right)
            a[ellipse_mask(32, 32, 15, 20, 7, 5)] = body
            hy = 14 if f == 0 else 19
            hx = 21 if f == 0 else 23
            disc(a, hx, hy, 3.5, light_of(body))
            a[hy, hx + 3:hx + 6] = beak
            a[hy - 1, hx + 1] = (20, 16, 20, 255)
            a[ellipse_mask(32, 32, 13, 19, 5, 3)] = wing
            a[18:22, 7:10] = wing                              # tail
            a[25:28, 14] = beak; a[25:28, 17] = beak           # legs
        else:             # flying
            a[ellipse_mask(32, 32, 16, 16, 7, 3.5)] = body
            disc(a, 23, 15, 3, light_of(body)); a[15, 26:29] = beak
            if f == 2:
                for k in range(8): a[15 - k, 10 + k // 2:16 + k // 2] = wing
            else:
                for k in range(7): a[17 + k, 10 + k // 2:16 + k // 2] = wing
        frames.append(outline(a))
    return np.concatenate(frames, axis=1)


def spr_goose():
    frames = []
    white, shade, orange = C("#f4f2ec"), C("#c8c4bc"), C("#f08a20")
    for f in range(4):
        a = blank(48, 48)
        a[ellipse_mask(48, 48, 21, 30, 12, 8)] = white
        a[ellipse_mask(48, 48, 18, 32, 8, 5)] = shade                        # wing
        a[26:30, 8:12] = shade                                               # tail
        if f == 3:     # grazing: neck down
            for k in range(10): a[28 + k // 2, 30 + k:32 + k] = white
            disc(a, 40, 36, 3.5, white); a[37:39, 43:47] = orange; a[35, 41] = (20, 16, 20, 255)
        else:
            neck_top = 8 if f == 2 else 12
            a[neck_top:28, 29:33] = white
            disc(a, 32, neck_top, 4, white)
            if f == 2:   # honk: beak open
                a[neck_top - 2, 35:41] = orange; a[neck_top + 1, 35:40] = orange
            else:
                a[neck_top:neck_top + 2, 35:40] = orange
            a[neck_top - 1, 33] = (20, 16, 20, 255)
        legs = (0, 2) if f == 0 else (2, 0)
        a[38:44, 18 + legs[0]:20 + legs[0]] = orange; a[38:44, 24 + legs[1]:26 + legs[1]] = orange
        a[43:45, 16 + legs[0]:22 + legs[0]] = orange; a[43:45, 22 + legs[1]:28 + legs[1]] = orange
        frames.append(outline(a))
    return np.concatenate(frames, axis=1)


GHOST_LOOKS = ((1, 0), (0.7, -0.7), (0, -1), (0.7, 0.7), (0, 1))   # pupils: right, up-right, up, down-right, down


def spr_ghost(frames=4):
    """A fallen foe's ghost: a round puff of gas with a wavy, swaying tail, two big dark eyes and a little 'o' mouth.
    Frames 0-3 calm; then the scared face (wide white eyes, a big 'O') looking each of GHOST_LOOKS in turn, 4 frames
    each: right, up-right, up, down-right, down (the game mirrors them to look left)."""
    out = []
    n = 64
    Y, X = np.mgrid[0:n, 0:n].astype(float)
    body, shade, glint = C("#eef2ff"), C("#b9c6f2"), C("#ffffff")
    looks = [None] + list(GHOST_LOOKS)
    for look, f in [(l, f) for l in looks for f in range(frames)]:
        scared = look is not None
        a = blank(n, n)
        sway = math.sin(f / frames * 2 * math.pi) * 3.0
        # Head: a dome; below it the body tapers to a tail that sways with the frame.
        head = ((X - 32) / 17) ** 2 + ((Y - 24) / 15) ** 2 <= 1
        t = np.clip((Y - 24) / 30.0, 0, 1)                       # 0 at the head's middle, 1 at the tail tip
        cx = 32 + sway * t * t * 2.2                              # the tail bends with the sway
        half = 17 * (1 - t) ** 0.8 + 2
        wave = 2.2 * np.sin(Y / 3.0 + f * 1.6)                    # wavy edges, like drifting smoke
        tail = (Y >= 24) & (Y <= 56) & (np.abs(X - cx - wave * t) <= half)
        m = head | tail
        a[m] = body
        # Soft shading on the lower right, and a few wisps peeling off the tail.
        a[m & ((X - 32) * 0.4 + (Y - 24) * 0.6 > 9)] = shade
        for k, (wx, wy, r) in enumerate(((22, 50, 2.2), (44, 46, 1.8), (36, 58, 1.6))):
            dy = (f + k) % frames
            disc(a, wx + sway * 0.6, wy - dy * 1.5, r, shade if k % 2 else body)
        if scared:
            # Wide white eyes, pupils hard to the right; a big 'O' of fright.
            for ex in (25, 38):
                ring = ((X - ex) / 5.0) ** 2 + ((Y - 21) / 6.2) ** 2
                a[ring <= 1.25] = C("#1d1a2a")
                a[ring <= 1] = glint
                a[((X - ex - look[0] * 2.4) / 2.3) ** 2 + ((Y - 21 - look[1] * 2.8) / 2.8) ** 2 <= 1] = C("#1d1a2a")
            d = ((X - 31.5) / 4.4) ** 2 + ((Y - 35) / 5.0) ** 2
            a[d <= 1] = C("#3a3456")
            a[d <= 0.35] = C("#6a6290")
        else:
            # Eyes: two tall dark ovals with a glint; a small round mouth.
            for ex in (25, 38):
                eye = ((X - ex) / 3.6) ** 2 + ((Y - 22) / 5.2) ** 2 <= 1
                a[eye] = C("#1d1a2a")
                a[18:20, ex - 2:ex] = glint                        # a highlight up-left
            d = ((X - 31.5) / 3.2) ** 2 + ((Y - 33) / 2.8) ** 2
            a[d <= 1] = C("#3a3456")                               # a little 'o' of surprise
            a[d <= 0.3] = C("#6a6290")
        out.append(outline(a, (120, 132, 190, 255)))
    return np.concatenate(out, axis=1)


SNEAK = dict(skin="#d8b090", torso="#2a2630", sleeve="#221e28", legs="#1e1a22", boots="#141016", belt="#4a3a2a",
             helmet="hood", hoodColor="#26222e", mask="#1e1a24", weapon="dagger", build="thin")
