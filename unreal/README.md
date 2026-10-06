# Action RPG — Unreal Engine 5.8

The game, top-down in the HD-2D style: pixel-art sprite characters standing in a lit 3D world (Lumen, real
shadows, a day/night cycle, fog), driven by the same rules and data as the HTML test bed
(`../prototype/rpg-prototype.html`, `../data/game-data.json`).

Almost everything is C++. There are no Blueprints of our own and no hand-built level: the world, lighting, UI,
input and materials are created by code; the pixel art is drawn by Python (`../tools/pixelart`) and imported by
`Tools/import_pixel.py`.

## Running it

**You need:** Unreal Engine **5.8** (Epic Games Launcher) and, to compile the C++, **Visual Studio 2026**
with the "Game development with C++" workload. Node.js syncs data; Python 3 + numpy regenerates art.

From PowerShell in this folder (`action-rpg\unreal`):

```powershell
.\Tools\rpg.ps1 build                 # compile the C++ (after any code change; ~15-30 s incremental)
.\Tools\rpg.ps1 run                   # play in a window: title -> character select -> game
.\Tools\rpg.ps1 run -Class mage -Sex female    # straight into the game
.\Tools\rpg.ps1 editor                # open in the Unreal Editor (then press Play)
.\Tools\rpg.ps1 prepare               # (re)build materials and import the pixel art
.\Tools\rpg.ps1 sync                  # copy ../data/game-data.json here and into the HTML prototype
```

Data changes need no rebuild: sync and restart. Art changes: `python3 ../tools/pixelart/build_all.py`, then
`prepare`.

## Controls

| Input | Action |
|---|---|
| Left click on the ground | Walk there (hold to follow the cursor). Pathfinds around houses and the river |
| Left click on an enemy | Walk into range and attack (hold to keep attacking) |
| Left click on a villager | Walk up and talk |
| E, then click someone | Talk to them (foes too, while they're not hostile). The cursor shows a speech bubble, greyed when nobody there will talk |
| Shift + left click | Attack in place, toward the cursor |
| Right click (hold) | Knight: shield block (100%, front; perfect block staggers). Scholar: buckler (50%). Mage: barrier all around (80%). All drain stamina while held. Thief: draw the bow, release to fire (arrows arc onto the target) |
| Space | Dodge (toward the cursor) |
| 1–4 / Shift + wheel | Abilities; Shift + wheel opens a picker in slow motion, release Shift to cast |
| Mouse wheel | Zoom (limited) |
| WASD | Optional screen-relative movement |
| Q / I / C / J / H | Potion / inventory / character / quests / help |
| Esc | Pause menu: Resume / New Game / Quit |
| ~ | Debug: dialogue odds and AI states; then K = level up, G = +100 gold |

The cursor shows what a click will do: the class's weapon (sword, dagger, wand, arrow) to attack, a speech
bubble to talk, a pointer to walk.

## Testing without playing

Scripted scenarios drive the real game and report what happened. `..\play.ps1 -Test` runs them all in
parallel (as many at once as this PC's cores, free RAM and video memory allow), each with its own log in
`Saved/Logs/Tests/`; about 40 s for all of them on a fast PC.

```powershell
.\Tools\rpg.ps1 test -Scenario combat   # knight kills a slime: swings, XP, quest progress, loot
.\Tools\rpg.ps1 test -Scenario block    # shield turns to the threat on its own; blocked hits do no damage
.\Tools\rpg.ps1 test -Scenario elder    # talk to Elder Maren, accept the quest (+ screenshot)
.\Tools\rpg.ps1 test -Scenario bridge   # walk up (nobody stops you), choose to talk to Brask, honor duel, he yields
.\Tools\rpg.ps1 test -Scenario mage     # arcane bolts vs an archer
.\Tools\rpg.ps1 test -Scenario thief    # full-draw arrow (arcing) + backstab
.\Tools\rpg.ps1 test -Scenario walk     # click-to-move round a house, click-to-talk, click-to-attack
.\Tools\rpg.ps1 test -Scenario picker   # Shift+wheel picker (slow motion, cycle, cast); talk vs fight
.\Tools\rpg.ps1 test -Scenario smoke    # Smoke Bomb staggers foes in the blast and clears after its duration
.\Tools\rpg.ps1 test -Scenario pause    # Esc pause menu, Resume, New Game back to character select
.\Tools\rpg.ps1 test -Scenario click    # one real mouse click on a dialogue choice answers it
.\Tools\rpg.ps1 shot -Class knight -Name hero -At 10 -Extra "-RPGNoInput -RPGHour=18.5"   # a screenshot at dusk
```

Useful switches (`-Extra`): `-RPGHour=22` (start at that hour), `-RPGLook=hd2d|flat2d|mesh3d`, `-RPGLowHP`,
`-RPGAutoSelect=mage` (stay on character select), `-RPGAutoBegin`, `-RPGFog=0|1`, `-RPGNoDOF`.

## How it's built

| Piece | Where | Notes |
|---|---|---|
| Game rules | `Content/Data/game-data.json` | Synced from `../data`. Read at startup by `URPGData` |
| Look | `Core/RPGLook` | HD-2D (default), flat 2D or 3D meshes; card rotation, pixel materials |
| World | `World/RPGWorldBuilder` | Map rows → terrain, river, bridge, cottages (cut away when they hide you), tree cards, ruins, fires; sun + moon day/night cycle, fog, exposure grade; runtime navmesh |
| Ambient life | `World/RPGAmbient` | Birds and geese by day, a prowler and fireflies by night; they react to the hero |
| Characters | `Characters/` | `ARPGCharacterBase` (hidden mannequin for animation timing) + `URPGSpriteComponent` (the pixel sprite you see) → player, enemy (AI state machine), NPC |
| Combat | `Combat/` | Stats, damage pipeline (blocks, barrier), abilities (12 types), arcing arrows, inventory, loot, effects |
| Story | `Game/RPGStory` | Flags, quests, dialogue engine (seeded hidden rolls), encounters, factions |
| UI | `UI/` | Slate: title, character select (animated backdrops per class), HUD, minimap (class-shaped frame), dialogue, pause menu, game cursor, deep-night darkness |
| Input | `RPGPlayerCharacter` | Enhanced Input in code; click-to-move / attack / talk |
| Materials | `Tools/create_materials.py`, `Tools/import_pixel.py` | Glow, telegraph, fresnel, flash; sprite, pixel-world, minimap, night-shade |

## Known gaps / next

- **Art:** all pixel art is procedural placeholder; portraits are built but switched off (`world3d.dialoguePortraits`) until there's proper illustrated art.
- **Sprites:** no dedicated cast or bow frames (they reuse the attack and wind-up frames); the Knight's longsword doesn't change the sprite.
- **Enemies:** steer directly (the navmesh is only used by the hero so far).
- **Saves:** none yet (New Game only).
