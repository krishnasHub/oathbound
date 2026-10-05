# Action RPG

A third-person action RPG where talking matters as much as fighting. There are four classes (Knight, Mage,
Thief, Scholar), two sexes, class-specific combat and dialogue styles, and quests with several possible outcomes.

| Folder | What |
|---|---|
| `play.ps1` | **Start here.** One-click launcher for the Unreal game |
| `unreal/` | The Unreal Engine 5.8 game (C++). See `unreal/README.md` |
| `prototype/rpg-prototype.html` | The original top-down prototype: one HTML file, open it in any browser |
| `data/game-data.json` | The rules both versions share: stats, classes, abilities, enemies, items, quests, dialogue, map |
| `DESIGN.md`, `CHARACTERS.md` | Game design: mechanics, classes, dialogue, quest structure, animation plan |
| `tools/sync-data.js` | Pushes `data/game-data.json` into both versions (`play.ps1` does this for you) |

## Play

```powershell
.\play.ps1                          # full-screen, starts at character select
.\play.ps1 -Windowed                # in a window
.\play.ps1 -Class mage -Sex female  # skip character select
.\play.ps1 -Test                    # run every automated scenario, report PASS/FAIL
.\play.ps1 -Package                 # build a standalone Dist\Windows\ActionRPG.exe and run it
```

`play.ps1` finds Unreal 5.8, syncs the data if you changed it, compiles only if the code changed, and generates
the materials on first run. If PowerShell refuses to run scripts, use
`powershell -ExecutionPolicy Bypass -File .\play.ps1`.

**Needs:** Unreal Engine 5.8 (Epic Games Launcher) and Visual Studio 2026 with "Game development with C++",
which is needed to compile. Node.js is optional (used to keep the HTML prototype's data in sync).
