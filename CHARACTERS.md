# Characters, Classes & Dialogue (v0.2 plan)

Design pillar: **talking is as important as fighting.** Every major encounter can be resolved through dialogue, and each class talks differently, just as each class fights differently.

Character creation = **Class** (4) × **Sex** (2) × **Trait** (1 of 4).

---

## 1. Attributes (updated)

The existing four attributes, plus a new social one:

| Attribute | Combat | Dialogue |
|---|---|---|
| Might | +phys damage | Intimidate (secondary) |
| Agility | +stamina, +crit | — |
| Focus | +mana, +magic damage | **Hypnotize** |
| Vitality | +HP | — |
| **Presence** *(new)* | — | Persuade, Honor, Negotiate, Intimidate |

Starting attributes (each class totals 25):

| | Might | Agility | Focus | Vitality | Presence |
|---|---|---|---|---|---|
| Knight | 8 | 4 | 2 | 8 | 3 |
| Mage | 2 | 4 | 9 | 4 | 6 |
| Thief | 4 | 9 | 2 | 4 | 6 |
| Scholar | 2 | 4 | 5 | 4 | 10 |

---

## 2. Classes

### Knight — the wall
- **HP base 90**, starts with medium armor. Move speed 155 (slowest).
- **Loadout choice** (prototype: press X anytime; final game: at camp/merchant):
  - *Sword + Shield:* fast 3-hit combo; **hold right-click to block**: the shield turns toward the nearest attacker and stops **100%** of a frontal hit. Holding it drains stamina (and each blocked hit costs a little more); out of stamina, the guard breaks. A perfect block within 0.15s of impact staggers the attacker.
  - *Longsword:* slow, wide, heavy 2-hit combo, +40% damage, no block.
- Abilities: **Shield Bash** (L1, huge poise damage) · **Charge** (L2, dash strike) · **Rallying Cry** (L3, +armor buff) · **Cleave** (L4, spin AoE).
- Dialogue style **Honor**: invoke rank and oaths, swear protection, **challenge to a duel** (a 1v1 fight; the enemy's allies stand down).

### Mage — glass cannon behind a barrier
- **HP base 45** (fragile), **mana base 60**.
- **Arcane Barrier (hold right-click):** a bubble engulfs the mage and stops **80%** of damage from **every** side, but it drains stamina while held (it drops when stamina runs out). Nothing is on by default: the mage is fragile unless he chooses to shield. *(Replaced the earlier passive mana shield, 2026-10-06.)*
- **Basic attack:** Arcane Bolt, a ranged projectile with no mana cost (Focus-scaled). Spells are the mana sink, so attacking always competes with keeping the shield up.
- Abilities: **Fireball** (L1) · **Frost Nova** (L2, AoE slow) · **Blink** (L3, short teleport, replaces the roll's i-frames) · **Chain Lightning** (L4).
- Dialogue style **Hypnotize** (Focus): compel an NPC to agree, costs mana. It's very effective, but the NPC **resents** it when it wears off, and some NPCs are immune (strong-willed, warded, undead).

### Thief — fragile but evasive
- **HP base 40** (lowest), **move speed 200** (fastest). Dodge costs 14 stamina (vs 22), travels further, and has longer i-frames.
- **Daggers:** fast 4-hit combo, +15% crit; **backstab** (hit from behind) does ×2 damage.
- **Bow (right-click, unlimited arrows):** hold to draw; a full draw does bonus damage. Lets the Thief hit and run.
- Abilities: **Volley** (L1, 3-arrow spread) · **Smoke Bomb** (L2, enemies lose track of you) · **Shadow Dash** (L3) · **Poison Blade** (L4, damage over time).
- Dialogue style **Intimidate** (Presence + Might): threaten and extort. It works fast, but it creates **fear**. Frightened NPCs comply now but may betray you, tip off others or flee later.

### Scholar — the negotiator
- **HP base 45, armor 0** (lowest defense). Dagger + small shield (block −50% from the front; drains stamina while held).
- **Mend** (self heal over time) from Lv1.
- Abilities: **Mend** (L1) · **Ward** (L2, damage-absorb buff) · **Silver Words** (L3, briefly dazes an intelligent enemy **and opens dialogue mid-fight**) · **Insight** (L4, marks an enemy to take +25% damage and reveals its weakness).
- Dialogue style **Negotiate** (Presence, +30% class bonus): the strongest social skill, and it gets unique deal outcomes (recruit, bargain, buy someone off).
- **Limits** (so the Scholar doesn't make other classes pointless): deals cost gold or favors, deals create obligations that come back later, and mindless enemies (beasts, slimes, undead) can't be reasoned with.

### Combat roles at a glance

| | HP | Defense mechanic | Range | Speed |
|---|---|---|---|---|
| Knight | ★★★★ | Block / armor | Melee | ★ |
| Mage | ★ | Barrier (hold, all sides) | Ranged | ★★ |
| Thief | ★ | Evasion | Both | ★★★★ |
| Scholar | ★ | Small block, heal | Melee (weak) | ★★ |

---

## 3. Sex: changes reactions, not stats

Male and female characters have **identical stats**. The difference is how the world reacts:

- Specific NPCs respond differently based on **their own story**, not on general stereotypes. Examples: a widowed innkeeper confides in a woman about her husband; a bandit captain who lost his brother sees him in a young man and hesitates.
- That unlocks unique dialogue lines, some easier or harder checks with *that NPC*, and occasionally a different quest branch.
- **Rule:** both sexes get roughly equal numbers of unique branches and advantages, tracked in content review.

## 4. Traits (pick 1 at creation; any class, any sex)

| Trait | Effect |
|---|---|
| **Keen Eye** | Hidden loot caches are visible; +15% item drop chance |
| **Hunter's Sense** | Enemies show at long range on the edge of the screen, ambushes are revealed, +20% damage on the first hit against an unaware enemy |
| **Brawny** | +15% max HP, +10% physical damage |
| **Silver Tongue** | +10% on all dialogue checks; unlocks some extra lines |

*Later:* a second trait slot at level 5.

---

## 5. Dialogue system v2

### Checks: hidden, seeded rolls
- Options show their style but **not the odds**: `[Intimidate] "Hand it over, or I take it."`
- Success chance (internal):
  `chance = clamp(50% + 5% × (stat − npc.resolve) + classBonus + disposition/4 % + traitBonus, 5%, 95%)`
- The roll is **seeded** by `saveSeed + npcId + optionId`. Reloading gives the same result, so players can't reload until they win.
- A failed option stays failed for that NPC until something changes it (higher stat, better disposition, a quest flag).
- **Failure branches the story; it never dead-ends.** Failed intimidation starts a fight. Failed hypnosis makes the NPC hostile and remembering it. A failed duel challenge brings an insult and a worse price.

### Who can use what
- **Persuade** (Presence) is available to everyone at base odds.
- **Honor / Hypnotize / Intimidate / Negotiate** are class-exclusive. Each opens its own options and outcomes, not just better odds.

### NPC memory
Each NPC tracks **disposition** (−100…100) plus flags:
- `respect` (Honor, duel won) → future discounts, allies
- `resentment` (Hypnotize wore off) → hostile later, warns others
- `fear` (Intimidate) → complies now, may betray or flee later
- `trust` (Negotiate, kept promises) → unlocks deeper quests

### Talking to enemies
- Intelligent enemies (bandits, guards, cultists) have a `parley` dialogue. Nobody stops you to talk: you choose to talk (**E** + click) or to fight. Walking past a guarded crossing without doing either turns the faction hostile.
- The Scholar's **Silver Words** can open parley in the middle of combat.
- Mindless enemies (slimes, beasts, undead) can't be talked to.

### XP for outcomes, not kills
Named encounters and quests award **resolution XP** once, however they were resolved: killed, persuaded, recruited, scared off or duelled. Ordinary monsters keep a small kill XP. Without this, talking paths fall behind and players learn to fight everything.

---

### Consequences carry forward (design rule)
NPC memory flags (`respect`, `resentment`, `fear`, `trust`) and encounter outcomes must **pay off in later quests**, not just in the next line of dialogue. Every quest outcome should leave at least one flag that a later quest reads. Planned payoffs from *The Toll Bridge*:
- `brask_respect` (duel): Brask turns up as an ally in a later fight.
- `brask_resentment` (hypnosis): Brask hunts the Mage later.
- `brask_fear` (intimidation): Brask resurfaces in the Thief's guild questline.
- `brask_trust` (recruited): Brask gives the follow-up quest *Old Debts*.
- `truce` (Silver Words mid-fight): the Red Hands owe the Scholar a favor.

## 6. Quest structure

Writing four separate questlines for every class would be far too much content. Instead:

- **Shared main story.** Every major quest has: **Fight** + **Persuade** (anyone) + **1–2 class-exclusive approaches**.
- **One exclusive side questline per class** (~3 quests each), e.g. the Knight's order, the Mage's tower, the Thief's guild, the Scholar's archive.
- **Sex and trait branches** are small, NPC-specific variations, not separate questlines.

### Prototype test quest: *The Toll Bridge*
A bandit captain and 4 men block the bridge east of Ashford.

| Path | Who | Outcome |
|---|---|---|
| Fight the camp | Anyone | Loot the stash; captain dead |
| Persuade (hard) | Anyone | Toll waived once; bandits stay |
| **Honor duel** | Knight | 1v1 vs the captain; win → bandits disband, captain gives you his sword (`respect`) |
| **Hypnotize** | Mage | Captain sends his men away; returns later with a grudge (`resentment`) |
| **Intimidate** | Thief | Captain flees, leaving his stash; shows up later in the Thief's guild quest (`fear`) |
| **Negotiate** | Scholar | Captain becomes Ashford's guard and gives you the follow-up quest *Old Debts* (`trust`) |
| Sex variant | Male | The captain hesitates (lost brother); the Persuade check is easier with him |
| Sex variant | Female | His lieutenant (a woman who wants out) can be persuaded to turn on him |

All paths give the same **resolution XP**.

**Prototype status:** implemented. On top of the table above:
- Elder Maren's dialogue is class-specific, and each class can set up the encounter in advance:
  - Knight: swears an oath → +disposition and a potion.
  - Mage: reads her mind → learns about the relic, but she resents it.
  - Thief: extorts → double the gold reward.
  - Scholar: finds the **bridge charter** → a 95% option with Brask.
- Wren's bounty tip unlocks a **[Leverage]** option for any class.
- Paying 50g or a successful Persuade lets you through but the bandits stay. Sneaking across turns them hostile. Brask's death resolves the quest as `killed`.
- The Elder's reaction to the outcome is different for all ten outcomes.

---

## 7. Data model additions

Dialogue JSON gains:
```jsonc
// conditions
{ "class": "knight" } { "sex": "female" } { "trait": "silver_tongue" }
{ "disposition": { "npc": "captain", "gte": 20 } } { "flag": "captain_fled" }
// check (hidden, seeded roll)
{ "check": { "verb": "intimidate", "resolve": 6, "success": "node_a", "fail": "node_b" } }
// actions
{ "disposition": { "npc": "captain", "add": -30 } } { "setFlag": "captain_fled" }
{ "startCombat": "bandit_camp" } { "duel": "captain" } { "recruit": "captain" }
{ "resolveEncounter": "toll_bridge" }
```
NPC data gains: `resolve`, `immune: ["hypnotize"]`, starting `disposition`, `parley` node.
New data: `classes`, `traits`, `encounters`.

**Unreal:** `URPGClassDefinition` / `URPGTraitDefinition` (data assets generated from JSON), `URPGDialogueSubsystem::EvaluateCheck()` and `URPGNPCMemoryComponent`, all in C++.

---

## 7b. Ability implementation notes (prototype)

All 16 class abilities are built from 12 reusable ability types, so a new ability is usually data only:

| Type | Used by |
|---|---|
| `projectile` | Fireball, Volley |
| `aoe` | Cleave, Frost Nova (slows via `applyTag`) |
| `cone` | Shield Bash (always staggers; needs a shield) |
| `dashStrike` | Charge, Shadow Dash |
| `buff` | Rallying Cry (stat mods), Ward (damage absorb) |
| `blink` | Blink (stops at walls/water, so it can't skip the bridge) |
| `chain` | Chain Lightning |
| `smoke` | Smoke Bomb (Hidden; attacking reveals you) |
| `weaponBuff` | Poison Blade (dagger hits apply poison over time) |
| `heal` | Mend |
| `daze` | Silver Words (intelligent enemies only; opens parley dialogue mid-fight) |
| `mark` | Insight (+25% damage taken, reveals HP and next move) |

Costs: Knight and Thief abilities use **stamina**; Mage and Scholar abilities use **mana**. Casts that miss (no target, mindless target) cost nothing.

## 7c. Animation plan (Unreal)

> **Superseded (2026-10-05):** the game went HD-2D: characters are pixel-art sprite sheets (3 directions x idle 2,
> walk 4, attack 4, hurt 1, plus dead and guard frames), drawn in code by `tools/pixelart/characters.py`. The
> mannequin still runs underneath, hidden, so montage hit timing is unchanged. The plan below is kept for the record.

**Had** (UE5 mannequin, from the Third Person template):
- idle / walk / jog blendspace, jump / fall / land, dash, deaths
- 3-hit unarmed combo + charged attack (montages)
- pistol/rifle aim and fire sets

Every class currently shares the combo; weapons are attached to the hands.

**Need, per class:**

| Class | Animations |
|---|---|
| Knight | sword & shield combo, shield block idle/hit, shield bash, longsword (two-handed) combo, charge |
| Mage | staff idle/locomotion, cast (bolt, AoE nova, channel), blink |
| Thief | dagger combo, bow draw/aim/release, roll, crouch-sneak |
| Scholar | dagger + small-shield combo, block, cast/heal |
| All | dodge roll, hit reactions, stagger, interact / talk gestures |

**Sources:**
- **Movement:** Epic's free **Game Animation Sample** (Fab, UE 5.8 version) has motion-matched locomotion and traversal on the UE5 mannequin. It's the best upgrade for movement, but it has no combat animations.
- **Combat:** **Mixamo** (free, Adobe account): search "sword and shield", "great sword", "longbow", "magic" / "spell", "dagger", "roll". Or a combat pack from Fab.

**Pipeline:**
1. Drop the FBX files into `unreal/ImportSource/Animations/<class>/`.
2. A headless Python step (to be written) imports them and retargets Mixamo → UE5 mannequin using UE 5.8's auto-retargeting.
3. The C++ then picks animations by class and action, configured in data.

## 8. Prototype build order

1. **Character creation screen:** class, sex, trait.
2. **Class kits:** block and perfect block, mana shield, bow with draw, dagger backstab, Arcane Bolt, the 16 class abilities.
3. **Dialogue v2:** conditions, seeded hidden checks, disposition and flags.
4. **Parley + resolution XP.**
5. **The Toll Bridge** encounter, plus class flavor in Elder Maren's dialogue.
6. Debug panel: show the hidden odds (for tuning only).
