"""
Imports the generated pixel art (ImportSource/Pixel/*.png, made by tools/pixelart/build_all.py) and builds the
two materials the 2D looks use. Run headless:

    UnrealEditor-Cmd.exe ActionRPG.uproject -run=pythonscript -script=Tools/import_pixel.py

  textures          /Game/RPG/Pixel/<file name>   nearest filtering, no mips, uncompressed (crisp pixels)
  M_RPG_Sprite      masked, lit, two-sided card: plays one frame of a sprite sheet
                    params: Tex, Cols, Rows, Col, Row, Flip (mirror), Tint, Flash (hit flash), Emissive
                    normal is world-up, so a card facing the camera is lit like the ground under it
  M_RPG_PixelWorld  opaque, lit: a 32 px texture projected by world position on whichever axis a face
                    points along (no UVs needed), Size = world units per texture repeat
"""
import glob
import os

import unreal

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ImportSource", "Pixel")
DEST = "/Game/RPG/Pixel"
MATS = "/Game/RPG/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary

# --- textures -------------------------------------------------------------------------------------
tasks = []
for path in sorted(glob.glob(os.path.join(SRC, "*.png"))):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.abspath(path))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    tasks.append(t)
tools.import_asset_tasks(tasks)
for t in tasks:
    for obj_path in t.get_editor_property("imported_object_paths"):
        tex = unreal.load_asset(obj_path)
        if not isinstance(tex, unreal.Texture2D):
            continue
        if tex.get_name().startswith("POR_"):   # portraits: high-res art, scaled smoothly
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            tex.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
        else:
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
            tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("never_stream", True)
        eal.save_loaded_asset(tex, only_if_is_dirty=False)
unreal.log("[RPG] imported %d pixel textures" % len(tasks))


# --- materials ------------------------------------------------------------------------------------
def fresh(name):
    full = MATS + "/" + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    mat = tools.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())
    for flag in ("used_with_instanced_static_meshes", "used_with_skeletal_mesh"):
        try:
            mat.set_editor_property(flag, True)
        except Exception:
            pass
    return mat


def scalar(m, name, v, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", v)
    return e


def vector(m, name, v, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*v))
    return e


def const3(m, v, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, x, y)
    e.set_editor_property("constant", unreal.LinearColor(v[0], v[1], v[2], 1))
    return e


def const(m, v, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", v)
    return e


def custom(m, code, inputs, out_type, x, y):
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out_type)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    return c


def finish(m):
    mel.recompile_material(m)
    eal.save_loaded_asset(m, only_if_is_dirty=False)
    unreal.log("[RPG] created " + m.get_path_name())


# M_RPG_Sprite ------------------------------------------------------------------------------------------
m = fresh("M_RPG_Sprite")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
m.set_editor_property("two_sided", True)
m.set_editor_property("tangent_space_normal", False)
m.set_editor_property("opacity_mask_clip_value", 0.5)
uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
calc = custom(m, "float u = lerp(UV.x, 1.0 - UV.x, Flip);\nreturn float2((u + Col) / Cols, (UV.y + Row) / Rows);",
              ["UV", "Cols", "Rows", "Col", "Row", "Flip"], unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1000, 0)
mel.connect_material_expressions(uv, "", calc, "UV")
for i, (n, v) in enumerate((("Cols", 4.0), ("Rows", 13.0), ("Col", 0.0), ("Row", 0.0), ("Flip", 0.0))):
    mel.connect_material_expressions(scalar(m, n, v, -1400, 120 + i * 90), "", calc, n)
tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
tex.set_editor_property("parameter_name", "Tex")
default_sheet = unreal.load_asset(DEST + "/SPR_knight_m")
if default_sheet:
    tex.set_editor_property("texture", default_sheet)
mel.connect_material_expressions(calc, "", tex, "UVs")
tint = mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 0)
mel.connect_material_expressions(tex, "RGB", mul, "A")
mel.connect_material_expressions(vector(m, "Tint", (1, 1, 1, 1), -700, 300), "", mul, "B")
flash = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -200, 0)
mel.connect_material_expressions(mul, "", flash, "A")
mel.connect_material_expressions(const3(m, (1, 1, 1), -400, 200), "", flash, "B")
mel.connect_material_expressions(scalar(m, "Flash", 0.0, -400, 320), "", flash, "Alpha")
mel.connect_material_property(flash, "", unreal.MaterialProperty.MP_BASE_COLOR)
emis = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -100, 300)
mel.connect_material_expressions(flash, "", emis, "A")
mel.connect_material_expressions(scalar(m, "Emissive", 0.0, -400, 440), "", emis, "B")
mel.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
mel.connect_material_property(const3(m, (0, 0, 1), -200, 500), "", unreal.MaterialProperty.MP_NORMAL)
mel.connect_material_property(const(m, 1.0, -200, 600), "", unreal.MaterialProperty.MP_ROUGHNESS)
mel.connect_material_property(const(m, 0.1, -200, 680), "", unreal.MaterialProperty.MP_SPECULAR)
finish(m)

# M_RPG_PixelWorld ---------------------------------------------------------------------------------------
m = fresh("M_RPG_PixelWorld")
texobj = mel.create_material_expression(m, unreal.MaterialExpressionTextureObjectParameter, -1000, 0)
texobj.set_editor_property("parameter_name", "Tex")
grass = unreal.load_asset(DEST + "/TX_grass")
if grass:
    texobj.set_editor_property("texture", grass)
wp = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1000, 200)
nrm = mel.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -1000, 300)
code = """float3 n = abs(N);
float3 p = WP / Size;
float2 uv = (n.z >= n.x && n.z >= n.y) ? p.xy : (n.x >= n.y ? float2(p.y, -p.z) : float2(p.x, -p.z));
return Texture2DSample(Tex, TexSampler, uv).rgb;"""
tri = custom(m, code, ["Tex", "WP", "N", "Size"], unreal.CustomMaterialOutputType.CMOT_FLOAT3, -600, 0)
mel.connect_material_expressions(texobj, "", tri, "Tex")
mel.connect_material_expressions(wp, "", tri, "WP")
mel.connect_material_expressions(nrm, "", tri, "N")
mel.connect_material_expressions(scalar(m, "Size", 200.0, -1000, 420), "", tri, "Size")
tmul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 0)
mel.connect_material_expressions(tri, "", tmul, "A")
mel.connect_material_expressions(vector(m, "Tint", (1, 1, 1, 1), -600, 300), "", tmul, "B")
mel.connect_material_property(tmul, "", unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(const(m, 0.92, -300, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
mel.connect_material_property(const(m, 0.15, -300, 380), "", unreal.MaterialProperty.MP_SPECULAR)
finish(m)

unreal.log("[RPG] import_pixel done")

# M_RPG_Minimap ------------------------------------------------------------------------------------------
# UI material: a window of the map (Center = UV of the player, Span = UV size of the window), cut to the
# class-shaped mask (orb / shield / coin / book).
m = fresh("M_RPG_Minimap")
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
unreal.log("[RPG] minimap material done")

# M_RPG_NightShade ---------------------------------------------------------------------------------------
# UI overlay for deep night (drawn under the HUD): near-black everywhere except ellipses around the hero and the
# nearest lights. Hero / Light0..7 = (u, v, radius u, radius v) in screen UV; Night = 0..1 strength.
m = fresh("M_RPG_NightShade")
m.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
uvn = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
names = ["UV", "Hero", "Night"] + ["L%d" % i for i in range(8)]
code = """float vis = 0.0;
float4 H[9] = { Hero, L0, L1, L2, L3, L4, L5, L6, L7 };
for (int i = 0; i < 9; i++)
{
    if (H[i].z <= 0.0) continue;
    float d = length((UV - H[i].xy) / H[i].zw);
    vis = max(vis, 1.0 - smoothstep(i == 0 ? 0.5 : 0.4, 1.0, d));
}
return Night * (1.0 - vis);"""
shade = custom(m, code, names, unreal.CustomMaterialOutputType.CMOT_FLOAT1, -700, 0)
def rgba(m, name, value, x, y):
    """A vector parameter as a full float4 (a vector parameter's default output is RGB only)."""
    v = vector(m, name, value, x, y)
    app = mel.create_material_expression(m, unreal.MaterialExpressionAppendVector, x + 250, y)
    mel.connect_material_expressions(v, "", app, "A")
    mel.connect_material_expressions(v, "A", app, "B")
    return app


mel.connect_material_expressions(uvn, "", shade, "UV")
mel.connect_material_expressions(rgba(m, "Hero", (0.5, 0.5, 0.3, 0.3), -1700, 150), "", shade, "Hero")
mel.connect_material_expressions(scalar(m, "Night", 0.0, -1400, 300), "", shade, "Night")
for i in range(8):
    mel.connect_material_expressions(rgba(m, "Light%d" % i, (0, 0, 0, 0), -1700, 400 + i * 100), "", shade, "L%d" % i)
mel.connect_material_property(const3(m, (0.0, 0.0, 0.0), -400, 200), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)   # true black: a lifted tone reads as grey
mel.connect_material_property(shade, "", unreal.MaterialProperty.MP_OPACITY)
finish(m)
if eal.does_asset_exist(MATS + "/M_RPG_NightVision"):
    eal.delete_asset(MATS + "/M_RPG_NightVision")   # replaced by the UI overlay above
unreal.log("[RPG] night shade material done")
