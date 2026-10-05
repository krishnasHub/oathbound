# SO FAR — Action RPG handoff

Read this first at the start of a session. It records where the project stands, how it is built, the conventions that matter, and what to do next.
Last updated: 2026-10-05.

---

## 1. The game (vision)

- A real-time action RPG in which **dialogue matters as much as combat**.
  - The **HTML top-down prototype** is the test bed for mechanics.
  - The **Unreal Engine 5.8 third-person game** is the real product.
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
| `play.ps1` | **Root launcher**: "just play it". See §3. |
| `data/game-data.json` | **Single source of truth** for all gameplay data (classes, enemies, abilities, quests, dialogue, map, `world3d` visuals) |
| `tools/sync-data.js` | Copies the data into `prototype/rpg-prototype.html` and `unreal/Content/Data/game-data.json`. Run it after every data edit (play.ps1 also runs it automatically). |
| `prototype/rpg-prototype.html` | Standalone HTML top-down prototype |
| `unreal/` | The UE 5.8 project (`ActionRPG.uproject`) |
| `unreal/Tools/rpg.ps1` | Developer commands: build, run, editor, prepare, shot, sync, test |
| `unreal/Tools/*.py` | Headless editor scripts: `create_materials.py` (M_RPG_Glow/Telegraph/Fresnel/Flash), `fix_material_usage.py` |
| `Dist/Windows/ActionRPG.exe` | Packaged standalone game (about 1.3 GB, rebuilt 2026-10-05 with the latest poses) |
| `README.md`, `unreal/README.md` | Controls, how to run, architecture |

There is no git repo yet. `.gitignore` and `.gitattributes` (LFS for uasset/umap/fbx/png/wav) are ready. `Dist`, `Binaries`, `Intermediate` and `Saved` are ignored.

## 3. How to run and test

```powershell
cd C:\Users\krish\dev\action-rpg
.\play.ps1                      # full-screen game, character select
.\play.ps1 -Windowed -Class thief -Sex female   # skip character select
.\play.ps1 -Test                # 6 self-test scenarios -> PASS/FAIL table
.\play.ps1 -Package             # build Dist\Windows\ActionRPG.exe (then launches it)
.\play.ps1 -Rebuild             # force C++ rebuild
```
`play.ps1` finds UE 5.8 automatically, syncs data if it is newer, rebuilds C++ if the source is newer than the DLL, and creates the materials if they are missing.

Developer loop (from `unreal/`):
```powershell
.\Tools\rpg.ps1 build
.\Tools\rpg.ps1 test -Scenario combat|block|elder|bridge|mage|thief
.\Tools\rpg.ps1 test -Scenario pose -Class mage|thief   # pose screenshots -> Saved/Screenshots/RPG/pose_*.png
```
- **Last state:** all 6 tests PASS and the build succeeds.
- **Self-test command-line flags:**
  - `-RPGTest=<scenario>`, `-RPGClass`, `-RPGSex`
  - `-RPGNoInput`: use it in automated runs, otherwise keyboard typing leaks into the game window.
  - `-RPGShot=<sec> -RPGShotName=`
  - `-RPGCam=x,y,z,pitch,yaw`
  - `-RPGProbe`: logs bone axes and material parameters.
  - `-RPGQuitAfter=`

**Controls:**

| Key | Action |
|---|---|
| WASD | move |
| Mouse | camera / aim |
| LMB | primary attack |
| RMB | block / draw bow |
| Space | jump |
| Left Shift | dodge |
| E | talk |
| 1–4 | abilities |
| Q | potion |
| I, J | panels |

## 4. Environment facts (hard-won)

- UE 5.8.2 is at `C:\Program Files\Epic Games\UE_5.8`. Visual Studio 2026 is pinned in the Target.cs files. UBT warns about the banned MSVC 14.39 toolchain, but the build still succeeds.
- The **`python` command on PATH is the Windows Store stub, which hangs.** Use **node** for scripts. Write scripts to the scratchpad instead of using `node -e` with quotes.
- UE 5.8 `FJsonObject` keys are `UE::TSharedString`, so convert with `FString(*KV.Key)`.
- Template content comes from the TP_ThirdPerson Combat variant: SKM_Manny_Simple / SKM_Quinn_Simple, ABP_Manny_Combat, AM_ComboAttack (Melee01–03), AM_ChargedAttack and NS_Damage. Starter Content was copied from `D:\UInreal_Projects\Test1`.
  - CoreRedirect `/Script/TP_ThirdPerson` → `/Script/ActionRPG`, so the template's anim notifies map to our classes.
- Material parameters:
  - The mannequin tint parameter is **"Paint Tint"**.
  - BasicShapeMaterial uses **"Color"**.
- Starter materials needed the instanced-static-mesh usage flag. `fix_material_usage.py` sets it.
- Collision: **Visibility** traces ignore InvisibleWall and Pawn. Line of sight and projectiles use `ECC_Visibility`.
- Enemies need `AutoPossessAI = PlacedInWorldOrSpawned` with an AAIController, otherwise CharacterMovement won't move them.

## 5. Unreal architecture (C++, `unreal/Source/ActionRPG/`)

- **Boot:** the game starts on `/Engine/Maps/Entry`. `ARPGWorldBuilder` generates the whole world at runtime:
  - procedural terrain and river
  - HISM trees
  - cube-built houses with gable roofs, the bridge and ruins
  - SkyAtmosphere, SkyLight, VolumetricCloud, fog and PostProcess
  - Lumen and virtual shadow maps
- **Units:** gameplay data is in prototype pixels, converted with `world3d.unitsPerPx = 3.5`. Tiles are 300 uu.
- **Core/:**
  - `RPGJson.h`: JSON helpers.
  - `URPGData`: data subsystem, map, `RegionAt`, `Px()`.
  - `RPGAssets`: asset paths, `Shape()`, `StarterMat()`.
- **Combat/:**
  - Stats, tags and the `RPGCombat::Deal` pipeline.
  - Projectile, loot, FX.
  - `URPGAbilityComponent`: 12 ability types.
  - Inventory.
  - All ported 1:1 from the JS prototype.
- **Game/:**
  - `RPGGameMode`: spawning, self-test flags, probe.
  - `RPGPlayerController`.
  - `URPGStory`: flags, quests, dialogue with seeded hidden rolls, encounters, factions, floaters.
  - `ARPGSelfTest`: scenarios.
- **UI/:** Slate widgets (`SRPGHud`, `SRPGDialogue`, `SRPGCharSelect`, `SRPGPanel`) plus the `ARPGHUD` canvas for world text, markers and threat arrows.
  - Dialogue uses `FInputModeUIOnly` with focus reclaim. This fixed the freeze when pressing 1 at Brask's surrender.
- **Characters/:**
  - `ARPGCharacterBase`
    - Weapon kits built from data: `SetWeaponKits`, holsters, `SetKitHolstered`.
    - Glow parts with a point light: `KitGlow`, `SetKitGlow` for the pulse.
    - Part ids: `KitPart(kit, id)`. Kit roots: `KitRoot`.
  - `ARPGPlayerCharacter`, `ARPGEnemy`, `ARPGNPC`.
  - **`URPGPoseMesh`** (the procedural animation layer, see §6).

## 6. Animation approach (current)

There are no imported combat animations yet. Instead:
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

## 8. Next steps / open threads

1. **Real animations.**
   - GASP (Game Animation Sample, free on Fab, has a UE 5.8 version) covers locomotion and traversal only.
   - Combat animations (sword, shield block, bow, cast) need Mixamo or Fab packs downloaded with the user's account.
   - Once those are on disk, build an import + retarget pipeline. The procedural poses then become a fallback.
2. **Scholar pass.** Same treatment as Mage and Thief: dagger + shield guard, heal cast pose and FX.
3. **Knight polish.** Longsword stance; perfect-block feedback.
4. **Trait pick** at character select (spot enemies, spot loot, brawn, persuasion).
5. **Consequence quests.** Follow-ups to the Toll Bridge outcomes (honor / bribe / intimidate / hypnotize), per the consequences rule in `CHARACTERS.md`.
6. Put the project in git (with LFS) once the user wants it.

## 9. Working agreements with the user

- Keep this project separate from other projects/folders.
- Prefer C++ and data-driven definitions over Blueprints.
- Test-run before reporting. Verify visual changes with screenshots (the `pose` / `block` scenarios, `-RPGShot`).
- Automated runs use `-RPGNoInput` so the user's typing isn't hijacked.
- Keep `play.ps1` the one-command way to play, test and package.
