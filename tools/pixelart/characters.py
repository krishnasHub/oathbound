"""
Procedural pixel-art characters for the 2D look tests.

Every character is a 32x32 chibi drawn from a small spec (outfit, hair/helmet, weapon, shield...),
in three directions (down = facing the camera, up = facing away, side = facing right; left is the
side frames mirrored at runtime) and five actions.

Sheet layout (4 columns x 13 rows of 64x64 frames; drawn in 32-unit coordinates at 2 texels per unit):
    rows 0-3   down:  idle(2)  walk(4)  attack(4)  hurt(1)
    rows 4-7   up:    idle     walk     attack     hurt
    rows 8-11  side:  idle     walk     attack     hurt
    row 12     dead(1), guard down, guard up, guard side (shield raised; same as idle without a shield)
"""
import math

import numpy as np

import tessera_path  # noqa: F401
from tspixel import sheet as layout
from tspixel.color import hex_color as C, mix  # noqa: F401  (C and mix are used across the art modules)

S = 32          # logical frame size: all drawing coordinates are in these units
RES = 2         # output texels per logical unit (64 x 64 frames): smoother curves, finer outlines
N = S * RES     # frame size in texels
DIRS = layout.DIRS
ACTIONS = layout.ACTIONS
OUTLINE = (34, 26, 44, 255)


def shadow_of(c):
    return mix(c, (52, 36, 86, 255), 0.38)     # shadows shift toward purple


def light_of(c):
    return mix(c, (255, 248, 222, 255), 0.32)  # highlights toward warm white


# Texel sample positions in logical units (texel i covers logical pixel i // RES).
Y, X = (np.mgrid[0:N, 0:N] - (RES - 1) / 2) / RES
PORTRAIT = False   # high-resolution portrait mode: painterly shading, real eyes, hair strands, thicker outlines


def use_res(r):
    """Draw at r texels per logical unit from now on (2 = sprites, 16 = portraits)."""
    global RES, N, Y, X, PORTRAIT
    RES, N = r, S * r
    Y, X = (np.mgrid[0:N, 0:N] - (RES - 1) / 2) / RES
    PORTRAIT = r >= 8


def ellipse(cx, cy, rx, ry):
    return ((X - cx) / max(rx, 0.1)) ** 2 + ((Y - cy) / max(ry, 0.1)) ** 2 <= 1.0


def rect(x0, y0, x1, y1):
    """Logical pixels x0..x1, y0..y1 inclusive."""
    return (X >= x0 - 0.5) & (X <= x1 + 0.5) & (Y >= y0 - 0.5) & (Y <= y1 + 0.5)


def trapezoid(x0t, x1t, yt, x0b, x1b, yb):
    t = np.clip((Y - yt) / max(yb - yt, 1), 0, 1)
    left = x0t + (x0b - x0t) * t
    right = x1t + (x1b - x1t) * t
    return (Y >= yt - 0.5) & (Y <= yb + 0.5) & (X >= left - 0.5) & (X <= right + 0.5)


class Frame:
    def __init__(self):
        self.a = np.zeros((N, N, 4), np.uint8)

    def fill(self, mask, col, shade=True):
        """Fill a mask; auto-shade its bottom/right rim darker and top rim lighter."""
        if not mask.any():
            return
        self.a[mask] = col
        if not shade:
            return
        if PORTRAIT:
            # Four bands of light falling from the top-left, then a crisp rim.
            ys, xs = np.nonzero(mask)
            y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
            # Soft light from above: a lit top, the base colour, a gentle shadow toward the bottom.
            t = (Y * RES - y0) / max(1, y1 - y0)
            band = np.where(t < 0.18, 0, np.where(t < 0.72, 1, 2))
            tones = [mix(col, light_of(col), 0.55), col, mix(col, shadow_of(col), 0.4)]
            for b in range(3):
                self.a[mask & (band == b)] = tones[b]
            edge = mask & ~(np.roll(mask, 2, 0) & np.roll(mask, -2, 0) & np.roll(mask, 2, 1) & np.roll(mask, -2, 1))
            self.a[edge] = mix(shadow_of(col), (30, 20, 40, 255), 0.3)
            return
        sh = np.zeros_like(mask)
        for k in range(1, RES + 1):                          # right rim: one logical pixel
            sh |= mask & ~np.roll(mask, -k, axis=1)
        for k in range(1, RES + RES // 2 + 1):               # bottom rim: a pixel and a half
            sh |= mask & ~np.roll(mask, -k, axis=0)
        self.a[sh] = shadow_of(col)
        hl = mask & ~np.roll(mask, 1, axis=0) & ~sh          # top rim: one fine texel
        self.a[hl] = light_of(col)

    def px(self, x, y, col):
        """One logical pixel (a RES x RES block)."""
        x, y = int(x), int(y)
        if 0 <= x < S and 0 <= y < S:
            self.a[y * RES:(y + 1) * RES, x * RES:(x + 1) * RES] = col

    def dot(self, x, y, col):
        """One fine texel at a logical position (details: lashes, highlights, thin lines)."""
        i, j = int(math.floor(y * RES + RES / 2)), int(math.floor(x * RES + RES / 2))
        if 0 <= i < N and 0 <= j < N:
            self.a[i, j] = col

    def line(self, x0, y0, x1, y1, col, width=1):
        """A line in logical coordinates, drawn at texel resolution (width 1 = a thin, crisp line)."""
        thick = max(1, width * RES - 1)
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * RES * 2) + 1
        steep = abs(y1 - y0) > abs(x1 - x0)
        for i in range(n + 1):
            t = i / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            for w in range(thick):
                o = (w - (thick - 1) / 2) / RES
                self.dot(x + (o if steep else 0), y + (0 if steep else o), col)

    def outline(self, col=OUTLINE):
        m = self.a[..., 3] > 0
        d = m
        for _ in range(max(1, RES // 4)):   # thicker at portrait resolution
            d = d | np.roll(d, 1, 0) | np.roll(d, -1, 0) | np.roll(d, 1, 1) | np.roll(d, -1, 1)
        self.a[d & ~m] = col

    def strands(self, mask, col):
        """Portrait hair: fine wavy strands across a hair mask."""
        if not PORTRAIT or not mask.any():
            return
        wave = np.floor((X * RES + np.sin(Y * 1.3) * RES * 0.6) / (RES * 0.9))
        self.a[mask & ((wave % 2) == 0) & ((np.floor(X * RES + np.sin(Y * 1.3) * RES * 0.6) % (RES * 0.9)) < 2)] = shadow_of(col)


def arc(fr, cx, cy, r, a0, a1, col, thick=2):
    """Slash smear: a bright arc from angle a0 to a1 (degrees, screen space: 0 = right, 90 = down)."""
    steps = int(abs(a1 - a0) / 1.5) + 2
    for i in range(steps):
        a = math.radians(a0 + (a1 - a0) * i / (steps - 1))
        for t in range(thick * RES - 1):
            rr = r - t / RES
            fr.dot(cx + math.cos(a) * rr, cy + math.sin(a) * rr, col)


# --- weapons ------------------------------------------------------------------------------------

def weapon(fr, kind, hx, hy, ang, spec, flash=0.0, extend=0):
    a = math.radians(ang)
    dx, dy = math.cos(a), math.sin(a)
    px, py = -dy, dx
    steel, steel_l = C("#c9d3df"), C("#f4f8ff")
    wood = C("#7a4a2a")
    if kind in ("sword", "sabre", "dagger", "greatsword"):
        L = {"sword": 10, "sabre": 9, "dagger": 5, "greatsword": 13}[kind] + extend
        g = C("#d8b44a")
        fr.line(hx - px * 2, hy - py * 2, hx + px * 2, hy + py * 2, g)                 # crossguard
        fr.line(hx - dx * 1.5, hy - dy * 1.5, hx, hy, C("#5a3a22"))                    # grip
        fr.line(hx + dx, hy + dy, hx + dx * L, hy + dy * L, steel, 2 if kind == "greatsword" else 1)
        fr.line(hx + dx + px * 0.6, hy + dy + py * 0.6, hx + dx * (L - 1) + px * 0.6, hy + dy * (L - 1) + py * 0.6, steel_l)
    elif kind == "staff":
        L = 15 + extend
        fr.line(hx - dx * 4, hy - dy * 4, hx + dx * L, hy + dy * L, wood)
        ox, oy = hx + dx * (L + 1), hy + dy * (L + 1)
        orb = C(spec.get("orb", "#b9a4ff"))
        r = 1.6 + flash * 1.2
        m = ellipse(ox, oy, r, r)
        fr.fill(m, orb, shade=False)
        fr.px(round(ox - 0.5), round(oy - 0.5), C("#ffffff"))
    elif kind == "club":
        L = 9 + extend
        fr.line(hx, hy, hx + dx * L, hy + dy * L, wood, 2)
        fr.fill(ellipse(hx + dx * L, hy + dy * L, 2.2, 2.2), C("#8a5a32"))
    elif kind == "cane":
        fr.line(hx, hy - 1, hx + dx * 8, hy + dy * 8, C("#6a4428"))
    elif kind == "bow":
        # A bow held vertically in front of the hand, string on the near side.
        bw = C("#8a5a30")
        for t in range(-6, 7):
            bx = hx + dx * 1.5 + dx * (1 - (t / 6.5) ** 2) * 2.5 + px * t
            by = hy + dy * 1.5 + dy * (1 - (t / 6.5) ** 2) * 2.5 + py * t
            fr.px(round(bx), round(by), bw)
        pull = spec.get("_pull", 0)
        sx, sy = hx - dx * pull, hy - dy * pull
        fr.line(hx + dx * 1.5 + px * 6, hy + dy * 1.5 + py * 6, sx, sy, C("#e8e0d0"))
        fr.line(hx + dx * 1.5 - px * 6, hy + dy * 1.5 - py * 6, sx, sy, C("#e8e0d0"))
        if pull > 0:
            fr.line(sx, sy, hx + dx * 7, hy + dy * 7, C("#d8c8a8"))  # nocked arrow


def shield(fr, kind, cx, cy, spec, side=False):
    if kind == "shield":
        m = ellipse(cx, cy, 2.2 if side else 3.6, 4.2)
        fr.fill(m, C(spec.get("shieldColor", "#3a5a9a")))
        if not side:
            fr.line(cx, cy - 3, cx, cy + 3, C("#e0c050"))
            fr.line(cx - 2, cy - 1, cx + 2, cy - 1, C("#e0c050"))
    elif kind == "buckler":
        m = ellipse(cx, cy, 1.6 if side else 2.8, 2.8)
        fr.fill(m, C("#a07840"))
        fr.px(round(cx), round(cy), C("#e0d090"))


# --- humanoids ----------------------------------------------------------------------------------

ATTACK_ANGLES = {
    # weapon angle per attack frame: windup, swing, strike, recover
    "down": (-125, -35, 60, 100),
    "side": (-115, -55, 5, 55),
    "up": (70, -40, -95, -70),
}
REST_ANGLE = {"down": 110, "side": 75, "up": 75}


def humanoid(spec, d, action, f):
    fr = Frame()
    wide = spec.get("build") == "wide"
    thin = spec.get("build") == "thin"
    skin = C(spec.get("skin", "#f2c6a0"))
    torso_c = C(spec["torso"])
    legs_c = C(spec.get("legs", "#4a3e5a"))
    boots_c = C(spec.get("boots", "#3a2a24"))
    hair_c = C(spec["hair"]) if spec.get("hair") else None

    bob = 0
    lift_l = lift_r = 0
    shift_l = shift_r = 0
    arm_l = arm_r = 0
    if action == "idle":
        bob = (0, 1)[f]
    elif action == "walk":
        bob = (0, -1, 0, -1)[f]
        if d == "side":
            shift_l, shift_r = (0, 2, 0, -2)[f], (0, -2, 0, 2)[f]
            lift_l, lift_r = (0, 1, 0, 0)[f], (0, 0, 0, 1)[f]
        else:
            lift_l, lift_r = (0, 2, 0, 0)[f], (0, 0, 0, 2)[f]
            arm_l, arm_r = (0, 1, 0, -1)[f], (0, -1, 0, 1)[f]
    elif action == "attack":
        bob = (0, 0, 1, 1)[f]
    elif action == "hurt":
        bob = -1
    elif action == "guard":
        bob = 1          # braced, a little lower

    guard = action == "guard" and spec.get("offhand") in ("shield", "buckler")
    # Crouch pose (spec "_crouch": how far the head and shoulders sink; "_wide": knees out; "_lean": forward, side
    # view; "_reach": the weapon hand out in front). All 0 = standing, so ordinary sheets are untouched.
    crouch, wide_k, lean, reach = spec.get("_crouch", 0), spec.get("_wide", 0), spec.get("_lean", 0), spec.get("_reach", 0)
    if d != "side" and "_crouchFront" in spec:
        crouch = spec["_crouchFront"]   # facing the camera / away, sink deeper: the big head hides a shallow crouch
    bob += crouch
    hy = 11 + bob          # head centre
    ty0, ty1 = 17 + bob, 25
    fem = bool(spec.get("fem"))
    tx0, tx1 = (9, 22) if wide else (11, 20) if thin else (11, 20) if fem else (10, 21)
    if d == "side":
        tx0, tx1 = (12, 21) if wide else (13, 19) if thin else (12, 20)
        tx0, tx1 = tx0 + lean // 2, tx1 + lean // 2

    weapon_kind = spec.get("weapon")
    off = spec.get("offhand")
    angle = REST_ANGLE[d]
    extend, flash = 0, 0.0
    if action == "attack" and weapon_kind:
        angle = ATTACK_ANGLES[d][f]
        if weapon_kind == "staff":
            angle = {"down": (-100, -80, 90, 60), "side": (-100, -70, 0, 30), "up": (-80, -95, -90, -80)}[d][f]
            extend = (0, 0, 3, 1)[f]
            flash = (0.2, 0.5, 1.0, 0.4)[f]
        if weapon_kind == "bow":
            angle = {"down": 90, "side": 0, "up": -90}[d]
            spec = dict(spec, _pull=(1, 4, 0, 0)[f])
    elif weapon_kind == "staff":
        angle = -95
    elif weapon_kind == "bow":
        angle = {"down": 100, "side": 70, "up": 80}[d]
    elif weapon_kind == "cane":
        angle = 95

    # Hand position (the weapon hand).
    if d == "down":
        hx, hyy = tx0 - 1, 23 + bob + arm_l
    elif d == "up":
        hx, hyy = tx1 + 1, 22 + bob + arm_r
    else:
        hx, hyy = 17 + lean + reach, 23 + bob - reach // 2
    if d == "down" and reach:
        hyy += reach // 2
    if action == "attack" and weapon_kind not in ("bow",):
        if d == "side":
            hx += (0, 0, 2, 1)[f]
        elif d == "down":
            hyy += (-2, -3, 1, 0)[f]
        else:
            hyy += (1, -3, -4, -2)[f]

    behind_weapon = d == "up"

    # --- back layer
    if d == "up" and spec.get("cape"):
        pass
    if behind_weapon and weapon_kind:
        weapon(fr, weapon_kind, hx, hyy, angle, spec, flash, extend)
    if d == "side" and off and not guard:
        shield(fr, off, tx0 - 1, 21 + bob, spec, side=True)
    if d == "up" and guard:
        shield(fr, off, 15.5, 17 + bob, spec)          # raised in front: its rim shows around the body
    if spec.get("back") == "bow" and d != "up":
        for t in range(-6, 7):
            # (crouched, the bow rides lower on the back, and further back as he leans)
            fr.px(13 + t * 0.6 if d == "down" else 11 + lean // 2 - (2 if crouch else 0), 19 + t + crouch if d == "down" else 18 + t + crouch, C("#8a5a30"))
    long_hair = spec.get("longHair")
    if long_hair and d != "up":
        lh = C(long_hair)
        hc0 = 15.5 if d != "side" else 17
        if d == "side":
            lm = trapezoid(hc0 - 7, hc0 - 3, hy - 3, hc0 - 9, hc0 - 5, hy + 9)   # tail swinging behind
        else:
            lm = trapezoid(hc0 - 8, hc0 - 5, hy - 2, hc0 - 8.5, hc0 - 5.5, hy + 8) | trapezoid(hc0 + 5, hc0 + 8, hy - 2, hc0 + 5.5, hc0 + 8.5, hy + 8)
        fr.fill(lm, lh)
        fr.strands(lm, lh)
    if hair_c is not None and spec.get("hairstyle") in ("long", "ponytail") and d != "up":
        if d == "side":
            fr.fill(rect(9, hy - 2, 13, hy + 7), hair_c)
        else:
            fr.fill(rect(7, hy - 2, 24, hy + 7) & ~rect(10, hy + 2, 21, hy + 8), hair_c)

    # --- legs
    if d == "side" and crouch:
        # Bent legs: the back knee down and behind, the front thigh forward and the shin straight down to the boot.
        lx_back, lx_front = 12 + shift_r - wide_k, 16 + shift_l + wide_k
        fr.fill(rect(lx_back, 25 - lift_r, lx_back + 3, 27 - lift_r), shadow_of(legs_c), shade=False)            # back thigh, kneeling
        fr.fill(rect(lx_back - 2, 27 - lift_r, lx_back + 1, 29 - lift_r), shadow_of(legs_c), shade=False)        # back shin along the ground
        fr.fill(rect(lx_back - 3, 28 - lift_r, lx_back - 1, 29 - lift_r), shadow_of(boots_c), shade=False)
        fr.fill(rect(lx_front - 1, 24 - lift_l, lx_front + 3, 26 - lift_l), legs_c)                               # front thigh, forward
        fr.fill(rect(lx_front + 2, 26 - lift_l, lx_front + 4, 29 - lift_l), legs_c)                               # front shin
        fr.fill(rect(lx_front + 2, 28 - lift_l, lx_front + 5, 29 - lift_l), boots_c)
    elif d == "side":
        lx_back, lx_front = 13 + shift_r, 16 + shift_l
        fr.fill(rect(lx_back, 25 - lift_r, lx_back + 2, 29 - lift_r), shadow_of(legs_c), shade=False)
        fr.fill(rect(lx_back, 28 - lift_r, lx_back + 3, 29 - lift_r), shadow_of(boots_c), shade=False)
        fr.fill(rect(lx_front, 25 - lift_l, lx_front + 2, 29 - lift_l), legs_c)
        fr.fill(rect(lx_front, 28 - lift_l, lx_front + 3, 29 - lift_l), boots_c)
    elif crouch:
        # Facing the camera or away: knees splayed out, feet wide.
        for x0, lift in ((12 - wide_k, lift_l), (17 + wide_k, lift_r)):
            out = -1 if x0 < 16 else 1
            fr.fill(rect(x0 + out, 25 - lift, x0 + 2 + out, 27 - lift), legs_c)    # knee out
            fr.fill(rect(x0, 27 - lift, x0 + 2, 29 - lift), legs_c)
            fr.fill(rect(x0, 28 - lift, x0 + 2, 29 - lift), boots_c, shade=False)
    else:
        fr.fill(rect(12, 25 - lift_l, 14, 29 - lift_l), legs_c)
        fr.fill(rect(17, 25 - lift_r, 19, 29 - lift_r), legs_c)
        fr.fill(rect(12, 28 - lift_l, 14, 29 - lift_l), boots_c, shade=False)
        fr.fill(rect(17, 28 - lift_r, 19, 29 - lift_r), boots_c, shade=False)

    # --- torso / robe
    if spec.get("robe"):
        fr.fill(trapezoid(tx0 + 1, tx1 - 1, ty0, tx0 - 1, tx1 + 1, 28), torso_c)
    else:
        if crouch: ty0 = min(ty0, ty1 - 3)      # a hunched back: never shorter than this
        body = rect(tx0, ty0, tx1, ty1) & ~(rect(tx0, ty0, tx0, ty0) | rect(tx1, ty0, tx1, ty0))
        fr.fill(body, torso_c)
    if spec.get("trim"):
        tc = C(spec["trim"])
        if d == "down":
            fr.fill(rect(14, ty0 + 1, 17, 26 if spec.get("robe") else ty1), tc)
        elif d == "side":
            fr.fill(rect(tx1 - 2, ty0 + 1, tx1 - 1, 26 if spec.get("robe") else ty1), tc)
    if spec.get("belt") and not spec.get("robe"):
        fr.fill(rect(tx0, 22 + (bob > 0), tx1, 22 + (bob > 0)), C(spec["belt"]), shade=False)
    if d == "up" and spec.get("cape"):
        fr.fill(trapezoid(tx0 + 1, tx1 - 1, ty0, tx0 - 1, tx1 + 1, 27), C(spec["cape"]))

    # --- arms
    sleeve = C(spec.get("sleeve", spec["torso"]))
    if d == "side":
        ax = 15 + (shift_l // 2 if action == "walk" else 0) + lean
        if reach:
            # The dagger arm out in front, low: shoulder to hand.
            fr.fill(rect(ax, ty0 + 1, ax + 2 + reach, ty0 + 3), sleeve)
            fr.fill(rect(ax + 2 + reach, ty0 + 2, ax + 3 + reach, ty0 + 3), skin)
        else:
            fr.fill(rect(ax, ty0 + 1, ax + 2, min(22 + bob, 27)), sleeve)
            fr.fill(rect(ax, min(23 + bob, 27), ax + 2, min(24 + bob, 28)), skin)
    else:
        fr.fill(rect(tx0 - 2, ty0 + 1, tx0 - 1, 22 + bob + arm_l), sleeve)
        fr.fill(rect(tx1 + 1, ty0 + 1, tx1 + 2, 22 + bob + arm_r), sleeve)
        fr.fill(rect(tx0 - 2, 23 + bob + arm_l, tx0 - 1, 24 + bob + arm_l), skin, shade=False)
        fr.fill(rect(tx1 + 1, 23 + bob + arm_r, tx1 + 2, 24 + bob + arm_r), skin, shade=False)

    # --- head
    hcx = 15.5 if d != "side" else 17 + lean
    head = ellipse(hcx, hy, 7.2 if d != "side" else 6.6, 6.6)
    fr.fill(head, skin)
    helmet = spec.get("helmet")
    if helmet == "knight":
        steel = C("#b8c4d2")
        cover = head & ((Y <= hy - 1) | (X <= hcx - 5) | (X >= hcx + 5)) if d == "down" else head & ((Y <= hy - 1) | (X <= hcx - 3)) if d == "side" else head
        fr.fill(cover, steel)
        plume = C(spec.get("plume", "#d04040"))
        fr.fill(ellipse(hcx + (-1 if d == "side" else 0), hy - 7.5, 2, 2.2), plume)
        fr.fill(rect(int(hcx) - 1, int(hy - 9), int(hcx) + 1, int(hy - 6)), plume)
    elif helmet == "hood":
        hc = C(spec.get("hoodColor", spec["torso"]))
        cover = head & ((Y <= hy - 2) | (X <= hcx - 5) | (X >= hcx + 5)) if d == "down" else head & ((Y <= hy - 2) | (X <= hcx - 2)) if d == "side" else head
        fr.fill(cover | (ellipse(hcx, hy - 1, 8, 7.5) & ~head & (Y <= hy + 5)), hc)
        if d == "down" and spec.get("mask"):
            fr.fill(rect(int(hcx) - 4, int(hy) + 3, int(hcx) + 4, int(hy) + 5), C(spec["mask"]), shade=False)
    elif helmet == "wizard":
        hc = C(spec.get("hatColor", "#4b3a8a"))
        brim = ellipse(hcx, hy - 4, 9.5, 2.2)
        cone = trapezoid(hcx - 5, hcx + 5, hy - 6, hcx - 6, hcx + 6, hy - 5) | trapezoid(hcx - 1, hcx + 1, 0, hcx - 5, hcx + 5, hy - 6)
        fr.fill(head & (Y <= hy - 3), hair_c or hc)
        fr.fill(cone, hc)
        fr.fill(brim, hc)
        fr.fill(rect(int(hcx) - 5, int(hy - 6), int(hcx) + 5, int(hy - 5)), C(spec.get("hatBand", "#e0c050")), shade=False)
    elif helmet == "bandana":
        if hair_c is not None:
            fr.fill(head & ((Y <= hy - 3) | ((X <= hcx - 6) & (Y <= hy + 1)) | ((X >= hcx + 6) & (Y <= hy + 1))), hair_c)
        fr.fill(head & (Y >= hy - 5) & (Y <= hy - 3), C(spec.get("bandanaColor", "#c03030")))
        if d != "down":
            fr.fill(rect(int(hcx) - 7, int(hy - 4), int(hcx) - 5, int(hy) - 1), C(spec.get("bandanaColor", "#c03030")))
    elif helmet == "cap":
        fr.fill(head & (Y <= hy - 3), C(spec.get("capColor", "#6a5a3a")))
        fr.fill(rect(int(hcx) - 8 + (2 if d == "side" else 0), int(hy - 3), int(hcx) + 8, int(hy - 3)), C(spec.get("capColor", "#6a5a3a")))
    elif hair_c is not None:
        style = spec.get("hairstyle", "short")
        if d == "down":
            cap = head & ((Y <= hy - 3) | ((X <= hcx - 5.5) & (Y <= hy + 2)) | ((X >= hcx + 5.5) & (Y <= hy + 2)))
            fringe = head & (np.floor(Y + 0.5) == hy - 2) & (((np.floor(X + 0.5).astype(int) + 1) % 3) != 0)
            fr.fill(cap | fringe, hair_c)
            fr.strands(cap | fringe, hair_c)
        elif d == "side":
            fr.fill(head & ((Y <= hy - 3) | (X <= hcx - 2)), hair_c)
        else:
            fr.fill(head, hair_c)
        if style == "bun":
            fr.fill(ellipse(hcx - (2 if d == "side" else 0), hy - 7, 2.6, 2.4), hair_c)
        if style == "ponytail" and d != "down":
            fr.fill(rect(int(hcx) - 8, int(hy) - 1, int(hcx) - 6, int(hy) + 6) if d == "side" else rect(14, int(hy) + 4, 17, int(hy) + 9), hair_c)
    if spec.get("beard") and d != "up":
        bc = C(spec["beard"])
        if d == "down":
            fr.fill(head & (Y >= hy + 3) & (X >= hcx - 5) & (X <= hcx + 5), bc)
        else:
            fr.fill(head & (Y >= hy + 3) & (X >= hcx - 1), bc)

    if long_hair and d == "up":
        fr.fill(trapezoid(hcx - 5, hcx + 5, hy + 2, hcx - 4, hcx + 4, hy + 11) & ~ellipse(hcx, hy + 13, 2.5, 2.5), C(long_hair))

    # --- face
    eye = C(spec.get("eye", "#2a1e2e"))
    if d == "down":
        if spec.get("skull"):
            fr.fill(rect(int(hcx) - 4, int(hy), int(hcx) - 2, int(hy) + 2), eye, shade=False)
            fr.fill(rect(int(hcx) + 2, int(hy), int(hcx) + 4, int(hy) + 2), eye, shade=False)
            fr.fill(rect(int(hcx) - 2, int(hy) + 4, int(hcx) + 2, int(hy) + 4), eye, shade=False)
        elif action == "hurt":
            for ex in (int(hcx) - 3, int(hcx) + 3):
                fr.px(ex - 1, hy, eye); fr.px(ex, hy + 1, eye); fr.px(ex + 1, hy, eye)
        elif PORTRAIT:
            portrait_face(fr, spec, hcx, hy, skin, eye, fem, helmet)
        else:
            for side, ex in ((-1, int(hcx) - 3), (1, int(hcx) + 3)):
                fr.px(ex, hy, eye); fr.px(ex, hy + 1, eye)
                fr.dot(ex + 0.25, hy - 0.25, C("#ffffff"))                        # catch-light
                if fem:
                    fr.dot(ex + (0.75 if side > 0 else -0.25), hy - 0.75, eye)     # lashes, flicked outward
                    fr.dot(ex + (1.25 if side > 0 else -0.75), hy - 0.75, eye)
                elif helmet not in ("hood",):
                    fr.line(ex - 0.6, hy - 1.4, ex + 0.9, hy - 1.4 - side * 0.3, C(spec.get("brow", "#4a3020")))   # brows
            if not helmet == "hood" or not spec.get("mask"):
                fr.px(int(hcx) - 4, hy + 3, C("#f08a8a")); fr.px(int(hcx) + 4, hy + 3, C("#f08a8a"))
            if fem:
                fr.dot(hcx - 0.25, hy + 3.75, C("#d0506a")); fr.dot(hcx + 0.25, hy + 3.75, C("#d0506a"))   # lips
            elif spec.get("stubble"):
                for k in range(-4, 5, 2):
                    fr.dot(hcx + k * 0.5, hy + 4.25 + (abs(k) % 4) * 0.25, mix(skin, (60, 40, 30, 255), 0.45))
        if spec.get("glasses") and not PORTRAIT:
            fr.line(int(hcx) - 5, hy, int(hcx) + 5, hy, C("#c0a060"))
    elif d == "side":
        ex = int(hcx) + 3
        if spec.get("skull"):
            fr.fill(rect(ex - 1, int(hy), ex + 1, int(hy) + 2), eye, shade=False)
        elif action == "hurt":
            fr.px(ex - 1, hy, eye); fr.px(ex, hy + 1, eye)
        else:
            fr.px(ex, hy, eye); fr.px(ex, hy + 1, eye)
            fr.dot(ex + 0.25, hy - 0.25, C("#ffffff"))
            if fem:
                fr.dot(ex + 0.75, hy - 0.75, eye); fr.dot(ex + 1.25, hy - 0.25, eye)
                fr.dot(ex + 2.25, hy + 3.75, C("#d0506a"))
            elif helmet != "hood":
                fr.line(ex - 0.6, hy - 1.4, ex + 0.9, hy - 1.4, C(spec.get("brow", "#4a3020")))
            if not (helmet == "hood" and spec.get("mask")):
                fr.px(ex - 1, hy + 3, C("#f08a8a"))

    # --- front layer: weapon + shield
    if d == "down" and off and not guard:
        shield(fr, off, tx1 + 2, 21 + bob, spec)
    if not behind_weapon and weapon_kind:
        weapon(fr, weapon_kind, hx, hyy, angle, spec, flash, extend)
    if guard and d == "down":
        shield(fr, off, 16, 20 + bob, spec)            # raised across the body
    if guard and d == "side":
        shield(fr, off, tx1 + 1, 20 + bob, spec)

    fr.outline()

    # --- FX on top of the outline
    if action == "attack" and weapon_kind and weapon_kind not in ("bow", "staff", "cane"):
        fx = C(spec.get("slash", "#ffffff"))
        fx2 = mix(fx, (255, 255, 255, 255), 0.0)
        if f == 2:
            if d == "down":
                arc(fr, 15, 19, 13, -30, 85, fx, 2)
            elif d == "side":
                arc(fr, 16, 19, 13, -70, 35, fx, 2)
            else:
                arc(fr, 16, 15, 12, -165, -15, fx, 2)
        elif f == 3:
            fx3 = fx2[:3] + (150,)
            if d == "down":
                arc(fr, 15, 19, 13, 40, 85, fx3, 1)
            elif d == "side":
                arc(fr, 16, 19, 13, 0, 35, fx3, 1)
            else:
                arc(fr, 16, 15, 12, -60, -15, fx3, 1)
    if weapon_kind == "staff" and action == "attack" and f == 2:
        a = math.radians(angle)
        ox, oy = hx + math.cos(a) * 19, hyy + math.sin(a) * 19
        for k in range(6):
            t = math.radians(k * 60)
            fr.px(round(ox + math.cos(t) * 3), round(oy + math.sin(t) * 3), C(spec.get("orb", "#b9a4ff")))
    return fr.a


# --- slime --------------------------------------------------------------------------------------

def slime(spec, d, action, f):
    fr = Frame()
    body = C(spec.get("torso", "#6cc24a"))
    sx, sy, oy = 1.0, 1.0, 0
    lunge = 0
    if action == "idle":
        sx, sy = ((1.0, 1.0), (1.08, 0.92))[f]
    elif action == "walk":
        sx, sy, oy = ((1.12, 0.86, 0), (0.9, 1.15, -2), (0.95, 1.05, -4), (1.15, 0.85, 0))[f]
    elif action == "attack":
        sx, sy, oy = ((1.25, 0.75, 0), (0.85, 1.25, -2), (1.2, 0.95, 0), (1.05, 0.95, 0))[f]
        lunge = (0, 2, 4, 1)[f]
    elif action == "hurt":
        sx, sy = 1.18, 0.8
    dx = lunge if d == "side" else 0
    dy = lunge if d == "down" else -lunge if d == "up" else 0
    rx, ry = 9.5 * sx, 8 * sy
    cy = 28 - ry + oy + dy
    m = ellipse(16 + dx, cy, rx, ry) & (Y <= 28 + oy + dy)
    fr.fill(m, body)
    fr.fill(ellipse(16 + dx - rx * 0.35, cy - ry * 0.45, 2, 1.4), C("#e8ffd8"), shade=False)
    eye = C("#1e2a1e")
    if d == "down":
        for ex in (-3, 3):
            if action == "hurt":
                fr.px(16 + dx + ex - 1, cy, eye); fr.px(16 + dx + ex, cy + 1, eye); fr.px(16 + dx + ex + 1, cy, eye)
            else:
                fr.fill(rect(16 + dx + ex - 0.5, cy - 1, 16 + dx + ex + 0.5, cy + 1), eye, shade=False)
    elif d == "side":
        fr.fill(rect(16 + dx + rx * 0.45, cy - 1, 16 + dx + rx * 0.45 + 1, cy + 1), eye, shade=False)
    fr.outline()
    return fr.a


# --- sheets -------------------------------------------------------------------------------------

def sheet(spec):
    draw = slime if spec.get("kind") == "slime" else humanoid
    # dead: the front idle frame lying on its side, sunk to the ground
    if spec.get("kind") == "slime":
        dead = np.roll(draw(dict(spec, torso="#4a8a3a"), "down", "hurt", 0), 3 * RES, axis=0)
    else:
        dead = np.roll(np.rot90(draw(spec, "down", "hurt", 0), k=-1), 6 * RES, axis=0)
    # guard (shield raised), one frame per direction
    guard = lambda d: draw(spec, d, "guard" if spec.get("kind") != "slime" else "idle", 0)
    return layout.build(lambda d, action, f: draw(spec, d, action, f), N, dead, guard)


# The Thief's sneak (crouched, Space): the "ninja creep" pose, a sheet of its own (SPR_<id>_sneak) the game swaps to.
SNEAK_POSE = dict(_crouch=5, _crouchFront=7, _wide=1, _lean=3, _reach=3)
SNEAK_SHEETS = ("thief_m", "thief_f")

SPECS = {
    # heroes (male / female differ by hair)
    "knight_m": dict(torso="#3a5a9a", sleeve="#b8c4d2", trim="#d8c060", legs="#8a96a8", boots="#4a4a5a", belt="#5a3a22",
                     helmet="knight", plume="#d04040", weapon="sword", offhand="shield", hair="#6a4a2a", stubble=True, brow="#5a3a22"),
    "knight_f": dict(fem=True, torso="#6a3a8a", sleeve="#c8d0dc", trim="#e8c860", legs="#9aa4b6", boots="#4a4a5a", belt="#5a3a22",
                     helmet="knight", plume="#f0f0f0", weapon="sword", offhand="shield", shieldColor="#7a3a9a",
                     hair="#e0a040", longHair="#e0a040"),
    "mage_m": dict(torso="#6a4fb0", trim="#e0c050", robe=True, helmet="wizard", hatColor="#4b3a8a", weapon="staff", hair="#e8e0d0", beard="#e8e0d0", brow="#d8d0c0"),
    "mage_f": dict(fem=True, torso="#b04f8a", trim="#f0d8a0", robe=True, helmet="wizard", hatColor="#7a2a5a", hatBand="#f0d8a0", weapon="staff", orb="#ffa0e0",
                   hair="#3a2a5a", longHair="#2a1a4a"),
    "thief_m": dict(torso="#3f6b4a", sleeve="#2f5038", legs="#3a3430", boots="#2a2420", belt="#6a4a2a", helmet="hood", hoodColor="#335a3e",
                    mask="#2a3a2e", weapon="dagger", back="bow", slash="#c8ffd8"),
    "thief_f": dict(fem=True, torso="#4a6a3a", sleeve="#3a5a2a", legs="#3a3430", boots="#2a2420", belt="#8a5a2a", helmet="hood", hoodColor="#2f4a5a",
                    weapon="dagger", back="bow", slash="#c8ffd8", longHair="#b0402a"),
    "scholar_m": dict(torso="#2f7a7a", trim="#e8d090", robe=True, weapon="dagger", offhand="buckler", hair="#4a3020", glasses=True, stubble=True, brow="#3a2010"),
    "scholar_f": dict(fem=True, torso="#2f5a8a", trim="#f0e0b0", robe=True, weapon="dagger", offhand="buckler", hair="#6a3a1a", longHair="#6a3a1a"),
    # foes
    "slime": dict(kind="slime", torso="#6cc24a"),
    "archer": dict(skin="#e8e2d0", torso="#d8d2c0", legs="#d8d2c0", boots="#b8b2a0", build="thin", skull=True, weapon="bow", eye="#3a2030"),
    "bandit": dict(torso="#8a5a3a", sleeve="#b0563a", legs="#4a3a30", belt="#2a1a14", helmet="bandana", bandanaColor="#b03028", hair="#3a2a20", weapon="sabre"),
    "bandit_lt": dict(fem=True, torso="#9a6a4a", sleeve="#d08a6a", legs="#4a3a30", belt="#2a1a14", hair="#b0502a", hairstyle="ponytail", weapon="bow"),
    "bandit_captain": dict(torso="#6a2a1e", sleeve="#8a2e1e", trim="#d8b04a", legs="#3a2a24", belt="#d8b04a", build="wide", hair="#2a1a14", beard="#2a1a14",
                           weapon="sabre", helmet="bandana", bandanaColor="#8a1e14"),
    "brute": dict(skin="#8aa070", torso="#6a4a30", legs="#4a3a2a", build="wide", hair="#3a3020", weapon="club", eye="#c03020"),
    # villagers
    "elder": dict(fem=True, torso="#e8c860", robe=True, trim="#a07a30", hair="#e0e0e8", hairstyle="bun", weapon="cane", skin="#e8bc98"),
    "merchant": dict(torso="#d09050", trim="#f0e8d0", legs="#5a4a3a", helmet="cap", capColor="#6a4a2a", hair="#6a4a2a", beard="#8a6a4a", build="wide"),
}


def portrait_face(fr, spec, hcx, hy, skin, eye, fem, helmet):
    """A detailed face for portraits (front view): almond eyes with iris, pupil and catch-light, brows, a hint of
    nose, a mouth, blush; lashes and lips for women, stubble for men who have it, round spectacles."""
    iris = C(spec.get("iris", "#4a7ab0" if fem else "#5a4a3a"))
    masked = helmet == "hood" and spec.get("mask")
    for side in (-1, 1):
        ex, ey = hcx + side * 3.0, hy + 0.6
        fr.fill(ellipse(ex, ey, 1.35, 1.15), C("#fbf7ee"), shade=False)                 # white
        fr.fill(ellipse(ex + 0.1 * side, ey + 0.1, 0.85, 1.0), iris, shade=False)        # iris
        fr.fill(ellipse(ex + 0.1 * side, ey + 0.25, 0.45, 0.6), C("#120c14"), shade=False)  # pupil
        fr.fill(ellipse(ex - 0.3, ey - 0.35, 0.28, 0.28), C("#ffffff"), shade=False)    # catch-light
        fr.line(ex - 1.35, ey - 1.0, ex + 1.35, ey - 1.0, C("#2a1a20"), 1)               # upper lid
        if fem:
            for k in range(3):
                fr.line(ex + side * (0.7 + k * 0.35), ey - 1.0, ex + side * (1.0 + k * 0.45), ey - 1.6 - k * 0.1, C("#1a1018"), 1)
        if not masked:
            bc = C(spec.get("brow", "#4a3020" if not fem else "#6a4030"))
            fr.line(ex - 1.3, ey - 2.2 + (0.2 if fem else 0), ex + 1.3, ey - 2.4 - side * (0.0 if fem else 0.25), bc, 1 if fem else 2)
    if masked:
        return
    fr.line(hcx + 0.1, hy + 2.2, hcx + 0.5, hy + 2.8, mix(skin, (90, 50, 40, 255), 0.4), 1)   # nose
    for side in (-1, 1):
        fr.fill(ellipse(hcx + side * 4.2, hy + 2.8, 1.2, 0.6), mix(skin, C("#f08a8a"), 0.45), shade=False)   # blush
    if fem:
        fr.fill(ellipse(hcx, hy + 4.0, 1.1, 0.45), C("#c8485a"), shade=False)
        fr.line(hcx - 1.0, hy + 4.0, hcx + 1.0, hy + 4.0, C("#8a2a3a"), 1)
    else:
        fr.line(hcx - 1.0, hy + 4.1, hcx + 1.0, hy + 4.0, mix(skin, (80, 40, 40, 255), 0.6), 1)
        if spec.get("stubble"):
            r = np.random.default_rng(7)
            for _ in range(60):
                x, y = hcx + r.uniform(-4.5, 4.5), hy + r.uniform(3.2, 5.6)
                if (x - hcx) ** 2 / 25 + (y - hy - 3) ** 2 / 9 <= 1.2:
                    fr.dot(x, y, mix(skin, (60, 40, 30, 255), 0.5))
    if spec.get("glasses"):
        for side in (-1, 1):
            ring = ellipse(hcx + side * 3.0, hy + 0.6, 1.9, 1.7) & ~ellipse(hcx + side * 3.0, hy + 0.6, 1.6, 1.4)
            fr.a[ring] = C("#c0a060")
        fr.line(hcx - 1.1, hy + 0.4, hcx + 1.1, hy + 0.4, C("#c0a060"), 1)


def portrait(spec):
    """A crisp, detailed bust of a character (front view) at 16 texels per logical unit."""
    old = RES
    use_res(16)
    try:
        img = humanoid(spec, "down", "idle", 0)
    finally:
        use_res(old)
    # Bust: from the top of the head (and hat) to the waist.
    rows = np.nonzero(img[..., 3].max(axis=1))[0]
    cols = np.nonzero(img[..., 3].max(axis=0))[0]
    top = max(0, rows.min() - 8)
    bottom = min(img.shape[0], top + int(img.shape[0] * 0.78))
    left, right = max(0, cols.min() - 8), min(img.shape[1], cols.max() + 9)
    return img[top:bottom, left:right]
