# Action RPG — Mechanics Design (v0.1)

Working title: TBD. Setting: TBD (mechanics-first; the prototype uses neutral placeholder fantasy).

> **Direction (2026-10-05):** the game is a **top-down HD-2D** game in Unreal (pixel-art sprites in a lit 3D world,
> mouse-driven: click to move / attack / talk). The HTML prototype stays as the test bed for the rules. See
> `SO_FAR.md` for the current state; the sections below are the rules both versions share.

## Goals for this phase

1. **Get the mechanics right.** The rules below are the source of truth.
2. **Test in HTML.** `prototype/rpg-prototype.html` is a top-down, single-file build that runs these rules.
3. **Base classes in Unreal.** C++-first on the Gameplay Ability System (GAS). Blueprints are only thin asset bindings (see "Unreal mapping").

All numbers live in **data**: the `<script id="game-data">` JSON block in the prototype. In Unreal that same data becomes Data Tables / Data Assets (CSV or JSON import), so balance changes made in the prototype carry over.

> What the prototype **can** test: stats, damage math, combo timing, stamina economy, ability costs and cooldowns, poise and stagger, enemy telegraphs and AI states, loot, progression and quests.
> What it **can't**: 3rd-person camera, lock-on, melee hit detection in 3D, animation feel, verticality. Plan an early Unreal combat test for those.

---

## Core loop

Explore → fight (real-time) → loot → level up / equip → take and turn in quests → harder zones.

## Controls (prototype)

| Input | Action |
|---|---|
| WASD | Move |
| Mouse | Aim / facing |
| LMB (tap or hold) | Light attack, 3-hit combo |
| Space | Dodge roll (i-frames, costs stamina) |
| 1–4 | Abilities |
| Q | Drink potion |
| E | Talk / interact |
| I / C / J | Inventory / Character / Quest log |
| ` (backtick) | Debug overlay + cheats (K = level up, G = +100 gold, L = spawn loot) |

---

> **v0.2:** classes, sex, traits, the Presence attribute and dialogue v2 are planned in [CHARACTERS.md](CHARACTERS.md). The sections below describe the current v0.1 prototype.

## 1. Attributes & derived stats

Primary attributes (player starts at 5 each, +3 points per level):

| Attribute | Effect |
|---|---|
| **Might** | +6% physical damage per point |
| **Agility** | +4 max stamina, +1% crit per point |
| **Focus** | +5 max mana, +7% ability (magic) damage per point |
| **Vitality** | +10 max HP per point |

Derived stats:

- `MaxHP = hpBase(60) + Vitality*10 + Level*5`
- `MaxStamina = 80 + Agility*4`, regenerates 38/s after a 0.55s delay following any stamina spend
- `MaxMana = 30 + Focus*5`, regenerates 2.5/s
- `CritChance = critPct(5) + Agility*1` (%) and `CritMultiplier = 1.6`
- `Armor` comes only from gear

## 2. Damage pipeline (one central function)

```
dmg = base
dmg *= 1 + Might*0.06            (physical)  OR  1 + Focus*0.07 (magic)
dmg *= CritMultiplier            (on crit roll)
dmg *= 100 / (100 + targetArmor) (armor: diminishing returns)
dmg *= random(0.9, 1.1)
```

- Light attack `base = weaponDamage * comboMultiplier`
- Ability `base = ability.damage`
- Enemy attack `base = attack.damage` (enemies don't scale with attributes and don't crit)

In Unreal this is a `UGameplayEffectExecutionCalculation`.

## 3. Combat

**Light combo:** 3 hits (1.0×, 1.1×, 1.7×). Each hit has `windup → active → recover` phases, a stamina cost, an arc and a range, poise damage and knockback, and a small forward lunge. Clicking during a swing buffers the next hit. The combo resets if you don't attack again within 0.45s of a swing ending. Movement is slowed to 35% while attacking.

**Dodge roll:** costs 22 stamina, lasts 0.32s with 0.26s of invulnerability, and cancels attack recovery. Projectiles pass through a dodging player.

**Stamina** gates attacks and dodges. Running out is the main source of risk.

**Poise / stagger:** every character has a poise value. Hits deal poise damage, and at 0 the target is **Staggered** for 0.55s (can't act). Poise refills fully 2s after the last hit.
- Slime 15 (staggers every hit), Archer 20, Brute 110 (needs a combo or a heavy ability)
- Player 60. Getting hit also grants 0.45s of invulnerability to prevent stun-lock.

**Enemy telegraphs:** every enemy attack has a visible wind-up (a red arc, circle or line) that's long enough to react to. The design rule: *every hit should be avoidable by reading the telegraph and dodging.*

## 4. Abilities (mana + cooldown, unlocked by level)

| Key | Ability | Unlock | Mana | CD | Effect |
|---|---|---|---|---|---|
| 1 | Fireball | Lv1 | 10 | 1.2s | Projectile, 16 base (Focus) |
| 2 | Whirlwind | Lv2 | 16 | 6s | AoE r85, 14 base (Might), big knockback |
| 3 | Shadow Dash | Lv3 | 12 | 5s | Invulnerable dash 190px; hits everything on the path (20 base, Might) |
| 4 | Mend | Lv4 | 20 | 12s | Heal over time: 7 per 0.5s for 4s (Focus) |

Ability types are generic (`projectile`, `aoe`, `dashStrike`, `heal`), so new abilities are mostly data.

## 5. Enemies & AI

State machine: `idle (wander) → chase → windup → attack → recover → chase`, with `return` (leash) when the player escapes. Enemies need line of sight to aggro. Each enemy cycles through its `attacks[]` list.

| Enemy | HP | Armor | Role |
|---|---|---|---|
| Slime | 30 | 0 | Slow melee lunger, teaches dodging |
| Skeleton Archer | 26 | 5 | Keeps ~210px away and strafes; telegraphed shots |
| Ruin Brute (boss) | 260 | 30 | 2 swings + ground slam; high poise; resists knockback |

Dead enemies respawn after a timer once the player is far away.

**Territories:** the map is split into regions (`map.regions`, by row): **north** (village, meadow, bridge) and **south** (south bank, woods, ruins). An enemy engages the player only while the player is in the enemy's home region. Crossing back mid-fight makes it cancel its wind-up and return home. **Provoked** enemies are the exception: an enemy hit by the player fights back across regions, so you can't snipe south-bank archers from safety. The bridge belongs to the north, so south-side enemies wake up when you step off it.

## 5b. Vision

- **Player:** sees an 80° cone toward the cursor out to 360px, plus a small 60px circle all around. Trees, walls and houses block sight; water doesn't. The edges are soft, and sight fades with distance: fully clear out to ~45% of the range, then hazier toward the tip. Characters fade in the same way, down to ~35% opacity at the far edge. Outside the view the terrain is greyed out (layout still readable) and enemies, NPCs, loot, projectiles and damage numbers are hidden.
- **Enemies:** notice the player only inside their 120° sight cone (up to their aggro range), or within 70px ("hearing"). They need line of sight. Any hit alerts them, even from behind. This makes approaching from behind a real tactic (Thief backstab, Smoke Bomb).
- **Fairness rules** (so nothing hits you from somewhere you can't see):
  - Ranged enemies engage from inside the player's sight range: archers notice and shoot from 260px (Wren 270), against the player's 360px cone.
  - **Threat sense:** each unseen enemy hunting you (within 450px) shows as a red arrow at the edge of your circle, pointing at it. It pulses bright while that enemy winds up an attack.
  - Incoming projectiles are always visible.
  - An enemy that just attacked is revealed faintly for 1.5s.
- All numbers live in `tuning.vision` / `tuning.enemyVision` / `tuning.threatSense`; enemies can override them with `visionAngle` / `hearRadius`. Debug (`) shows everything and draws enemy sight cones.
- **Unreal:** the player's view becomes a fog-of-war post-process/material (or, in 3rd person, the camera simply limits it). Enemies use `UAIPerceptionComponent` sight config (cone angle + radius) plus hearing.

## 6. Progression

- `XP to next level = 40 * 1.45^(level-1)` (40, 58, 84, 122, …)
- Level up: +3 attribute points, full restore, ability unlocks
- Free respec button in the prototype (testing aid)

## 7. Loot & inventory

- 3 equipment slots: **Weapon** (weaponDamage), **Armor** (armor), **Trinket** (affixes only)
- Rarity: Common (0 affixes) / Magic (1) / Rare (2). Affixes: +Might, +Agility, +Focus, +Vitality, +Armor, +Crit%.
- Loot tables per enemy: drop chance, rolls, rarity weights, weighted item entries. Gold always drops.
- 16-slot bag; potions stack. Shift-click salvages an item for gold.
- Gold sinks: merchant (potions, gear), death penalty (−10% gold).

## 8. Quests & dialogue

- Quest objectives: `kill` (target type × count) and `collect` (item × count, consumed on turn-in).
- Quest states: `none → active → complete → turnedIn`.
- Dialogue is a data graph: nodes have text and choices. Choices have conditions (`quest is X`, `gold >= N`, `hasItem`) and actions (`startQuest`, `turnIn`, `buy`, `restore`). Choices can carry a `!` / `?` marker, which shows above the NPC.
- Prototype content: *Slime Trouble* (kill 5 slimes) → *The Lost Relic* (beat the Brute, return the relic). There's also a merchant.

---

## Architecture: prototype → Unreal mapping

The JS prototype is deliberately structured like the Unreal version:

| Prototype (JS) | Unreal (C++ base → BP child) |
|---|---|
| `Character` | `ARPGCharacterBase : ACharacter, IAbilitySystemInterface` |
| `Player` | `ARPGPlayerCharacter` → `BP_Player` |
| `Enemy` + `enemyAI()` | `ARPGEnemyCharacter` + `ARPGAIController` / StateTree → `BP_Enemy_Slime`, … |
| `NPC` | `ARPGNPC` (interactable) → `BP_NPC_Elder` |
| `StatsComponent` (attributes, modifiers, effects) | `URPGAttributeSet` + `UAbilitySystemComponent` + `UGameplayEffect`s |
| `TagSet` (`Invulnerable`, `Staggered`, `Dodging`) | Gameplay Tags (`State.Invulnerable`, …) |
| `AbilityComponent` + `AbilityImpls` | `URPGGameplayAbility` base → `GA_Fireball`, `GA_Whirlwind`, … |
| `dealDamage()` / `computeDamage()` | `URPGDamageExecution` (ExecutionCalculation) |
| `InventoryComponent` | `URPGInventoryComponent` (ActorComponent) |
| `makeItem()` + item/affix data | `URPGItemDefinition` (PrimaryDataAsset) + affix Data Table |
| `Quests` | `URPGQuestSubsystem` (GameInstanceSubsystem) |
| `Dialogue` + dialogue JSON | `URPGDialogueAsset` (DataAsset with nodes, conditions, actions) |
| `Events` bus | Multicast delegates / Gameplay Message Subsystem |
| `game-data` JSON | Data Tables (imported from the same JSON/CSV) |

### C++-first (so git stays useful)

Blueprints and other `.uasset` files are binary: no text diffs, no merges, and they need Git LFS plus file locking. So:

- **All gameplay logic is in C++.** That covers the base classes and the concrete abilities (`UGA_Fireball` in C++, not a BP graph), enemy AI tasks, damage, inventory, quests and dialogue.
- **All tuning data lives in text files in the repo.** The `game-data` JSON is split into `Data/*.json` (enemies, items, abilities, quests, dialogue). A small C++ loader, or an editor import step, builds the Data Tables/Assets from them. The JSON is the source of truth that gets reviewed in PRs; the generated `.uasset` files are just build output.
- **Blueprints are thin asset bindings only:** `BP_Slime` just picks a mesh, anim BP, VFX and a row name, with no graph logic. If one gets "broken", re-creating it takes minutes.
- Unavoidable binary assets (maps, meshes, textures, anims, those thin BPs) go through **Git LFS with locking** (`.gitattributes` + `git lfs lock`).
- A standard UE `.gitignore` keeps `Binaries/`, `Intermediate/`, `Saved/` and `DerivedDataCache/` out of the repo.

## Open questions

- Theme and setting
- 3rd-person combat specifics: lock-on? Light/heavy attacks? Animation-driven hit windows?
- Party/companions, or solo?
- Save system scope
- Open world vs. hub + zones
