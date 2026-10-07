"""
One-time asset fix-up, run headless by `rpg.ps1 prepare`:

    UnrealEditor-Cmd.exe Oathbound.uproject -run=pythonscript -script=Tools/fix_material_usage.py

The world builder draws trees, bushes, rocks and bridge planks with instanced static meshes. A material must be
flagged "Used with Instanced Static Meshes" to render that way; the Starter Content materials are not, and a
running game cannot add the flag, so it silently falls back to the grey default material. This sets the flag on
every Starter Content material (Tessera's tessera_assets.flag_for_instancing).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Plugins", "Tessera", "Tools", "unreal"))
import tessera_assets as ta  # noqa: E402

changed = ta.flag_for_instancing(["/Game/StarterContent/Materials", "/Game/StarterContent/Props/Materials"])
ta.log("fix_material_usage done: %d materials updated" % changed)
