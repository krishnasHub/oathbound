# Action RPG — Unreal Engine 5.8

The third-person version of the action RPG prototyped in `../prototype/rpg-prototype.html`.
Same rules, same data (`../data/game-data.json`), rebuilt in Unreal: real characters and animation,
physically-based sky lighting, and a 3D world generated from the same map.

Almost everything is C++. There are no Blueprints of our own and no hand-built level: the world, lighting,
UI, input and materials are all created by code or by scripts in `Tools/`.

## Running it

**You need:** Unreal Engine **5.8** (Epic Games Launcher) and, to compile the C++, **Visual Studio 2026**
with the "Game development with C++" workload. Node.js is only needed to sync data.

From PowerShell in this folder (`action-rpg\unreal`):

```powershell
.\Tools\rpg.ps1 build                 # compile the C++ (after any code change; ~30s incremental)
.\Tools\rpg.ps1 run                   # play in a window — starts at character select
.\Tools\rpg.ps1 run -Class mage -Sex female    # skip character select
.\Tools\rpg.ps1 editor                # open in the Unreal Editor (then press Play)
```

If you edit `../data/game-data.json` (tuning, dialogue, quests, map...), run `.\Tools\rpg.ps1 sync` to copy it
into this project and into the HTML prototype. Then restart the game; no rebuild is needed for data changes.

First launch compiles shaders and takes a minute or two. Later launches take seconds.

## Controls

| Key | Action |
|---|---|
| WASD / mouse | Move / look and aim (third-person camera) |
| Left click | Attack (hold to keep swinging). Mage: arcane bolt |
| Right click (hold) | Block (Knight sword & shield, Scholar) / draw bow, release to fire (Thief) |
| Space | Jump |
| Left Shift | Dodge roll (invulnerable, costs stamina) |
| 1–4 | Class abilities (unlock at levels 1–4) |
| Q / E / X | Drink potion / talk / Knight: switch Sword & Shield ↔ Longsword |
| I / C / J / H | Inventory / character / quests / help |
| ~ | Debug: shows dialogue odds and enemy AI states; then K = level up, G = +100 gold |

## Testing without playing

The game can drive itself through scripted scenarios and report what happened (results print to the console and
`Saved/Logs/ActionRPG.log`). These use the real animation, AI, combat and dialogue code:

```powershell
.\Tools\rpg.ps1 test -Scenario combat   # knight kills a slime: swings, XP, quest progress, loot
.\Tools\rpg.ps1 test -Scenario bridge   # Brask stops you, honor duel, he yields, you get the sabre
.\Tools\rpg.ps1 test -Scenario elder    # talk to Elder Maren, accept the quest (+ screenshot)
.\Tools\rpg.ps1 test -Scenario mage     # arcane bolts vs an archer, mana shield soaking arrows
.\Tools\rpg.ps1 test -Scenario thief    # full-draw arrow + backstab
.\Tools\rpg.ps1 test -Scenario pose -Class mage    # screenshots of the staff cast / thrust (Saved/Screenshots/RPG/pose_*.png)
.\Tools\rpg.ps1 test -Scenario pose -Class thief   # bow on the back, draw, release, dagger on the belt
.\Tools\rpg.ps1 shot -Class knight -Name hero -Cam "2480,2900,130,-6,192"   # screenshot from a fixed camera
```

Test runs open a game window for a few seconds. They ignore keyboard/mouse (`-RPGNoInput`), but the window
may grab focus while it is open.

## How it's built

| Piece | Where | Notes |
|---|---|---|
| Game rules | `Content/Data/game-data.json` | Synced from `../data`. Read at startup by `URPGData` |
| World | `World/RPGWorldBuilder` | Map rows → procedural terrain (grass/gravel/cobble/riverbed sections), river, plank bridge, timber-frame cottages, trees, ruins, campfire. Plus sun, sky atmosphere, real-time sky light, volumetric clouds, height fog, Lumen |
| Characters | `Characters/` | `ARPGCharacterBase` (Manny/Quinn + the template's combat anim blueprint, tint, weapons) → player, enemy (AI state machine), NPC |
| Combat | `Combat/` | Stats, damage pipeline, abilities (12 types), projectiles, inventory, loot, effects |
| Story | `Game/RPGStory` | Flags, quests, dialogue engine (seeded hidden rolls), encounters, factions |
| UI | `UI/` | Slate (HUD, dialogue, character select, panels) + a canvas HUD for world-anchored text |
| Input | `RPGPlayerCharacter` | Enhanced Input actions and mapping context created in code |
| Materials | `Tools/create_materials.py` | Glow, telegraph, fresnel and flash materials are generated headless by Python |

**Assets used:**
- **UE5 mannequins (Manny/Quinn) and the combat montages** come from the Third Person template.
- **Materials and props** come from Starter Content.
- **Sky textures** come from the engine.

Weapons are assembled from simple shapes, defined in `world3d.kits` in the data file.

## Known gaps / next

- **Animations:** every class currently uses the template's unarmed combo, so we still need per-class animations (see the animation plan in `../CHARACTERS.md`).
- **Dodge:** reuses the dash animation; a real roll is needed.
- **Trees:** built from bush foliage on trunks.
- **Enemy pathfinding:** enemies steer directly, with no navmesh yet.
