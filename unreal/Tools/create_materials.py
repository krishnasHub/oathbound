"""
Creates the game's own materials (no clicking in the material editor), run headless by `rpg.ps1 prepare`:

    UnrealEditor-Cmd.exe ActionRPG.uproject -run=pythonscript -script=Tools/create_materials.py

Each material's node graph is built here in code. They are regenerated from scratch every run, so this
script — not the .uasset — is the source of truth.

  M_RPG_Glow       unlit, Color * Intensity          projectiles, magic orbs, embers
  M_RPG_Telegraph  translucent unlit, Color/Opacity  enemy wind-up markers on the ground
  M_RPG_Fresnel    translucent unlit rim glow        mana shield, Ward, buff auras
  M_RPG_Flash      translucent unlit white overlay   hit flash (used as an overlay material)
"""
import unreal

PATH = "/Game/RPG/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def fresh(name):
    full = PATH + "/" + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    mat = tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    for flag in ("used_with_skeletal_mesh", "used_with_instanced_static_meshes", "used_with_niagara_sprites",
                 "used_with_niagara_mesh_particles", "used_with_particle_sprites"):
        try:
            mat.set_editor_property(flag, True)
        except Exception:
            unreal.log_warning("[RPG] usage flag not available in this engine version: " + flag)
    return mat


def vec(mat, name, value, x=-600, y=0):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*value))
    return e


def scalar(mat, name, value, x=-600, y=200):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def mul(mat, a, b, x=-300, y=0):
    m = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, "", m, "A")
    mel.connect_material_expressions(b, "", m, "B")
    return m


def finish(mat):
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat, only_if_is_dirty=False)
    unreal.log("[RPG] created " + mat.get_path_name())


# --- M_RPG_Glow: bright unlit colour --------------------------------------------------------------
m = fresh("M_RPG_Glow")
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mel.connect_material_property(mul(m, vec(m, "Color", (1.0, 0.5, 0.2, 1)), scalar(m, "Intensity", 8.0)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)

# --- M_RPG_Telegraph: see-through coloured marker ---------------------------------------------------
m = fresh("M_RPG_Telegraph")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("two_sided", True)
mel.connect_material_property(mul(m, vec(m, "Color", (1.0, 0.15, 0.1, 1)), scalar(m, "Intensity", 3.0)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(scalar(m, "Opacity", 0.35, y=400), "", unreal.MaterialProperty.MP_OPACITY)
finish(m)

# --- M_RPG_Fresnel: rim-lit bubble (mana shield, Ward) -----------------------------------------------
m = fresh("M_RPG_Fresnel")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
fres = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -900, 300)
fres.set_editor_property("exponent", 2.5)
color = mul(m, vec(m, "Color", (0.5, 0.6, 1.0, 1)), scalar(m, "Intensity", 4.0))
mel.connect_material_property(mul(m, color, fres, -150, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(mul(m, fres, scalar(m, "Opacity", 0.6, y=500), -150, 300), "", unreal.MaterialProperty.MP_OPACITY)
finish(m)

# --- M_RPG_Flash: white overlay for hit flashes ---------------------------------------------------------
m = fresh("M_RPG_Flash")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mel.connect_material_property(vec(m, "Color", (3.0, 3.0, 3.0, 1)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(scalar(m, "Opacity", 0.55, y=300), "", unreal.MaterialProperty.MP_OPACITY)
finish(m)

unreal.log("[RPG] create_materials done")
