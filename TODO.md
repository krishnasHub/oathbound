# TODO — Oathbound

## Current plan: the Thief's sticky fingers (agreed 2026-10-07)

The Thief steals the way the Scholar talks: anything with pockets can be robbed (slimes have none). After each phase:
automated tests, a fresh package (`Dist\Windows\Oathbound.exe`) for the user to play, then the next phase. Nothing is
committed or pushed until every phase is done and the user has tested it all. Background: `SO_FAR.md` §8.

Controls (the user, 2026-10-07): **E** is the action key: talk, unlock a door, steal. **Space**: the Thief
crouches (a toggle: sneaking); every other hero dodges / dashes through as today. The Thief's normal speed goes up a
lot to make up for it (the crouch is the slow, quiet mode).

How a steal plays: **sneak** (Space, Thief only: ~60% speed, foes hear you at ~25% radius) -> get within reach **unseen**
(outside the target's sight cone, i.e. behind or beside it; or hidden by Smoke Bomb; or while it sleeps) -> hover shows
a hand cursor, "Steal" and a hint of what it carries -> **click** and a progress ring fills (longer for better loot,
shorter with Agility); stay unseen and in reach until it fills. Walking off cancels safely. While sneaking, E (or E
then a click) on an unaware target steals; otherwise clicks attack / backstab as today.

Decisions: **caught** = the target shouts "Thief!" and turns hostile, allies join, talkers remember. **Mood**: unseen
theft 0, caught -2; villagers: unseen -1, caught -3 and Tobin's prices rise. **Villagers can be robbed** too. **Everyone keeps
hours** (see T0): villagers and the bandits sleep at night; the Ruin Brute and the skeletons sleep by day, each in its
own rest place. A sleeper has no sight and tiny hearing, so it's the easy window to rob it (lower level floors).
Villagers sleep in their houses, and **only the Thief can enter houses** (E picks the lock). Each pocket empties once;
stealing a quest item resolves the quest with the same XP as the other routes. Level floors use Loom's level checks.

Where things go: **Tessera** = sneak modifier on senses, a timed action on a character (progress ring, cancels if
noticed), sleep schedules (a perception state + day/night events). **Loom** = the steal check (level floor + per-level bonus), flags
(`robbed_by_hero`...). **Oathbound** = pockets data, routes, consequences, art (sneak pose, hand / eye icons).

### Phase T0 — Day and night: who sleeps where, and houses the Thief can enter (built; `sleep` test passes; waiting for the user to play it)
- [x] Tessera `UTSSleep`: hours (`"sleeps": "night" | "day"`) from `UTSDayNight`; at bedtime walk to a rest place and lie
      down ("Asleep" tag, sprite lies down, a "z" drifts up); no sight cone, hearing x `tuning.sleep.hearMul`; hits
      x `tuning.sleep.hitMul` then it wakes; woken (hit, noise, talked to) = up for a while, then back to bed.
- [x] Hours: villagers and the bandits incl. Brask sleep at **night**; the Ruin Brute and the skeletons by **day**. Wren
      keeps watch by the camp fire at night (the bridge isn't free). Grot the Miner keeps his routine.
- [x] Rest places: the Brute on a **haystack** inside the mine; the bandits on **bedrolls round a small camp fire** by
      the bridge; the skeletons **rest in a walled graveyard** by day (its gate is sealed: nobody, not even the
      Thief, gets in; skeletons are robbed only at night, crouched and from behind); villagers **go into their houses** (a bed
      inside each).
- [x] Houses: a villager walks to the door and goes in; at night, walking close to a house with a sleeper cuts it away
      (Tessera cutaways: plank floor, knee-high walls) so you see them asleep in bed.
- [x] Doors: **only the Thief can enter houses.** E (or click) at a house door: the Thief picks the lock and the door
      stays open (the house can be walked into); everyone else: "Locked." That's the Thief's way to the villagers'
      pockets (T5).
- [x] Talking to a sleeper wakes it (a grumpy line first); a sleeper's "!" / "?" marker is hidden.
- [x] Art: haystack (a flat bed of straw under the brute, a torch beside it), bedroll, the camp fire, the in-house bed,
      the graveyard gate. Rest props are always there, so the Thief can see where everyone sleeps.
- [x] Tests (`sleep`): who sleeps by day / by night; rest places reached; a quiet hero isn't noticed by a sleeper, one
      right beside it wakes it; a hit wakes it with bonus damage; the Knight can't open a door, the Thief can and walks in.

### Phase T1 — Foundations: sneak, pockets, the steal (built; `heist` test)
- [x] Tessera: `UTSChannel` (a moment's work standing still: label, progress, breaks if you move or Keep says no);
      `TSPerception`: a target tagged "Sneaking" is heard only within hearing x `tuning.sneak.hearMul` (0.25);
      the sprite crouches (shorter, slower steps).
- [x] Space: the Thief crouches (toggle; `classes.thief.sneak.speed` 0.5) instead of dashing; other heroes still
      dodge. The Thief's standing speed is up (200 -> 270).
- [x] E is the action key: a crouched Thief steals from whoever is within reach and unaware (asleep, or behind it and
      too quiet to hear); with nobody there, E talks / uses as before. Doors: a click only tries a cottage door
      ("Locked. (E: pick the lock)"); E picks it (a short channel at the door). Everyone else: "Locked."
- [x] Pockets (`enemies.<id>.pockets`, `npcs.<id>.pockets`): gold, items (with a chance), hint, time, level
      { min, asleep }, bark, and `do` (Loom actions on success). Each pocket empties once (flag `robbed:<who>`).
- [x] Caught (it turns round / wakes mid-lift): "Thief!", a foe and its faction turn hostile, mood -2 (villagers -3
      and `thief_known`). Unseen: foes 0, villagers -1.
- [x] HUD: "E: steal (a heavy purse)" over the mark, or why not ("Get behind it", "Too well guarded (level 3)");
      "E: pick the lock" at a locked door; a bar over the hero while working; "sneaking" under the hero.
- [x] Loom: `RunActions` public (the pockets' `do` list runs outside dialogue).

### Phase T2 — Brask's toll purse (built)
- [x] Brask carries the toll purse (level 3, or 1 while he sleeps): stolen -> the Toll Bridge resolves `robbed`,
      `brask_fear`, and the Red Hands scatter (`scatter`: walk off, no "talked down" reward). Maren has a line for it.

### Phase T3 — Skeletons (built)
- [x] Archers carry grave coins and sometimes a Ferryman's Coin, which pays a toll-wanting skeleton (Scholar talks).
- [x] Captain Ossric's rank badge (level 2): stolen -> the Bonewardens drift apart. Skeletons can only be robbed at
      night (by day they're in the sealed graveyard).

### Phase T4 — The Ruin Brute and the relic (built)
- [x] The relic (level 4, or 1 while it sleeps by day on its hay). Stolen -> **world change**: at night the brute leaves
      the mine and prowls the village (`prowl` routine; "SHINY THIEF!" if it sees you), by day it goes back to its hay.
- [x] It ends when the relic is put back on its hay (the hay offers it when you carry the relic), when it's paid off
      (60 gold, Old Tongue speakers), or when it dies. Its other routes (bread, hypnosis, trade) no longer hand over a
      relic it hasn't got; the kill drop waits until it's back. Maren knows ("It will come looking for this").

### Phase T5 — Villagers (built)
- [x] Maren, Tobin and Guard Brask carry purses; robbed asleep in their houses (E picks the lock). Unseen -1 mood;
      caught -3, `thief_known`: Tobin charges a thief's price (x1.3) and both speak coldly.

### After the user's first Thief play (2026-10-07)
- [x] The Thief can pick any lock, the graveyard gate too (a map '+' gate tile; `restPlaces.graveyard.enclosure`).
      Locks have levels: cottage doors 1 (1.4 s), the graveyard gate 2 (3 s) (`pickLevel`, `pickTime`). Beyond his
      level the Thief can still try: the bar fills to 55-65 %, turns red and snaps in two ("*Snap.* The pick breaks.")
      (Tessera `UTSChannel::FailAt`; test `lockpick`).
- [x] Action mode (E toggles it): the cursor shows what a click does: speech bubble (talk), turning gears (steal /
      pick / use), grey (nothing). Crouched, the Thief never talks: gears to steal (even from sleepers); standing he
      talks (a sleeper is woken). Without E, clicks move / attack / talk as before.
- [x] Villagers and idle foes turn to the hero only when they notice him (or talk), not all the time.
- [x] Fixes from play: a steal click now walks right up to the mark (it stopped at talking distance: "Get closer");
      a crash after a refused steal click (null target in the approach hook) fixed, with a regression test.
- [x] Night eyes: the Thief sees a dim grey beyond his light at night, not black (`classes.thief.nightEyes` 0.55,
      Tessera `ATSSky::SetNightEyes`; only the look: what counts as seen is unchanged). Test `nighteyes`.
- [x] Crouch look "B+" (the user's pick from tools/pixelart/out/crouch_options.png): a ninja-creep sheet
      (SPR_thief_*_sneak: deep, leaning crouch, dagger hand out low) swapped in while sneaking, and a shadow-cloak
      tint (`statusTints.Sneaking`). Test `crouch`.

### Phase T6 — Proof runs
- [x] `heist` (Thief, night): crouch, quiet steps, a lift from behind, caught mid-lift, Brask's purse (band
      scatters), Ossric's badge (band drifts apart), Maren robbed in bed, Tobin catches you, the relic lifted from
      the sleeping brute, the brute prowling the village, the relic put back. Screenshots `heist_*`.
- [ ] The user plays it.

---

## Done: talking foes, the Scholar's routes and world mood (2026-10-07)

The agreed plan (2026-10-07), built phase by phase. **All six phases done, tested and committed (2026-10-07).**

Where things go: **Loom** = story state and rules (level checks, world mood). **Tessera** = generic mechanisms
(ambient life, building wear, interactables, NPC routines). **Oathbound** = this game's data, content, art and the
code that listens to Loom / Tessera events. Event-driven: the plugins announce, the game reacts.

### Phase 1 — Foundations: languages, level-scaled persuasion, peaceful wins (done; played by the user as mage and scholar, 2026-10-07)
- [x] Languages: `"speaks"` on creatures, `"languages"` on classes; the Scholar speaks every language; slimes have none.
      Common (bandits, Brask, Wren, villagers: everyone) · Old Tongue (Ruin Brute: Mage, Scholar) · Grave-speech
      (skeletons: Scholar).
- [x] Who can talk: a creature that speaks the hero's language and isn't provoked waits and watches instead of
      attacking; others attack as before. Reasons shown when you can't ("It speaks the Old Tongue. You don't.").
- [x] Silver Words (mid-fight parley) follows the same language rule.
- [x] Placeholder conversations for the skeleton and the Ruin Brute (real ones come in phases 3-4).
- [x] Loom: level-scaled checks (`"level": { "min", "per" }`), a hook for the hero's level; below the minimum the
      check can't succeed; a failed check can be retried after levelling up.
- [x] The bridge: Brask's honour challenge, hypnosis and negotiation get level floors.
- [x] Peaceful wins pay: talked-down foes give at least their kill XP and never respawn.
- [x] Tests: who can talk to whom per class; level-gated check and retry; peaceful XP.

### Phase 2 — World mood and ambient life (done; played by the user as mage and scholar, 2026-10-07)
- [x] Loom: a hidden world-mood value (starts at 0) with change / band events; data actions to move it.
- [x] Weights: peaceful outcomes +, killing talkable creatures -, betrayal --, slimes 0; redemption possible but slow.
- [x] Tessera: a generic ambient-life system (ATSAmbientLife): harmless critters and walkers that live in the world, flee or vanish
      near the hero, can't be fought; day / night spawn tables the game can re-weight.
- [ ] Move today's birds, geese, night prowler and fireflies onto it. (Kept as they are for now: they react to
      Frost Nova and the barrier, which ambient life doesn't yet. The new geese flocks are on the new system.)
- [x] The game bridges Loom's mood to Tessera's tables, applied at dawn / dusk (Tessera's day/night events).
- [x] First effects: good = more geese, butterflies, children playing by day; bad = bats at night.
- [x] Tests: mood moves with actions; tables follow the bands; critters flee the hero (`mood`).

### Phase 3 — Skeletons (Scholar) (done; played by the user as mage and scholar, 2026-10-07)
- [x] Tessera: things you can interact with (ATSInteractable; click or E walks up and uses it) and talk to (graves, lost items, signs): a Loom conversation with an object.
- [x] Lone skeletons (ferryman's toll, a riddle, a song): a small random pool of simple wants (gold...) plus hand-written fun ones.
- [x] The unmarked grave (Aldric, the Grave-Watcher): find it, read the rites (Scholar), mark it (a grave marker appears for good); back at the
      skeleton, it crumbles in peace (calm ghost).
- [x] A skeleton band with a leader (Captain Ossric's Bonewardens, his signet): the leader wants a lost item fetched; bring it back and the whole band leaves.
- [x] Tests: each route end to end (`skeletons`).

### Phase 4 — The Ruin Brute (Scholar; the Mage can Hypnotize) (done; played by the user as mage and scholar, 2026-10-07)
- [x] Move it into a cave with the relic: the old mine in the ruins is a small map of its own (Tessera map
      areas), entered and left through a two-way door (E / click), with a fade.
- [x] Wants food: Elder Maren parts with some after a kill-5-slimes quest; bring it, get the relic.
- [x] High level (4+): teach it a trade (Grot the Miner) instead; it hands over the relic and from then on walks between its mine and the
      village carrying gold for food (Tessera: NPC routines on the day/night events).
- [x] Tests: food route, teach route, the trade walk, the cave doors (`brute`, `trader`, `cave`).

### Phase 5 — The full mood palette (all cosmetic, all flee the hero) (done; played by the user as mage and scholar, 2026-10-07)
- [x] Good: puppies, more children, flocks. Bad: grumpy men by day; wolves, bats, snakes at night.
- [x] Tessera: adjustable building dressing (ATSDressing), both ways: bad = cracks in unkept roads, wear and tear on houses;
      good = a few cute green vines on buildings (subtle, not too many) and more sunrays by day.
- [x] Villagers' tone and shop prices follow the mood (Loom text variants; Loom {price:N} text, the game's price rule).
- [x] Pixel art for all of the above (tools/pixelart/life.py).
- [x] Tests: each band looks right (screenshots), changes land at dawn / dusk (`palette`, `palette_night`, `mood`).
- [x] Walkers (children, geese, puppies, grumpy men...) never step into water or walls.

### Phase 6 — Proof runs (done; played by the user as mage and scholar, 2026-10-07)
- [x] A pacifist Scholar who only kills slimes finishes the bridge and the relic quests.
- [x] A killing spree darkens the world; redemption slowly brings it back.
- [x] A play-through with every hero (`tour_knight`, `tour_mage`, `tour_thief`, `tour_scholar`): who can talk to whom, each
      class's own way with Brask, the grave, the old mine and its brute (talked to or fought), in and out.
- [ ] Then: other classes' world-changing routes. Thief: in progress (above). Knight's honour duel: later.

### Found and fixed while play-testing
- Grot vanished in the ruins: flat sprite cards were being lost by the occlusion culler (roomier bounds), and tall
  cards leaned into walls behind them (character cards now stand upright, stretched to look the same).
- The cave's walls hid the hero: cave rock kept low (the dark and the torches make it a cave).
- Puddles on the road replaced by road cracks (the user's call).
- Spawn letters are case-insensitive in Unreal (`b` vs `B` swapped the brute for a skeleton); noted in the map legend.
- Tests run at most 4 at a time (tessera.json maxParallel).

## Later / open threads
See `SO_FAR.md` §8 (fog of war, saves, music, Scholar and Knight polish, traits...).
