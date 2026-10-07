# SO FAR — Oathbound handoff

Read this first at the start of a session. It records where the project stands, how it is built, the conventions that matter, and what to do next.
Last updated: 2026-10-06 (HD-2D, front end, day/night, ambient life, shields, parallel tests, screenshots, slim git; Tessera + Loom plugins, phases 1-3).

---

## 1. The game (vision)

- A real-time action RPG in which **dialogue matters as much as combat**.
  - The **HTML top-down prototype** is the test bed for mechanics.
  - The **Unreal Engine 5.8 game** is the real product. **Decision (2026-10-05): it is top-down HD-2D** (pixel-art sprites in a lit 3D world, fixed 3/4 camera, Diablo-style mouse control), not third-person. That keeps animation cost low, matches the prototype's vision/threat design, and the release needs Unreal for saves, difficulty, lighting, fog, day/night, music and a richer UI. `world3d.camera.mode` still switches back to `"third"`.
- **Code-first.** We maximize C++ because Blueprints are hard to check into git. Assets are either built-in (engine, template, Starter Content) or generated from code.
- **Four classes**, each playable as male or female:

  | Class | Weapons | Signature | Dialogue verb |
  |---|---|---|---|
  | Knight | sword + shield, or longsword | block / perfect block | **Honor** |
  | Mage | big staff, arcane bolt | mana shield (damage drains mana before HP) | **Hypnotize** |
  | Thief | daggers + unlimited bow; fast and fragile | backstab, charged bow draw | **Intimidate** |
  | Scholar | dagger + shield | heals | **Persuade** (master negotiator) |

- **Sex:** stats are identical, but NPCs react differently. Perks such as spot enemies, spot loot, brawn and persuasion become **Traits**.
- **Dialogue checks are hidden rolls.** Consequences must carry into later quests.
- **Design docs:** `DESIGN.md` covers the vision, threat sense and territories. `CHARACTERS.md` covers classes, dialogue v2, the consequences rule, ability types and the animation plan.

## 2. Folder layout (`C:\Users\krish\dev\action-rpg`)

| Path | What |
|---|---|
| `play.ps1` | **Root launcher**: "just play it". See §3. A thin wrapper over Tessera's `Tools/Tessera.ps1` |
| `tessera.json` | This game's settings for Tessera's tools: project, data sync, prepare scripts, art build, test scenarios (+ their switches), screenshot list |
| `data/game-data.json` | **Single source of truth** for all gameplay data (classes, enemies, abilities, quests, dialogue, map, `world3d` visuals) |
| `tools/sync-data.js` | Copies the data into `prototype/rpg-prototype.html` and `unreal/Content/Data/game-data.json`. Run it after every data edit (play.ps1 also runs it automatically). |
| `prototype/rpg-prototype.html` | Standalone HTML top-down prototype |
| `unreal/` | The UE 5.8 project (`Oathbound.uproject`, C++ module `Oathbound`; classes keep the `RPG` prefix) |
| `unreal/Plugins/Tessera` | Shared top-down HD-2D framework, **git submodule** (github.com/krishnasHub/tessera). See §7g |
| `unreal/Plugins/Loom` | Shared story engine, **git submodule** (github.com/krishnasHub/loom). See §7g |
| `unreal/Tools/rpg.ps1` | Developer commands (wrapper over Tessera.ps1): build, run, editor, prepare, art, shot, sync, test |
| `unreal/Tools/*.py` | Headless editor scripts, on Tessera's `Tools/unreal/tessera_assets.py`: `create_materials.py` (M_RPG_Glow/Telegraph/Fresnel/Flash), `fix_material_usage.py`, `import_pixel.py` (imports the pixel art; builds M_RPG_Sprite / PixelWorld / NightShade and this game's Minimap), `audit_content.py` (which Content assets the game uses; writes `Content/.gitignore`) |
| `tools/pixelart/` | Python + numpy (no PIL), on Tessera's `Tools/pixelart/tspixel` (PNG, colours, drawing, sheet layout; `tessera_path.py` puts it on the path): `characters.py` (sprite sheets, portraits), `environment.py` (textures, props), `ui_art.py` (UI, minimap frames, fades), `extras.py` (cursors, backdrops, effects, ambient creatures), `build_all.py` (writes `unreal/ImportSource/Pixel/`) |
| `tools/screenshots.ps1` | Retakes the README gallery (`docs/screenshots/*.jpg`, list in `tessera.json`) in parallel; `-Only <part of a name>` |
| `lookdev/` | Look comparisons (`index.html`: 3D / HD-2D / flat 2D; `crisp.html`: fog vs tilt-shift). Local only, not in git |
| `Dist/Windows/Oathbound.exe` | Packaged standalone game (rebuilt 2026-10-06) |
| `README.md`, `unreal/README.md` | Controls, how to run, architecture |
| `TODO.md` | The agreed plan as a phase-by-phase checklist (see §8) |

The project is in git (`main`, **github.com/krishnasHub/oathbound**; the game is Oathbound inside and out: project, module, exe), with LFS for uasset/umap/fbx/png/wav. **Git holds only
what a new machine needs to build and play** (checked 2026-10-06 after the plugin split: `git clone --recurse-submodules` of oathbound + tessera + loom into an empty folder (~196 MB, 17 s), then `play.ps1 -Test`: clean build 42 s, all 11 tests PASS; `rpg.ps1 art` regenerates all 101 pixel images identical to git).
Kept on disk but ignored: `Dist`, `Binaries`, `Intermediate`, `Saved`, `lookdev/`, and the Content assets listed in
`unreal/Content/.gitignore` (generated by `unreal/Tools/audit_content.py`: template assets nothing loads, plus the
Starter Content materials that HD-2D swaps for pixel art, which only the optional 3D look shows). Docs images are JPEGs,
not LFS PNGs. GitHub's free LFS quota is 1 GB.

## 3. How to run and test

```powershell
cd C:\Users\krish\dev\action-rpg
.\play.ps1                      # full-screen game, character select
.\play.ps1 -Windowed -Class thief -Sex female   # skip character select
.\play.ps1 -Test                # all self-test scenarios in parallel (sized to this PC; -Parallel N) -> PASS/FAIL table
.\play.ps1 -Test -Scenario walk  # one (or a few: walk,click), with its log lines
.\play.ps1 -Package             # build Dist\Windows\Oathbound.exe (then launches it; -NoLaunch: just build)
.\play.ps1 -Rebuild             # force C++ rebuild
```
`play.ps1` finds UE 5.8 automatically, syncs data if it is newer, rebuilds C++ if the source is newer than the DLL, and creates the materials if they are missing.

Developer loop (from `unreal/`):
```powershell
.\Tools\rpg.ps1 build
.\Tools\rpg.ps1 art               # redraw the pixel art (python3 + numpy), then: prepare
.\Tools\rpg.ps1 test -Scenario combat|block|elder|bridge|mage|thief|walk|picker|smoke|pause|click|frost|barrier|night|scars|ghost
.\Tools\rpg.ps1 test -Scenario pose -Class mage|thief   # pose screenshots -> Saved/Screenshots/RPG/pose_*.png
```
- **Last state (2026-10-07):** all 16 tests PASS (combat, block, elder, bridge, mage, thief, walk, picker, smoke, pause, click, frost, barrier, night, scars, ghost). (Phase 2 also passed from a fresh clone with a clean build.)
  - `click` sends real mouse events; with 8 windows overlapping it sometimes hit another window. Fixed in phase 4: `ATSTestRunner::ClickAt` brings its window to the front (3 full runs in a row passed).
  - `prepare` can't overwrite the committed assets while they're read-only (Git LFS "lockable"): clear the flag first. To keep
    only new assets, restore the rest with `git checkout -- unreal/Content/RPG` (not all of `unreal/Content`: that also
    reverts the synced `Data/game-data.json`; if it happens, rerun `node tools/sync-data.js`).
- **Self-test command-line flags:**
  - `-RPGTest=<scenario>`, `-RPGClass`, `-RPGSex`
  - `-RPGNoInput`: use it in automated runs, otherwise keyboard typing leaks into the game window.
  - `-RPGShot=<sec> -RPGShotName=`
  - `-RPGCam=x,y,z,pitch,yaw`
  - `-RPGProbe`: logs bone axes and material parameters.
  - `-RPGQuitAfter=`
  - `-RPGLook=hd2d|flat2d|mesh3d`, `-RPGSun=pitch,yaw,lux[,r,g,b]` (time of day)
  - `-RPGAutoBegin` (title -> character select at 2 s -> Begin at 6 s), `-RPGAutoSelect[=class]` (go to character select at 2 s and stay, for screenshots)

**Controls:**

Top-down (default):

| Input | Action |
|---|---|
| LMB on ground | walk there along the navmesh (hold: follow the cursor) |
| LMB on enemy (even a neutral one) | fight: walk into range and attack (hold: keep attacking). Hitting a neutral faction member turns the faction hostile. |
| LMB on villager | walk up and talk (villagers can't be attacked yet) |
| E | on someone: walk up and talk. Elsewhere: toggle talk mode (hand cursor), and the next click on a character talks. Foes talk only while their faction is neutral (mid-fight needs Silver Words); mindless ones can't. E or RMB cancels. |
| Shift + LMB | attack in place toward the cursor |
| Shift + wheel | ability picker: slow motion (`camera.abilityPicker.timeScale`), scroll to choose, release Shift or click to cast at the cursor, RMB cancels |
| RMB | block / draw bow (aims at the cursor) |
| Space | dodge (toward the cursor, or along the current path) |
| Mouse wheel | zoom, limited to `camera.topdown.minArm`–`maxArm` |
| 1–4 | abilities (aim at the cursor) |
| WASD | optional screen-relative movement; cancels a click-move (also the future gamepad-stick path) |
| Q / I, J | potion / panels |
| Dialogue | 1–9 picks; wheel, ↑↓ or W/S highlight; Enter, Space or E confirms; Esc closes |

Third-person (`mode: "third"`): WASD move, mouse look, LMB attack, Space jump, Left Shift dodge.

## 4. Environment facts (hard-won)

- UE 5.8.2 is at `C:\Program Files\Epic Games\UE_5.8`. Visual Studio 2026 is pinned in the Target.cs files. UBT warns about the banned MSVC 14.39 toolchain, but the build still succeeds.
- **Python writes CRLF on Windows** unless files are opened with `newline='\n'` (or `newline=''` when reading). CRLF breaks `tools/sync-data.js` (its regex expects `\n`) and bloats diffs. Repo text files are LF.
- The **`python` command on PATH is the Windows Store stub, which hangs.** Use **node** for scripts. Write scripts to the scratchpad instead of using `node -e` with quotes.
- **A clean build is a unity build:** several .cpp files are compiled as one, so names in anonymous namespaces must be unique across files (C4459 / C2374 otherwise). Incremental builds don't show this; a fresh clone does. Shared constants go in a named namespace in a header (e.g. `RPGSpriteSheet` in `RPGSprite.h`).
- UE 5.8 `FJsonObject` keys are `UE::TSharedString`, so convert with `FString(*KV.Key)`.
- **World subsystems initialize before the world has its game instance**, and also for transient editor worlds:
  read game-instance data in `OnWorldBeginPlay`, and limit game-only subsystems with `DoesSupportWorldType`.
- Every plugin module needs its `IMPLEMENT_MODULE` (otherwise "module could not be initialized" at startup), and
  a module that calls `FJsonObject` itself must list `Json` in its own Build.cs.
- Windows PowerShell 5.1 `Set-Content -Encoding utf8` writes a BOM; edit files with node or the editor tools.
- Template content comes from the TP_ThirdPerson Combat variant: SKM_Manny_Simple / SKM_Quinn_Simple, ABP_Manny_Combat, AM_ComboAttack (Melee01–03), AM_ChargedAttack and NS_Damage. Starter Content was copied from `D:\UInreal_Projects\Test1`.
  - CoreRedirects: `/Script/TP_ThirdPerson` → `/Script/TesseraGameplay` (the template's anim notifies map to Tessera's classes), and `/Script/ActionRPG` → `/Script/Oathbound` (the module's old name).
- Material parameters:
  - The mannequin tint parameter is **"Paint Tint"**.
  - BasicShapeMaterial uses **"Color"**.
- Starter materials needed the instanced-static-mesh usage flag. `fix_material_usage.py` sets it.
- Collision: **Visibility** traces ignore InvisibleWall and Pawn. Line of sight and projectiles use `ECC_Visibility`.
- Enemies need `AutoPossessAI = PlacedInWorldOrSpawned` with an AAIController, otherwise CharacterMovement won't move them.

## 5. Unreal architecture (C++, `unreal/Source/Oathbound/`)

- **Boot:** the game starts on `/Engine/Maps/Entry`. `ARPGWorldBuilder` generates the whole world at runtime:
  - procedural terrain and river
  - HISM trees
  - cube-built houses with gable roofs, the bridge and ruins
  - SkyAtmosphere, SkyLight, VolumetricCloud, fog and PostProcess
  - Lumen and virtual shadow maps
- **Units:** gameplay data is in prototype pixels, converted with `world3d.unitsPerPx = 3.5`. Tiles are 300 uu.
- **Plugins (§7g):** Tessera gives `TSJson`, `UTSData` (data, map, `RegionAt`, `Px()`), `TSLook`, `TSAssets`,
  `ATSWorldBuilder`, `ATSSky`; Loom gives `ULMStory`.
- **Core/:**
  - `RPGAssets`: this game's asset paths (mannequins, montages, `StarterMat()`, `StarterProp()`).
- **Combat/:**
  - Stats, tags and the `RPGCombat::Deal` pipeline.
  - Projectile, loot, FX.
  - `URPGAbilityComponent`: 12 ability types.
  - Inventory.
  - All ported 1:1 from the JS prototype.
- **Game/:**
  - `RPGGameMode`: spawning, self-test flags, probe.
  - `RPGPlayerController`.
  - `URPGSession`: this game's glue to Loom (conditions, actions, placeholders, verb costs, quest rewards), duels,
    the bridge-crossing rule, floaters / toasts / shake.
  - `ARPGSelfTest`: scenarios.
- **UI/:** Slate widgets (`SRPGHud`, `SRPGCharSelect`, `SRPGPanel`, `SRPGMinimap`) plus the `ARPGHUD` canvas for world text and markers,
  on Tessera's UI kit (dialogue box, title, pause menu, cursor, night shade, toasts, ability picker, threat arrows).
  - The dialogue box reads Loom through `URPGSession::DialogueView()` (an `FTSDialogueView`).
  - Dialogue uses `FInputModeUIOnly` with focus reclaim. This fixed the freeze when pressing 1 at Brask's surrender.
- **Characters/:**
  - `ARPGCharacterBase`
    - Weapon kits built from data: `SetWeaponKits`, holsters, `SetKitHolstered`.
    - Glow parts with a point light: `KitGlow`, `SetKitGlow` for the pulse.
    - Part ids: `KitPart(kit, id)`. Kit roots: `KitRoot`.
  - `ARPGPlayerCharacter`, `ARPGEnemy`, `ARPGNPC`.
  - **`URPGPoseMesh`** (the procedural animation layer, see §6).

## 6. Animation approach

> **Now (HD-2D):** what you see is a sprite (`URPGSpriteComponent`, frames chosen from the character's state). The
> mannequin and the pose layer below still run underneath, hidden, so montage-driven hit timing is unchanged.

The (hidden) 3D layer:
- The real animated mesh, running ABP_Manny_Combat, is **hidden**.
- **`URPGPoseMesh`**, a poseable mesh, copies that pose every frame in `TG_PostUpdateWork`, then eases selected bones toward named poses.
  - Several poses can be active at once, and later poses override earlier ones.
  - Each bone eases independently, so poses blend smoothly.
- Weapons attach to the PoseMesh (`BodyMesh()`), so they follow the posed arms. `OnPosed` runs right after the pose is applied; it updates the drawn bowstring and nocked arrow.
- **Bone conventions** (measured with `-RPGProbe`):
  - **Left-arm bones:** X points toward the hand.
  - **Right-arm bones:** X points back toward the shoulder, so targets are the negated limb direction.
  - **Spine and pelvis:** X up, Y forward, Z right.
  - The staff runs along `hand_r`'s X axis.
- **Poses** are defined in the `ARPGPlayerCharacter` constructor:

  | Pose | Used for |
  |---|---|
  | `guard` | shield arm raised (Knight / Scholar block) |
  | `cast`, `cast_kick` | Mage holds the staff up, then thrusts the orb at the target (on bolts and spell abilities) |
  | `bow_aim`, `bow_draw`, `bow_release` | Thief bow arm out, string hand to the cheek, snap back |

- **Timers** live in `UpdatePoses()`: `CastKick`, `CastHold`, `OrbFlash`, `BowRelease`, `BowOut`.
- **Thief holsters:**
  - While the bow is out: dagger on the right hip, bow in hand.
  - About 1.2 s after the last shot, or on a dagger swing: bow goes on the back, dagger back in hand.
- **Shots leave from `Muzzle()`:** the staff orb (Mage), the bow (Thief) or the chest (everyone else).
- Weapon and holster geometry is all data, in `world3d.kits`, `mounts`, `styleKits` and `enemyKits` in game-data.json.

## 7. Done so far (high level)

- **HTML prototype:**
  - Character select with stats.
  - Class weapons, ranged vs melee, abilities.
  - Toll Bridge quest with class-specific dialogue.
  - Soft vision cone with a greyed-out map.
  - Enemy threat sense.
  - Regions: south-bank enemies only engage once you cross the river.
- **Unreal:** everything above ported, plus:
  - Third-person camera, jump, dodge.
  - Character select.
  - Elder Maren quest giver.
  - Captain Brask honor duel. He yields, you get the sabre, and the bandits leave.
- **Fixes and polish:**
  - Block now raises the arm with the shield, instead of the old white aura.
  - Enemy AI possession fixed.
  - Dialogue freeze fixed.
- **Latest session:**
  - Mage staff cast and thrust animation, bolts from the orb, orb pulse.
  - Thief bow draw/release with the string pulled to the hand and an arrow nocked.
  - Dagger on the belt while bowing, bow on the back while using daggers.
  - Exe repackaged.

## 7b. Look test (2026-10-05): 3D mesh vs HD-2D vs flat 2D

Same game, three ways to draw it. `world3d.look` or `-RPGLook=hd2d|flat2d|mesh3d` on any run.

**Decision (2026-10-05): HD-2D is the game's look** (`world3d.look = "hd2d"`, the default). The user likes the procedural sprites; iterate on them slowly rather than replacing them. Sprite rows include a guard frame (row 12, cols 1-3) for shields. Character select frames the sprite head-on; tilt-shift DOF is on the gameplay camera only.
- **Art pipeline:** `tools/pixelart/build_all.py` (Python + numpy, no PIL) draws all sprites, textures, props and the baked map into `unreal/ImportSource/Pixel/`. `unreal/Tools/import_pixel.py` imports them (nearest filter, no mips) and builds `M_RPG_Sprite` (flipbook card, world-up normal, flip/tint/flash) and `M_RPG_PixelWorld` (world-projected 32 px textures). It is part of `prepare` and play.ps1's first-run step.
- **Code:** Tessera `TSLook` (mode, card rotation, pixel material swap: `RPGAssets::StarterMat` returns pixel versions in HD-2D), `Characters/RPGSprite` (picks direction/action/frame from character state; the 3D body stays hidden but keeps animating, so montage hit timing is unchanged), `ARPGWorldBuilder::BuildFlat2D` and the tree/bush cards in `BuildTreesAndScatter`.
- **Lessons:** in flat 2D, hidden 3D geometry must also leave lighting (distance-field shadows). Ortho plus Lumen/SSAO smears dark bands, so GI and AO are off there. Same-depth cards z-fight, so a tiny x tie-break is added. HD-2D depth of field needs a big virtual sensor (400 mm) to show at this distance.
- **Results:** `lookdev/index.html` (local only; 15 side-by-side screenshots + a live sprite animation player). Sun override for time of day: `-RPGSun=pitch,yaw,lux,r,g,b`.
- **Open in HD-2D:** no cast/bow-specific frames (casting uses the attack row, drawing the bow holds the wind-up frame); the mana shield, guard arc and aim line are still 3D effects; weapon-style swaps (Knight longsword) don't change the sprite. Flat 2D: clicking characters aims at the actor, not the drawn sprite.

## 7c. Front end (2026-10-05)

- **Boot -> title screen** (`SRPGTitle`): game name + tagline from `data.title`, Start New Game / Quit, over a slow drifting camera across the village (same angle and tilt-shift as the game). The hero is hidden until Begin; the canvas HUD is off on both screens.
- **Character select** (`SRPGCharSelect`, full screen): the hero big on a grass stage at the left, looping through every animation with captions (idle, walking 4 ways, primary attack, shield block / bow / mana shield, each ability with a small effect: `FX_Orb`, `FX_Arrow`, `FX_Ring`, `FX_Heal`); the right side has class cards with live portraits, sex, stats, kit icons (`ICO_*`), dialogue style, abilities (the one being shown lights up), Back / Begin. Esc goes back to the title.
- **Pause menu New Game** goes straight to character select (`?RPGNewGame`). `-RPGClass=` (tests) skips both screens.
- **Bug fixed on the way:** `AddBox` cached the engine cube in a `static`; a New Game reload could unload it (crash). Don't cache engine assets in statics without `AddToRoot`.

## 7d. Minimap, day/night, art pass (2026-10-05)

- **Camera:** HD-2D arm 5300 (zoom 4300-6300).
- **Minimap** (`SRPGMinimap`, bottom right): `MAP_Mini` (a clean baked map: tiles, trees, red roofs) through the UI material `M_RPG_Minimap` (window centred on the hero, cut by `MM_Mask_<shape>`), dots for foes / villagers / quest givers, and a class frame `MM_Frame_<shape>`: orb (Mage), shield (Knight), coin (Thief), book (Scholar). The HUD clock sits above it.
- **Day/night** (`world3d.dayNight`, Tessera `ATSSky`): starts 14:00, 30 s per game hour; sun 6-20 (golden from ~17), moon 20-6 (a second atmosphere light), a shadowless fill light (dusk shadows not black), sky fill, volumetric fog thickening toward night (`fog.day/dusk/night`), and a grade: exposure clamped to `exposureMinEV..exposureMaxEV` (otherwise auto exposure turns night back into day), plus cooler / desaturated nights and warm dusks. The hero carries a soft warm `NightGlow` after dark. The main forward-shading light switches between sun and moon. `-RPGHour=` starts at an hour; `-RPGSun=` freezes the sun.
- **Characters at 2x** (`tools/pixelart/characters.py`: drawn in 32-unit coordinates, sampled at `RES = 2`, so 64x64 frames): finer outlines and rims, catch-lights, and clearly different men and women (`fem`: slimmer, lashes, lips, long hair out from under helmets / hats / hoods via `longHair`, their own colours; men: brows, stubble, broader build).

## 7e. Polish pass (2026-10-06)

- **Game cursor** (`SRPGCursor`, drawn above all UI; OS cursor hidden in play, `EMouseCursor::None`): `ARPGPlayerCharacter::CursorIcon()` picks `CUR_sword/dagger/wand/arrow` when a click would attack (hovering a foe, Shift held; arrow while the bow is drawn), `talk` over a villager or in talk mode (E), `talk_off` (greyed, slashed) in talk mode with nobody talkable under it, else `pointer`.
- **Character-select backdrops** (`SRPGBackdrop`, behind the hero, clipped to the left column, anchored to the hero's feet via `GetPaintSpaceGeometry`, not `GetCachedGeometry`, which is desktop space): 14 s loops per class. Knight: castle at dawn, rays, petals, glints. Thief: rooftops, ninjas, chest + coins. Mage: sunset storm, lightning, runes, sparks, orbs, and an easter-egg dark four-horned tower in a ring of stone. Scholar: library sunbeam, dust, book opening, pages.
- **Ambient life** (`World/RPGAmbient`): by day bird flocks (take off within 650 uu, resettle out of sight) and geese (honk + flee within 380 uu); by night a hooded prowler on a route round the cottages (startles at 700 uu, bolts, reappears later) and fireflies that drift away from the hero.
- **No auto-conversations:** encounters no longer open dialogue when you approach (you choose E + click, or fight). Slipping past still turns the faction hostile.
- **Conversation backdrop:** the world darkens in a soft rectangle centred on the dialogue box (`UI_FadeRadial`, drawn in `SRPGDialogue::OnPaint`).
- **Parallel tests:** `play.ps1 -Test` runs scenarios as separate processes with their own logs (`Saved/Logs/Tests/<name>.log`), as many at once as CPU cores / free RAM / video memory allow (~3 cores, 2.5 GB, 2 GB each); all 11 take ~40 s instead of ~5 min.
- **Dialogue portraits:** built (`POR_<sheet>`, `characters.portrait()` at 16 texels/unit) but **off** (`world3d.dialoguePortraits = false`) until there's proper illustrated art.
- **Look:** tilt-shift off (`hd2dCamera.tiltShift`), fog back on; deep-night overlay is true black, with a cool moonlit glow around the hero. Camera arm 6600 (zoom 5400-7800).

## 7f. Screenshots and a lean repo (2026-10-06)

- **Gallery:** 11 JPEG shots in `docs/screenshots/`, shown at the top of `README.md`; `tools/screenshots.ps1` retakes them (parallel, ~1 min). The dialogue shot reuses the `elder` test's own screenshot.
- **Content audit:** `unreal/Tools/audit_content.py` walks the asset registry from every `/Game/` path the code and data load (its `ROOTS`, kept in step with `Source/`) and writes `unreal/Content/.gitignore` for everything else. Git now has ~174 MB of Content instead of ~750 MB. If code starts using a template asset, add it to `ROOTS`, rerun, `git add` it.
- **Clean-build fix:** sprite sheet constants clashed in the unity build (see §4); now `RPGSpriteSheet` in `RPGSprite.h`.
- **History rewritten locally** (filter-branch) without the ignored files: LFS referenced by history went from ~790 MB to 173 MB. Backup: `.git/backup-before-slim.bundle` (old refs also under `refs/original`).
- Mage tagline no longer mentions the old mana shield.

## 7g. Shared plugins: Tessera and Loom (2026-10-06)

More games are planned (sci-fi, stealth, faster action); they share the top-down HD-2D view, the mechanics and
dynamic story. So the reusable code is moving into two plugins, each its own repo, used here as git submodules:

- **Loom** (`LM` prefix, engine-only dependencies): `ULMStory`: dialogue nodes, text variants, conditions, actions,
  hidden seeded checks (formula in `CheckRules`), flags, disposition, factions, quests, encounter outcomes. Games
  register conditions / actions / placeholders / verb rules and listen to events (`OnQuest`, `OnDialogueOpened`...).
- **Tessera** (`TS` prefix): `TesseraCore` (`UTSData`, `TSLook`, `TSAssets`, `TSJson`, `TSCmd`, `TSConfig`) and
  `TesseraWorld` (`ATSWorldBuilder`: helpers, height field, cutaways, flicker, navmesh; `ATSSky`: sky and day/night),
  `TesseraGameplay` (characters, stats, combat, abilities, items, loot, feedback, `TSPerception`),
  `TesseraHero` (`UTSCameraRig`, `UTSHeroControl`), `TesseraUI` (Slate kit, `TSHUDDraw`).
- **Rule (the user's tenet):** the plugins are a framework / library, as generic and abstract as possible, with no
  game logic. Nothing in either plugin may name a game. File names, sections, map symbols, material paths, switch
  prefixes, data keys of the game's own (e.g. `enemyVision` is passed in), wording and colours come from the game's
  data, `[Tessera]` config, `FTSUIStyle` or registered hooks.
- **Phases:** 1 done (Loom; Tessera core + world). 2 done, tested and pushed (TesseraGameplay: `ATSCharacter`,
  data-defined stats pools in the `stats` section, `TSCombat`, `UTSAbilityComponent` + type registry, projectiles, FX,
  inventory, loot, `UTSFeedback`, sprite, pose mesh, anim notifies; game keeps looks, class rules, duels, quest drops via
  hooks; `world3d.assets` / `.text` / `.currencyKey` hold paths and words; the unused mana-shield code was dropped).
  Each phase: tests + package, the user tests the exe, then commit + push the plugins.
  3 done, tested and pushed:
  - `TSPerception`: senses (cone, hearing, line of sight), "Hidden" stealth, threat sense; `ATSCharacter::IsHunting`.
    Enemies keep their territory rule.
  - `UTSCameraRig`: the camera set-up, zoom, tilt-shift focus and shake that were in the player.
  - `UTSHeroControl`: cursor picking, aim assist, click-to-move / attack / talk, talk mode, ability picker. The player
    supplies the rules as hooks (`InAttackRange`, `Attack`, `TalkBlocker`, `Talk`) and still owns its keys, attacks, bow and guard.
  - TesseraUI: `STSDialogueBox` (+ `FTSDialogueView`), `STSTitle`, `STSPauseMenu`, `STSCursor`, `STSNightShade`,
    `STSToasts`, `STSAbilityPicker`, `FTSChoose`, `FTSUIStyle` / `TSUI`, `TSHUDDraw`. New data: `world3d.assets`
    { dialogueFade, titleFade, nightShade }, `world3d.text` { progressLost, pickerHint }, `title.footer`.
  - Still in the game: HUD layout, minimap (class-shaped frames), character select, panels, ability bar.
  4 done, tested and pushed (tools in any language are fine in a plugin, but never Unreal assets):
  - `TesseraTest`: `ATSTestRunner` (the game's `ARPGSelfTest` derives: `RunStep`, `Report`, `Quit`, `Shot`, `ClickAt`) and
    `TSTestSwitches` (`-RPGTest/Shot/ShotName/Cam/QuitAfter`; the game mode keeps `-RPGProbe`, `-RPGLowHP`).
  - `Tools/Tessera.ps1`: the launcher (UE discovery, data sync, change-detecting build, first-run prepare, parallel
    tests, package, shots, art); prefix and shot folder from `DefaultGame.ini [Tessera]`, the rest from `tessera.json`.
  - `Tools/unreal/tessera_assets.py`: material builders + texture import; regenerated assets passed all tests.
  - `Tools/pixelart/tspixel`: PNG, colour, canvas helpers, sheet layout; the game's 101 images regenerate byte-identical.
  - Still in the game: its art definitions, minimap material, `audit_content.py` (lists this game's template assets), `sync-data.js` (inlines the HTML prototype).

## 7h. Mage barrier and Frost Nova (2026-10-07)

- **Barrier** (hold RMB): a glowing column around the mage, as wide as `keepOut` (50 px = 175 uu) on the staff's guard.
  Anyone inside when it goes up is thrown clear (knockback + a short stagger); nobody gets in while it's up (held at the
  edge after all movement each frame: Tessera's `UTSKeepOut`, post-physics). Animals: `UTSAreaEvents::OnPush` -> birds
  burst into the air, geese and the prowler are thrown, then run. Trees and walls don't react.
- **Frost Nova**: a light-blue sphere swells and fades (`ATSFX::Sphere`), the ground stays frosted for 3 s (`ATSFX::Stain`),
  and every character in it (foes, villagers) gets `Frozen` for 3 s: no movement, AI, animation or talk, tinted
  `world3d.statusTints.Frozen`. Animals freeze through `UTSAreaEvents::OnStatus`. (Replaced its old 40% slow.)
- Mechanisms in Tessera; numbers, colours and the column's look in Oathbound. Tests: `frost`, `barrier`.
- **Ground scars** (`ATSFX::Scar`, an ability's `scar` data): Frost Nova cracks (after the frost fades), Fireball scorch
  (burnt splat, streaks, dying embers), Chain Lightning forks (burn lines + flash) under each target. Each a new random
  pattern, a random lifetime, and a chance of none. Test: `scars`.
- **Night orb**: the mage's staff orb glows faint white-blue as night falls and widens the hero's sight in the dark
  (1000 -> 1450 uu). Event-driven: Tessera's `UTSDayNight` publishes `OnPhase` / `OnHour` / `OnNightLevel`; the hero
  (game code) listens and drives the orb light and `ATSSky::SetCarriedLight`; numbers on the orb's `nightLight` in data.
  Test: `night` (-RPGHour=23).
- **Death ghosts**: Tessera announces deaths (`UTSCharacterEvents::OnDied`); the session spawns `ARPGGhost` for enemies: a
  see-through pixel ghost (`SPR_Ghost`, `M_RPG_SpriteSeeThrough`) rises from the body, wiggles and fades (`world3d.deathGhost`).
  If it was the last foe around (none within `scaredVicinity`) it sometimes (`scaredChance` 0.5) gets a fright: a "!", a
  scared face whose eyes follow the hero (5 directions + mirror), then it flees the other way. Tests: `combat`, `ghost`
  (`-RPGGhostScared`).
- **Architecture rule (user, 2026-10-07):** decoupled and event-driven. Tessera / Loom publish events and offer hooks;
  the game listens and acts. Loom and Tessera never know about each other: the game bridges them (e.g. time of day to Loom).

## 7i. The Scholar's routes and the world's mood (2026-10-07, phases 1-6 of TODO.md; done, played by the user)

- **Languages** (`languages`, `classes.<id>.languages`, `enemies.<id>.speaks`): Common for everyone, the Old Tongue (Ruin
  Brute: Mage, Scholar), Grave-speech (skeletons: Scholar); slimes speak none. A foe that speaks your tongue and isn't
  provoked waits to be talked to; the hover says why when you can't ("It speaks Grave-speech. You don't.").
- **Loom**: level-scaled checks (`check.level { min, per }`, `tooLow` node; a forced test roll can't beat the floor); a
  hidden **world mood** (`mood { start, min, max, bands }`, `OnMood` / `OnMoodBand`, `{"mood": n}` action and condition);
  `{price:N}` text with a game hook.
- **Mood sources** (tuning): talking down / pacifying +3, quests +2..4, killing anyone who could talk -5, striking a
  faction mid-talk -10; slimes 0. The world shows a new band only at the next dawn or dusk (Tessera `UTSDayNight`).
- **What the mood looks like** (all cosmetic, nothing to fight, everything flees or vanishes near the hero):
  Tessera `ATSAmbientLife` (children, butterflies, geese, puppies; grumpy men by day; wolves, bats, snakes by night;
  `moodLife` weights per band; walkers never step into water or walls) and `ATSDressing` (cracked roads, wall cracks
  and moss when it's bad; vines and sunrays when it's good; `moodDressing` counts). Tobin's prices (`moodPrices`) and
  Maren's and Tobin's tone follow it too.
- **Skeletons**: lone ones each want something (`wants`: the ferryman's toll, a riddle, a song); Aldric the Grave-Watcher
  rests once the Scholar reads the rites at his unmarked grave (a marker appears; his ghost rises, calm); Captain
  Ossric's Bonewardens stand down for his lost signet. Tessera `ATSInteractable`: things you walk up to and use (click
  or E): graves, lost items, doors.
- **The Ruin Brute** lives in **the old mine**, a small map of its own (Tessera map areas `map.areas`, built off to the
  side, its own navmesh; two-way doors with a fade; the sky goes "indoors" dark there). Routes: food (Maren's slime cull
  -> bread), Mage hypnosis (level 3+), Scholar teaches it a trade (level 4+): it becomes **Grot the Miner** and walks
  its rounds (Tessera `UTSRoutine`, doors between areas), carrying gold from the mine to the village.
- **Proof runs and tours**: `pacifist`, `spree` (and redemption), and a play-through per hero (`tour_*`).
- **Lessons**: Unreal's spawn-letter map is case-insensitive (never reuse a letter in another case); flat sprite cards
  need roomier bounds or the occlusion culler can lose them; character cards stand upright (stretched by 1/cos pitch)
  so they don't lean into walls; walls in a top-down cave must stay low.
- Tests now 30 scenarios, run at most 4 at a time (`tessera.json maxParallel`).

## 8. Next steps / open threads

**The agreed plan (2026-10-07): talking foes, the Scholar's routes, and world mood.** Checklist by phase: `TODO.md`.
Built one phase at a time; after each, tests + a package for the user; nothing is committed until all phases are done
and tested.

- **Languages:** each creature speaks a language, each class knows some; the Scholar talks to everything that has one
  (slimes have none). Common (bandits, Brask, Wren, villagers: all) · Old Tongue (Ruin Brute: Mage, Scholar) ·
  Grave-speech (skeletons: Scholar). More as the world grows. A foe that speaks your language waits instead of attacking.
- **Level-scaled persuasion:** checks get a minimum level and a per-level bonus; below the minimum they can't succeed
  (a low-level Mage won't change Brask's mind, a Scholar won't convince him, a Knight's challenge is laughed off). Retry
  after levelling up, or fight.
- **Peaceful wins pay** at least the kill XP; talked-down foes don't respawn.
- **Scholar routes:** lone skeletons (random simple wants + hand-written fun ones: find, read the rites over and mark an
  unmarked grave); a skeleton band's leader wants a lost item; the Ruin Brute (moved to a cave) holds the relic and wants
  food, which Elder Maren gives for a kill-5-slimes quest; at high level the Scholar teaches it a trade and it then walks
  between its mine and the village carrying gold for food. Other classes get world-changing routes later.
- **World mood:** hidden, starts at 0; up with peaceful outcomes, down with killing talkable creatures and betrayal;
  slimes neutral; redemption possible but slow. Shown only cosmetically and subtly, applied at dawn / dusk: good =
  children playing, geese, butterflies, puppies; bad = fewer children, grumpy men by day, puddles and building wear,
  wolves / bats / snakes at night. None of it can be fought; it flees or vanishes near the hero.
- **Where:** Loom = mood value + level checks; Tessera = ambient life, building wear, interactables, NPC routines;
  Oathbound = languages, wants, rewards, mood weights, art, and the code bridging Loom's events to Tessera.

Older open threads:

**GitHub reset: done 2026-10-06.** The old action-rpg repo was deleted; the slimmed history went to the new
github.com/krishnasHub/oathbound (~196 MB instead of ~790 MB). Then renamed
inside too (2026-10-06): `Oathbound.uproject`, module `Oathbound` (`OATHBOUND_API`), targets, `Oathbound.exe`. The `RPG` class
prefix, `LogRPG`, the `/Game/RPG` content folder and the `-RPG...` switches stay. The local folder is still `action-rpg`.


0. **Top-down follow-ups** (spike done 2026-10-05; `walk` test covers click-to-move/talk/attack):
   - Fog of war / vision cone as a post-process (the prototype's signature mechanic).
   - Hover highlight (outline) on characters; a ground decal instead of the HUD ring for the move target.
   - Enemies onto the navmesh (`ATSWorldBuilder` builds it at runtime; enemies still steer directly).
   - Dialogue presentation: camera push-in; portraits are built but off until there's real illustrated art.
   - Open question: should villagers be attackable? (A default click on a villager currently talks.)
   - Saves and load (title has New Game / Quit only), difficulty settings, music.
   - Sprite gaps: cast and bow frames, Knight longsword sprite, block/hit frames for enemies.
   - Check: in the `thief` test the dagger backstab shows 0 floaters and the dagger may hit a different slime than the arrow did. Probably a test-setup issue; not investigated.
1. **Real animations** (superseded by HD-2D sprites; kept for reference).
   - GASP (Game Animation Sample, free on Fab, has a UE 5.8 version) covers locomotion and traversal only.
   - Combat animations (sword, shield block, bow, cast) need Mixamo or Fab packs downloaded with the user's account.
   - Once those are on disk, build an import + retarget pipeline. The procedural poses then become a fallback.
2. **Scholar pass.** Same treatment as Mage and Thief: dagger + shield guard, heal cast pose and FX.
3. **Knight polish.** Longsword stance; perfect-block feedback.
4. **Trait pick** at character select (spot enemies, spot loot, brawn, persuasion).
5. **Consequence quests.** Follow-ups to the Toll Bridge outcomes (honor / bribe / intimidate / hypnotize), per the consequences rule in `CHARACTERS.md`.
6. ~~Put the project in git~~ (done; slimmed 2026-10-06, see §7f).

## 9. Working agreements with the user

- Keep this project separate from other projects/folders.
- Prefer C++ and data-driven definitions over Blueprints.
- Test-run before reporting. Verify visual changes with screenshots (the `pose` / `block` scenarios, `-RPGShot`).
- Automated runs use `-RPGNoInput` so the user's typing isn't hijacked.
- Keep `play.ps1` the one-command way to play, test and package.
- After each chunk of progress: retake screenshots (`tools/screenshots.ps1`), update the docs (this file, READMEs), commit and push.
- Keep git lean: only what a new machine needs. Unused or 3D-only assets and look-dev comparisons stay local, git-ignored (never deleted).
