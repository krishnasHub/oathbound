# TODO — Oathbound

The agreed plan (2026-10-07), built phase by phase. After each phase: automated tests, a fresh package
(`Dist\Windows\Oathbound.exe`) for the user to play, then the next phase. **Nothing is committed or pushed until every
phase is done and the user has tested it all.** Background and decisions: `SO_FAR.md` §8.

Where things go: **Loom** = story state and rules (level checks, world mood). **Tessera** = generic mechanisms
(ambient life, building wear, interactables, NPC routines). **Oathbound** = this game's data, content, art and the
code that listens to Loom / Tessera events. Event-driven: the plugins announce, the game reacts.

## Phase 1 — Foundations: languages, level-scaled persuasion, peaceful wins
- [ ] Languages: `"speaks"` on creatures, `"languages"` on classes; the Scholar speaks every language; slimes have none.
      Common (bandits, Brask, Wren, villagers: everyone) · Old Tongue (Ruin Brute: Mage, Scholar) · Grave-speech
      (skeletons: Scholar).
- [ ] Who can talk: a creature that speaks the hero's language and isn't provoked waits and watches instead of
      attacking; others attack as before. Reasons shown when you can't ("It speaks the Old Tongue. You don't.").
- [ ] Silver Words (mid-fight parley) follows the same language rule.
- [ ] Placeholder conversations for the skeleton and the Ruin Brute (real ones come in phases 3-4).
- [ ] Loom: level-scaled checks (`"level": { "min", "per" }`), a hook for the hero's level; below the minimum the
      check can't succeed; a failed check can be retried after levelling up.
- [ ] The bridge: Brask's honour challenge, hypnosis and negotiation get level floors.
- [ ] Peaceful wins pay: talked-down foes give at least their kill XP and never respawn.
- [ ] Tests: who can talk to whom per class; level-gated check and retry; peaceful XP.

## Phase 2 — World mood and ambient life
- [ ] Loom: a hidden world-mood value (starts at 0) with change / band events; data actions to move it.
- [ ] Weights: peaceful outcomes +, killing talkable creatures -, betrayal --, slimes 0; redemption possible but slow.
- [ ] Tessera: a generic ambient-life system: harmless critters and walkers that live in the world, flee or vanish
      near the hero, can't be fought; day / night spawn tables the game can re-weight.
- [ ] Move today's birds, geese, night prowler and fireflies onto it.
- [ ] The game bridges Loom's mood to Tessera's tables, applied at dawn / dusk (Tessera's day/night events).
- [ ] First effects: good = more geese, butterflies, children playing by day; bad = bats at night.
- [ ] Tests: mood moves with actions; tables follow the bands; critters flee the hero.

## Phase 3 — Skeletons (Scholar)
- [ ] Tessera: things you can interact with and talk to (graves, lost items, signs): a Loom conversation with an object.
- [ ] Lone skeletons: a small random pool of simple wants (gold...) plus hand-written fun ones.
- [ ] The unmarked grave: find it, read the rites (Scholar), mark it (a grave marker appears for good); back at the
      skeleton, it crumbles in peace (calm ghost).
- [ ] A skeleton band with a leader: the leader wants a lost item fetched; bring it back and the whole band leaves.
- [ ] Tests: each route end to end.

## Phase 4 — The Ruin Brute (Scholar; the Mage can Hypnotize)
- [ ] Move it into a cave with the relic.
- [ ] Wants food: Elder Maren parts with some after a kill-5-slimes quest; bring it, get the relic.
- [ ] High level: teach it a trade instead; it hands over the relic and from then on walks between its mine and the
      village carrying gold for food (Tessera: NPC routines on the day/night events).
- [ ] Tests: food route, teach route, the trade walk.

## Phase 5 — The full mood palette (all cosmetic, all flee the hero)
- [ ] Good: puppies, more children, flocks. Bad: grumpy men by day; wolves, bats, snakes at night.
- [ ] Tessera: adjustable building wear; puddles on unkept roads, wear and tear on houses.
- [ ] Villagers' tone and shop prices follow the mood (Loom text variants).
- [ ] Pixel art for all of the above.
- [ ] Tests: each band looks right (screenshots), changes land at dawn / dusk.

## Phase 6 — Proof runs
- [ ] A pacifist Scholar who only kills slimes finishes the bridge and the relic quests.
- [ ] A killing spree darkens the world; redemption slowly brings it back.
- [ ] Then: other classes' world-changing routes (Knight's honour duel, Thief stealing the relic...).

## Later / open threads
See `SO_FAR.md` §8 (fog of war, saves, music, Scholar and Knight polish, traits...).
