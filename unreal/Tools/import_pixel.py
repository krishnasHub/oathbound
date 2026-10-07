"""
Imports the generated pixel art (ImportSource/Pixel/*.png, made by tools/pixelart/build_all.py) and builds the
materials the 2D looks and the HUD use, with Tessera's builders (Plugins/Tessera/Tools/unreal/tessera_assets.py).
Run headless:

    UnrealEditor-Cmd.exe Oathbound.uproject -run=pythonscript -script=Tools/import_pixel.py

  textures          /Game/RPG/Pixel/<file name>   nearest filtering, no mips, uncompressed (crisp pixels)
  M_RPG_Sprite      masked, lit, two-sided card: plays one frame of a sprite sheet
                    params: Tex, Cols, Rows, Col, Row, Flip (mirror), Tint, Flash (hit flash), Emissive
                    normal is world-up, so a card facing the camera is lit like the ground under it
  M_RPG_PixelWorld  opaque, lit: a 32 px texture projected by world position on whichever axis a face
                    points along (no UVs needed), Size = world units per texture repeat
  M_RPG_NightShade  UI overlay for deep night (Tessera's STSNightShade)
  M_RPG_SpriteSeeThrough  the sprite card, translucent (a fallen foe's ghost)
  M_RPG_Minimap     this game's minimap: a window of the baked map cut to the class-shaped mask
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Plugins", "Tessera", "Tools", "unreal"))
import tessera_assets as ta  # noqa: E402
from tessera_assets import mel, eal, fresh, vector, custom, finish  # noqa: E402

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ImportSource", "Pixel")
DEST = "/Game/RPG/Pixel"
MATS = "/Game/RPG/Materials"

ta.import_textures(SRC, DEST, smooth_prefixes=("POR_",))   # portraits: high-res art, scaled smoothly
ta.sprite(MATS, "M_RPG_Sprite", default_texture=DEST + "/SPR_knight_m")
ta.pixel_world(MATS, "M_RPG_PixelWorld", default_texture=DEST + "/TX_grass")
ta.night_shade(MATS, "M_RPG_NightShade")
ta.sprite(MATS, "M_RPG_SpriteSeeThrough", default_texture=DEST + "/SPR_Ghost", cols=4.0, rows=1.0, see_through=True)   # ghosts
if eal.does_asset_exist(MATS + "/M_RPG_NightVision"):
    eal.delete_asset(MATS + "/M_RPG_NightVision")   # replaced by the UI overlay above

# M_RPG_Minimap ------------------------------------------------------------------------------------------
# UI material: a window of the map (Center = UV of the player, Span = UV size of the window), cut to the
# class-shaped mask (orb / shield / coin / book).
m = fresh(MATS, "M_RPG_Minimap")
m.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
win = custom(m, "return Center.xy + (UV - 0.5) * Span.xy;", ["UV", "Center", "Span"], unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1000, 0)
mel.connect_material_expressions(uv, "", win, "UV")
mel.connect_material_expressions(vector(m, "Center", (0.5, 0.5, 0, 0), -1400, 150), "", win, "Center")
mel.connect_material_expressions(vector(m, "Span", (0.25, 0.3, 0, 0), -1400, 350), "", win, "Span")
mapt = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
mapt.set_editor_property("parameter_name", "Map")
mt = unreal.load_asset(DEST + "/MAP_Mini")
if mt:
    mapt.set_editor_property("texture", mt)
mel.connect_material_expressions(win, "", mapt, "UVs")
maskt = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 300)
maskt.set_editor_property("parameter_name", "Mask")
mk = unreal.load_asset(DEST + "/MM_Mask_orb")
if mk:
    maskt.set_editor_property("texture", mk)
mel.connect_material_expressions(uv, "", maskt, "UVs")
mel.connect_material_property(mapt, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(maskt, "A", unreal.MaterialProperty.MP_OPACITY)
finish(m)
ta.log("import_pixel done")
