# Action RPG

A top-down action RPG in the HD-2D style (pixel-art characters standing in a lit 3D world) where talking matters as
much as fighting. There are four classes (Knight, Mage, Thief, Scholar), two sexes, class-specific combat and
dialogue styles, and quests with several possible outcomes. It is built in Unreal Engine 5.8; a single-file HTML
version is the test bed for the rules.

| Folder | What |
|---|---|
| `play.ps1` | **Start here.** One-click launcher: play, test, package |
| `unreal/` | The Unreal Engine 5.8 game (C++). See `unreal/README.md` |
| `prototype/rpg-prototype.html` | The HTML top-down test bed for the rules: one file, open it in any browser |
| `data/game-data.json` | The rules both versions share: stats, classes, abilities, enemies, items, quests, dialogue, map, look settings |
| `tools/sync-data.js` | Pushes `data/game-data.json` into both versions (`play.ps1` does this for you) |
| `tools/pixelart/` | Draws all the pixel art in code (characters, world textures, props, UI, cursors, backdrops) |
| `lookdev/` | Look tests: `index.html` (3D vs HD-2D vs flat 2D), `crisp.html` (fog / tilt-shift comparison) |
| `DESIGN.md`, `CHARACTERS.md` | Game design: mechanics, classes, dialogue, quest structure |
| `SO_FAR.md` | Project log and handoff: where things stand, how it's built, what's next |

## Play

```powershell
.\play.ps1                          # full-screen: title screen -> character select -> the village
.\play.ps1 -Windowed                # in a window
.\play.ps1 -Class mage -Sex female  # skip the title and character select
.\play.ps1 -Test                    # every automated scenario, in parallel (sized to this PC), PASS/FAIL
.\play.ps1 -Package                 # build a standalone Dist\Windows\ActionRPG.exe and run it
```

`play.ps1` finds Unreal 5.8, syncs the data if you changed it, compiles only if the code changed, and generates
the materials and imports the pixel art on first run. If PowerShell refuses to run scripts, use
`powershell -ExecutionPolicy Bypass -File .\play.ps1`.

**Controls (mouse first):** left-click the ground to walk, an enemy to fight, a villager to talk; **E** then click
someone to talk to them (foes included); **Shift + left-click** attacks in place; **right-click (hold)** blocks or
raises the Mage's barrier or draws the bow; **Space** dodges; **1-4** abilities, or **Shift + mouse wheel** to pick one
in slow motion; **wheel** zooms; **Esc** pauses. In conversations: 1-9, or wheel / arrows + Enter.

**Needs:** Unreal Engine 5.8 (Epic Games Launcher) and Visual Studio 2026 with "Game development with C++",
which is needed to compile. Node.js keeps the HTML prototype's data in sync; Python 3 with numpy regenerates the
pixel art (`python3 tools/pixelart/build_all.py`).
