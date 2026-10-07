# TODO — Oathbound

The agreed plan (2026-10-07), built phase by phase. **All six phases done, tested and committed (2026-10-07).** After each phase: automated tests, a fresh package
(`Dist\Windows\Oathbound.exe`) for the user to play, then the next phase. **Nothing is committed or pushed until every
phase is done and the user has tested it all.** Background and decisions: `SO_FAR.md` §8.

Where things go: **Loom** = story state and rules (level checks, world mood). **Tessera** = generic mechanisms
(ambient life, building wear, interactables, NPC routines). **Oathbound** = this game's data, content, art and the
code that listens to Loom / Tessera events. Event-driven: the plugins announce, the game reacts.

## Phase 1 — Foundations: languages, level-scaled persuasion, peaceful wins (done; played by the user as mage and scholar, 2026-10-07)
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

## Phase 2 — World mood and ambient life (done; played by the user as mage and scholar, 2026-10-07)
- [x] Loom: a hidden world-mood value (starts at 0) with change / band events; data actions to move it.
- [x] Weights: peaceful outcomes +, killing talkable creatures -, betrayal --, slimes 0; redemption possible but slow.
- [x] Tessera: a generic ambient-life system (ATSAmbientLife): harmless critters and walkers that live in the world, flee or vanish
      near the hero, can't be fought; day / night spawn tables the game can re-weight.
- [ ] Move today's birds, geese, night prowler and fireflies onto it. (Kept as they are for now: they react to
      Frost Nova and the barrier, which ambient life doesn't yet. The new geese flocks are on the new system.)
- [x] The game bridges Loom's mood to Tessera's tables, applied at dawn / dusk (Tessera's day/night events).
- [x] First effects: good = more geese, butterflies, children playing by day; bad = bats at night.
- [x] Tests: mood moves with actions; tables follow the bands; critters flee the hero (`mood`).

## Phase 3 — Skeletons (Scholar) (done; played by the user as mage and scholar, 2026-10-07)
- [x] Tessera: things you can interact with (ATSInteractable; click or E walks up and uses it) and talk to (graves, lost items, signs): a Loom conversation with an object.
- [x] Lone skeletons (ferryman's toll, a riddle, a song): a small random pool of simple wants (gold...) plus hand-written fun ones.
- [x] The unmarked grave (Aldric, the Grave-Watcher): find it, read the rites (Scholar), mark it (a grave marker appears for good); back at the
      skeleton, it crumbles in peace (calm ghost).
- [x] A skeleton band with a leader (Captain Ossric's Bonewardens, his signet): the leader wants a lost item fetched; bring it back and the whole band leaves.
- [x] Tests: each route end to end (`skeletons`).

## Phase 4 — The Ruin Brute (Scholar; the Mage can Hypnotize) (done; played by the user as mage and scholar, 2026-10-07)
- [x] Move it into a cave with the relic: the old mine in the ruins is a small map of its own (Tessera map
      areas), entered and left through a two-way door (E / click), with a fade.
- [x] Wants food: Elder Maren parts with some after a kill-5-slimes quest; bring it, get the relic.
- [x] High level (4+): teach it a trade (Grot the Miner) instead; it hands over the relic and from then on walks between its mine and the
      village carrying gold for food (Tessera: NPC routines on the day/night events).
- [x] Tests: food route, teach route, the trade walk, the cave doors (`brute`, `trader`, `cave`).

## Phase 5 — The full mood palette (all cosmetic, all flee the hero) (done; played by the user as mage and scholar, 2026-10-07)
- [x] Good: puppies, more children, flocks. Bad: grumpy men by day; wolves, bats, snakes at night.
- [x] Tessera: adjustable building dressing (ATSDressing), both ways: bad = cracks in unkept roads, wear and tear on houses;
      good = a few cute green vines on buildings (subtle, not too many) and more sunrays by day.
- [x] Villagers' tone and shop prices follow the mood (Loom text variants; Loom {price:N} text, the game's price rule).
- [x] Pixel art for all of the above (tools/pixelart/life.py).
- [x] Tests: each band looks right (screenshots), changes land at dawn / dusk (`palette`, `palette_night`, `mood`).
- [x] Walkers (children, geese, puppies, grumpy men...) never step into water or walls.

## Phase 6 — Proof runs (done; played by the user as mage and scholar, 2026-10-07)
- [x] A pacifist Scholar who only kills slimes finishes the bridge and the relic quests.
- [x] A killing spree darkens the world; redemption slowly brings it back.
- [x] A play-through with every hero (`tour_knight`, `tour_mage`, `tour_thief`, `tour_scholar`): who can talk to whom, each
      class's own way with Brask, the grave, the old mine and its brute (talked to or fought), in and out.
- [ ] Then: other classes' world-changing routes (Knight's honour duel, Thief stealing the relic...).

## Found and fixed while play-testing
- Grot vanished in the ruins: flat sprite cards were being lost by the occlusion culler (roomier bounds), and tall
  cards leaned into walls behind them (character cards now stand upright, stretched to look the same).
- The cave's walls hid the hero: cave rock kept low (the dark and the torches make it a cave).
- Puddles on the road replaced by road cracks (the user's call).
- Spawn letters are case-insensitive in Unreal (`b` vs `B` swapped the brute for a skeleton); noted in the map legend.
- Tests run at most 4 at a time (tessera.json maxParallel).

## Later / open threads
See `SO_FAR.md` §8 (fog of war, saves, music, Scholar and Knight polish, traits...).
