"""
One-time asset fix-up, run headless by tools/prepare-assets.ps1:

    UnrealEditor-Cmd.exe ActionRPG.uproject -run=pythonscript -script=Tools/fix_material_usage.py

The world builder draws trees, bushes, rocks and bridge planks with instanced static meshes. A material
must be flagged "Used with Instanced Static Meshes" to render that way; the Starter Content materials
are not, and a running game cannot add the flag (it needs a shader recompile), so it silently falls
back to the grey default material. This script sets the flag on every Starter Content material and
saves the asset.
"""
import unreal

ROOTS = ["/Game/StarterContent/Materials", "/Game/StarterContent/Props/Materials"]

registry = unreal.AssetRegistryHelpers.get_asset_registry()
changed = 0
for root in ROOTS:
    for data in registry.get_assets_by_path(root, recursive=True):
        if str(data.asset_class_path.asset_name) != "Material":
            continue
        mat = unreal.load_asset(str(data.package_name))
        if mat.get_editor_property("used_with_instanced_static_meshes"):
            continue
        mat.set_editor_property("used_with_instanced_static_meshes", True)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
        changed += 1
        unreal.log("[RPG] flagged for instancing: " + str(data.package_name))

unreal.log("[RPG] fix_material_usage done: %d materials updated" % changed)
