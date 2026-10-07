"""
Creates the game's own materials (no clicking in the material editor), run headless by `rpg.ps1 prepare`:

    UnrealEditor-Cmd.exe Oathbound.uproject -run=pythonscript -script=Tools/create_materials.py

The node graphs are Tessera's (Plugins/Tessera/Tools/unreal/tessera_assets.py); this picks the names, folder and
colours. They are regenerated from scratch every run, so the scripts — not the .uasset — are the source of truth.

  M_RPG_Glow       unlit, Color * Intensity          projectiles, magic orbs, embers
  M_RPG_Telegraph  translucent unlit, Color/Opacity  enemy wind-up markers on the ground
  M_RPG_Fresnel    translucent unlit rim glow        barrier, Ward, buff auras
  M_RPG_Flash      translucent unlit white overlay   hit flash (used as an overlay material)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Plugins", "Tessera", "Tools", "unreal"))
import tessera_assets as ta  # noqa: E402

PATH = "/Game/RPG/Materials"
ta.glow(PATH, "M_RPG_Glow")
ta.telegraph(PATH, "M_RPG_Telegraph")
ta.fresnel(PATH, "M_RPG_Fresnel")
ta.flash(PATH, "M_RPG_Flash")
ta.log("create_materials done")
