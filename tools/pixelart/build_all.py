"""
Generates every pixel-art asset for the 2D look tests into unreal/ImportSource/Pixel/:

  SPR_<name>.png         character sprite sheets (see characters.py for the layout)
  TX_<name>.png          32x32 seamless textures (HD-2D world materials)
  PR_<name>.png          props: trees, bush, rock, ruin wall block, one house per footprint size
  MAP_Ground.png         the whole map baked as a flat-2D ground image (32 px per tile, plus a forest border)

Run:  python3 tools/pixelart/build_all.py      (then unreal/Tools/import_pixel.py imports them)
"""
import json
import os

import numpy as np

import tessera_path  # noqa: F401
import characters as ch
import environment as env
import ui_art as ui
import extras as ex
import life
from tspixel.png import write_png, upscale

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "unreal", "ImportSource", "Pixel")
PREVIEW = os.path.join(os.path.dirname(__file__), "out")
PAD = 6  # forest tiles around the map in the baked ground


def map_rows():
    data = json.load(open(os.path.join(ROOT, "data", "game-data.json"), encoding="utf-8"))
    rows = [list(r) for r in data["map"]["rows"]]
    known = set("#TH~=.,r")
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c not in known:   # spawn markers stand on path if the row runs through a path, else grass
                near = [row[x - 1] if x > 0 else "", row[x + 1] if x + 1 < len(row) else ""]
                row[x] = "," if "," in near else ("r" if "r" in near else ".")
    return rows


def houses(rows):
    seen, out = set(), []
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c != "H" or (x, y) in seen:
                continue
            stack, cells = [(x, y)], []
            seen.add((x, y))
            while stack:
                px, py = stack.pop()
                cells.append((px, py))
                for nx, ny in ((px + 1, py), (px - 1, py), (px, py + 1), (px, py - 1)):
                    if 0 <= ny < len(rows) and 0 <= nx < len(rows[0]) and rows[ny][nx] == "H" and (nx, ny) not in seen:
                        seen.add((nx, ny)); stack.append((nx, ny))
            xs, ys = [c[0] for c in cells], [c[1] for c in cells]
            out.append((min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1))
    return out


def bake_ground(rows):
    H, W = len(rows), len(rows[0])
    T = env.T
    img = np.zeros(((H + 2 * PAD) * T, (W + 2 * PAD) * T, 4), np.uint8)
    tiles = {
        ".": [env.grass(s) for s in range(1, 9)], ",": [env.dirt(s) for s in range(2, 6)], "r": [env.cobble(s) for s in range(3, 7)],
        "~": [env.water(s) for s in range(9, 13)], "F": [env.forest_floor(s) for s in range(10, 14)],
    }
    rnd = np.random.default_rng(5)

    def kind(x, y):
        if not (0 <= x < W and 0 <= y < H):
            return "F"
        c = rows[y][x]
        if c in "TH":
            return "."
        if c == "#":
            # the outer border is forest; inner walls (ruins) stand on cobbles
            return "F" if x in (0, W - 1) or y in (0, H - 1) else "r"
        if c == "=":
            return "~"
        return c

    for ty in range(-PAD, H + PAD):
        for tx in range(-PAD, W + PAD):
            k = kind(tx, ty)
            t = tiles[k][rnd.integers(0, len(tiles[k]))].copy()
            # soft, jagged edges: grass creeps 1-2 px over path / cobble borders
            for (dx, dy, sl) in ((-1, 0, np.s_[:, 0:2]), (1, 0, np.s_[:, T - 2:T]), (0, -1, np.s_[0:2, :]), (0, 1, np.s_[T - 2:T, :])):
                nk = kind(tx + dx, ty + dy)
                if k in ",r" and nk == ".":
                    g = env.grass(int(rnd.integers(0, 99)), flowers=False)
                    m = rnd.random(t[sl].shape[:2]) < 0.55
                    t[sl][m] = g[sl][m]
                if k == "~" and nk not in "~":
                    edge = t[sl]
                    edge[...] = (150, 200, 230, 255)            # foam line where water meets land
            if 0 <= tx < W and 0 <= ty < H and rows[ty][tx] == "=":
                t = env.planks(7, "#a0743e")
                t[:, 0:2] = (90, 60, 34, 255); t[:, T - 2:T] = (90, 60, 34, 255)
            y0, x0 = (ty + PAD) * T, (tx + PAD) * T
            img[y0:y0 + T, x0:x0 + T] = t
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    n = 0
    for name, spec in ch.SPECS.items():
        write_png(os.path.join(OUT, f"SPR_{name}.png"), ch.sheet(spec)); n += 1
        if spec.get("kind") != "slime":
            write_png(os.path.join(OUT, f"POR_{name}.png"), ch.portrait(spec)); n += 1   # dialogue portraits
    for name, fn in env.TEXTURES.items():
        write_png(os.path.join(OUT, f"TX_{name}.png"), fn()); n += 1
    write_png(os.path.join(OUT, "PR_Tree1.png"), env.tree(20)); n += 1
    write_png(os.path.join(OUT, "PR_Tree2.png"), env.tree(23, 52, 66)); n += 1
    write_png(os.path.join(OUT, "PR_Bush.png"), env.bush()); n += 1
    write_png(os.path.join(OUT, "PR_Rock.png"), env.rock()); n += 1
    write_png(os.path.join(OUT, "PR_Wall.png"), env.wall_block()); n += 1
    write_png(os.path.join(OUT, "PR_Shadow.png"), env.blob_shadow()); n += 1
    for name, fn in (("FX_Arrow", ui.fx_arrow), ("FX_Orb", ui.fx_orb), ("FX_Heal", ui.fx_heal), ("FX_Ring", ui.fx_ring), ("UI_Stage", ui.stage), ("UI_Fade", ui.fade), ("UI_FadeUp", ui.fade_up), ("UI_FadeRadial", ui.fade_radial), ("UI_Vignette", ui.vignette)):
        write_png(os.path.join(OUT, f"{name}.png"), fn()); n += 1
    for name in ui.ICONS:
        write_png(os.path.join(OUT, f"ICO_{name}.png"), ui.icon(name)); n += 1
    for name, fn in ex.CURSORS.items():
        write_png(os.path.join(OUT, f"CUR_{name}.png"), fn()); n += 1
    for name, fn in (("BD_knight", ex.bd_knight), ("BD_thief", ex.bd_thief), ("BD_mage", ex.bd_mage), ("BD_scholar", ex.bd_scholar),
                     ("FX_Petal", ex.fx_petal), ("FX_Ray", ex.fx_ray), ("FX_Coin", ex.fx_coin), ("FX_Bolt", ex.fx_bolt),
                     ("FX_Runes", ex.fx_runes), ("FX_RunesSpin", ex.fx_runes_spin), ("FX_Page", ex.fx_page), ("FX_Spark", ex.fx_spark),
                     ("SPR_Ninja", ex.spr_ninja), ("SPR_Chest", ex.spr_chest), ("SPR_Book", ex.spr_book),
                     ("SPR_Bird", ex.spr_bird), ("SPR_Goose", ex.spr_goose), ("SPR_Ghost", ex.spr_ghost)):
        write_png(os.path.join(OUT, f"{name}.png"), fn()); n += 1
    write_png(os.path.join(OUT, "SPR_sneak.png"), ch.sheet(ex.SNEAK)); n += 1
    for name, fn in life.SHEETS.items():   # world mood and Scholar quests (life.py)
        write_png(os.path.join(OUT, f"{name}.png"), fn()); n += 1
    rows = map_rows()
    sizes = sorted({(w, h) for (_, _, w, h) in houses(rows)})
    for (w, h) in sizes:
        write_png(os.path.join(OUT, f"PR_House_{w}x{h}.png"), env.house(w, h)); n += 1
    for kind in ui.MINIMAP_SHAPES:
        write_png(os.path.join(OUT, f"MM_Mask_{kind}.png"), ui.minimap_mask(kind)); n += 1
        write_png(os.path.join(OUT, f"MM_Frame_{kind}.png"), ui.minimap_frame(kind)); n += 1
    write_png(os.path.join(OUT, "MAP_Mini.png"), ui.minimap_image(rows, houses(rows), PAD)); n += 1
    ground = bake_ground(rows)
    write_png(os.path.join(OUT, "MAP_Ground.png"), ground); n += 1
    write_png(os.path.join(PREVIEW, "preview_ground.png"), ground[::2, ::2])
    print(f"wrote {n} files to {OUT}; houses {sizes}; ground {ground.shape[1]}x{ground.shape[0]} (pad {PAD})")


if __name__ == "__main__":
    main()
