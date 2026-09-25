# Emberline: introductory campaign

Status: Agreed · Revision 2 · 2026-09-15

## Decision

Build a complete six-mission single-player campaign using the existing Cairn roster and rules. Teach the player to **observe, decide, command, and read the result**, progressively removing help. The story follows a salvage column restoring the Emberline against a rival Cairn splinter. Each mission restores another part of the route; the final mission is an ordinary match against Normal AI.

This request authorizes design and implementation of the campaign. It does not resume the broader P2 mechanics roadmap. Abilities, a second resource, new armor systems, tactical elevation, cinematics, voice acting, and new factions are outside this pass.

## Problem and evidence

The current 14-lesson tutorial teaches interface operation in one long match. Players have asked what a Drudge is, missed oversized instructions, and struggled with production, individual commands, rallies, and economy recovery. The campaign must teach decisions through observable outcomes, with short instructions and safe opportunities to recover.

Relevant code:

- [Existing tutorial](../Source/Cinderline/Public/Presentation/CinderTutorial.h) and its [single-action guidance](../Source/Cinderline/Private/Presentation/CinderTutorial.cpp).
- [Battlefield lifecycle](../Source/Cinderline/Private/Presentation/CinderBattlefield.cpp), which owns the simulation, pause, saves, and tutorial opponent.
- [HUD](../Source/Cinderline/Private/Presentation/CinderHUD.cpp), which already projects targets and chooses a card position around interactive controls.
- [Simulation API](../Source/Cinderline/Public/Sim/Simulation.h): ordinary paid commands, fog, statistics, production, resource rallies, save v13 and network protocol 10.

Scenario setup may use existing maps, default bases/resources, and initialization-only authored actors/resources. Current APIs cannot author partial health, depleted nodes, new obstacles, or hard unit bans. Siphons are ore delivery points and add crew capacity. Do not invent rescue, damaged-base, custom-ramp, or forced depletion mechanics.

Apple's [Onboarding for Games](https://developer.apple.com/app-store/onboarding-for-games/) recommends short, sequential lessons, increasing independence, replay and contextual reminders. This campaign applies that guidance to the existing RTS controls; the particular mission design is our implementation decision.

## Learning arc

All six missions are visible and playable for practice. **Continue recommended** leads to the first unfinished mission. Starting a later mission clearly lists assumed skills. Actual success completes only that mission; replay never removes progress. There is no skip-objective command or automatic player command.

1. **First Shift** — Operate a small base and win a first engagement. Demonstrate camera movement, selecting the mining robot, assigning ore, training one worker through the global catalogue, constructing a Kiln, and training Embers. Explain each term when it first appears. Coach attacking with the newly trained force, then prove the economy-to-army loop by defeating an authored patrol/post. No research lesson here.
2. **Keep the Fires Fed** — Maintain production as resources and crew become constraints. Demonstrate an Anchor resource rally and a Siphon. Coach relocating workers to a second reachable deposit and maintaining worker production. Prove the lesson by completing a remote delivery point and delivering ore from the new patch. Explain depletion, but do not require waiting for a particular node to empty. No timed expansion pass/fail gate.
3. **Eyes Beyond** — Learn what can be seen and why to scout. Demonstrate a Skim, explicit Move, and fog exploration. Coach finding two forward positions before committing the army. Prove the lesson by using an attack-move force to clear the forward threat. Route and force choices remain the player's. Previously taught production becomes goal-based guidance.
4. **Hold the Line** — Keep defending while reinforcements arrive. Demonstrate shared army rally, a Ward, and Defend. Coach preparing for two announced, bounded, paid enemy waves. Prove the lesson by surviving and clearing their attackers, then counterattacking if the scenario requires it. Formation spacing, queued orders, patrol and escort are optional field-guide practice rather than mandatory new quizzes.
5. **Break the Siege** — Scout, research, and choose suitable units. Demonstrate Resonator research and advanced production. Explain one existing counter clearly using actual unit definitions. Coach building a mixed force; support units are an application, not a separate examination. Prove the lesson by breaking the fortified position/default enemy Anchor. Alternate successful compositions count.
6. **Trial by Fire** — Win an ordinary Standard match against Normal AI. No new mechanic, scripted enemy director, resource bonus, compulsory build order, or automatic arrow. The objective is victory; optional hints remind the player to scout, expand, reinforce and research. Broad mastery observations record independent use of the economy and several combat roles without turning the match into a checklist.

Expected durations are playtest estimates, not guarantees: roughly 6–12 minutes for early missions, 10–20 for later missions. Tune from real novice play, not only automated wins.

### Phase predicates (implementation contract)

Phase numbers below are ordered, and entering each is a settled checkpoint boundary. Completed means alive, friendly and fully constructed/trained unless stated otherwise. Tutorial-only input gates require an accepted player command after the phase begins. Outcome phases accept work done early. Setup actors never satisfy a requirement to produce/build something new: compare mission-start IDs/counters, and require the result still to exist. Authored IDs are stable semantic roles resolved during setup, with their positions chosen from legal reachable sites on the selected map; record the resolved IDs/positions in checkpoints.

**First Shift — eight phases:** (1) deliberate camera pan/zoom observed; (2) select a friendly Drudge; (3) accepted Gather and actual delivery from that assigned worker; (4) a newly trained Drudge completes; (5) a player-built Kiln completes; (6) three completed player-trained Embers exist; (7) an accepted AttackMove by combat troops; (8) the initialization-authored practice patrol is dead. Phase 8 is mission victory. The opponent holds position until approached; player losses can be replaced. No default Anchor destruction required.

**Keep the Fires Fed — six phases:** (1) an Anchor resource rally is set to a live visible ore node through an accepted command; (2) a worker trained after that rally completes and is assigned to its ore; (3) a new Siphon completes, increasing capacity; (4) an accepted Gather relocates a worker to a different patch from the starting patch; (5) a new Siphon completes within delivery distance of that destination patch; (6) actual ore delivery from a worker gathering that patch occurs after the remote Siphon is operational. Phase 6 wins. If phase 3 already placed the remote Siphon, it also satisfies phase 5. Identify a legal destination with enough separation to teach relocation; if its node empties, retarget a live reachable node in the same remote area or a new area and update guidance. Do not require a specific worker to survive or wait for depletion.

**Eyes Beyond — five phases:** (1) a newly trained Skim completes; (2) accepted Move by a Skim and it reaches the first scout zone; (3) both authored forward zones have been explored; (4) accepted AttackMove by a combat force; (5) both authored forward threats are dead, regardless of the later killing command. Phase 5 wins. Discovery is remembered. A dead scout is replaceable; threats destroyed early count when their proof phase is reached.

**Hold the Line — five phases:** (1) shared army rally set through accepted AutoRally; (2) a new Ward completes; (3) accepted Defend by friendly combat troops; (4) survive the first announced paid wave and clear its attackers; (5) survive the second announced paid wave and clear its attackers. Phase 5 wins. The initial setup supplies the producers/starting defensive force needed for the lesson; later units are paid. Each wave has an explicit finite training budget, stable produced IDs and a finished-production flag. Destroying its producer closes remaining production; all existing attackers and pending paid output must be accounted for before clearing a wave. The second wave is announced only after the first is cleared, with preparation time. No building repair command is taught because it does not exist.

**Break the Siege — six phases:** (1) explore the authored fortified position; (2) a player-built Resonator completes; (3) tier 2 research completes; (4) a weapons upgrade completes; (5) a player-built Crucible completes and at least one advanced frontline/support unit is trained; (6) destroy the enemy Anchor. Phase 6 wins. The scouted threat contains armored units: explain Needle piercing against armor and Anvil frontline/Mend support using existing definitions. Guidance suggests a mixed force but an alternate victorious composition still wins. No required unit survives as an irreplaceable named actor.

**Trial by Fire — one outcome phase:** initialize an ordinary two-player Standard configuration with Normal AI and no authored bonuses. Player victory (`winner == 0`) completes the mission. Observe optional scout/expansion/research/mixed-force achievements without requiring their sequence. Do not gate this mission behind artificial phases merely to increase a phase count.

For every mission, loss is player elimination, enemy victory or a draw; an ordinary early player victory also completes the mission, preventing frozen remaining objectives. Learning prompts can mention unfinished practice, but never withhold a real win. There is no timeout defeat. Failed required economy/production has recovery hints and checkpoint retry. Predicates must not pass on rejected commands, queued-but-unfinished work, unrelated workers' deliveries, or setup actors alone.

## Guidance and accessibility

- **Demonstrate:** one instruction, one action, one exact arrow. Use plain names such as “Drudge — the robot that mines ore” before shorthand.
- **Coach:** show a goal and progress. HINT explains why and reveals the next single action; after a stall, pulse the hint affordance rather than moving the camera or commanding units.
- **Prove:** outcome-only goal. Hints are optional and mark the attempt Assisted for mastery; they never prevent completion. The final match never receives compulsory arrows.
- Later missions recall already-taught concepts without repeating mandatory catalogue clicks. Completion follows actual results; accepted command provenance is required only where a specific new input is the lesson.
- One compact objective card, one progress line, and HINT/SHOW. Expand explanations on demand. Do not make players read paragraphs over a live battle.
- SHOW focuses a world target only after a tap. Arrows/rings are noninteractive. The card must avoid the actual tap target and controls, including open drawers.
- Preserve comfortable touch targets, phone landscape orientations, and keyboard/mouse use. Campaign menu, briefing and result content respect existing safe insets; gameplay remains full bleed.

## Screens and flow

Main menu → CAMPAIGN → six mission tiles and selected briefing → START/REPLAY → mission → debrief → NEXT/REPLAY/MISSION SELECT.

Briefings contain a short story beat, skill promise, up to three outcome summaries, and assumed skills. The existing quick tutorial stays accessible; Campaign becomes the preferred new-player offer.

Pause offers resume, retry checkpoint, restart mission, field guide, and mission select. Defeat retains a concrete reason and offers retry/restart/select. Victory shows completion, an assistance indicator, and useful feedback; optional marks never block progression. Campaign completion offers skirmish next. No forced online/account flow.

## Technical design and ownership

Add `FCinderCampaign`, an independent director with mission definitions, phase state, accepted-command observations, authored IDs, result, and enemy-wave state. Keep predicates and scenario setup separate from HUD rendering. Share the existing tutorial guidance types/projection path rather than maintaining a second input coordinate system.

`ACinderBattlefield` owns exactly one active learning mode. Starting, loading, retrying, returning to menu, or joining online resets incompatible director state. Campaign terminal results stop simulation updates and command submission even when the simulation's Anchor victory condition has not fired. Do not alter core victory rules. On a simultaneous loss/objective result, player elimination wins precedence.

The controller forwards accepted offline commands, selection observations and deliberate camera input to the active director. Rejected commands do not advance lessons. The HUD consumes immutable/read-only presentation and never completes objectives itself.

Scenario initialization is transactional. Author positions only where footprint/access checks permit them; use existing map lanes. After initialization, enemy units come from normal paid training and ordinary orders. Pressure is bounded, announced, and starts after relevant preparation. No hidden runtime spawning, free refills, or player invulnerability. The finale uses the normal AI unchanged.

## Persistence, performance and privacy

Store campaign metadata and objective-boundary checkpoints separately under the project's Saved directory. Never overwrite `Matches/skirmish.cinder` or change simulation save v13/network protocol 10.

Checkpoint consists of a normal simulation save plus a schema-versioned director record. Commit an A/B generation only after both files validate; a manifest selects the settled generation. Check mission, schema, tick/hash, phase, authored IDs, counters and timers before replacing live state. If the newest generation is torn/corrupt, try the prior generation; otherwise offer restart. Retry restores the complete settled objective boundary, not only its phase number. Mobile resume means the last saved boundary, not arbitrary every-frame state.

Completion metadata is monotonic across replay/out-of-order practice, with per-mission completion and assistance/mastery information. Loss never grants completion. Corrupt/unsupported metadata uses a safe fallback with a visible message. Keep telemetry local: mission transitions, retries, hints and diagnostic logs. No analytics service or backend dependency.

Observe campaign state only while gameplay is active, on simulation progress. No disk writes or expensive route searches every frame. Save only settled boundaries/results; no work while menu/paused/terminal. Serialize builds and use at most two compile jobs to respect the user's thermal constraint.

## Failure and recovery

- Cancelled production, killed workers/units and abandoned foundations must return to actionable guidance. Prefer current world state over permanently latched “order sent” booleans.
- An early-destroyed objective counts as cleared once earlier lessons are satisfied. Never ask the player to attack an already-dead target.
- If the opponent is eliminated before remaining instructional goals, finish successfully with appropriate feedback; never leave an uncommandable frozen match waiting for a lesson.
- Required resources/roles must remain replaceable. No mandatory named-unit survival. If an economy becomes irrecoverable, explain retry rather than awarding success.
- Announced waves use ordinary production; destroyed producers cannot leave an eternal “wait for wave” objective. Account for eliminated threats and pending queues explicitly.
- Campaign/scenario/checkpoint load failures preserve the current match until a replacement fully validates. Restart is always available.

## Delivery and acceptance

1. Agree this design, freeze director/UI/persistence contracts.
2. Implement all six authored missions and result predicates, normal paid opponent scripts, guidance and recovery.
3. Integrate campaign selection, briefings, compact objective card, pause/debrief, progress and checkpoints.
4. Verify scenario determinism, successful and false-positive objectives, normal-command completion, destroyed/replaced targets, wave bounds, loss precedence and finale AI configuration.
5. Verify checkpoint restore/fallback, corrupt metadata, replay/out-of-order progress, save isolation, lifecycle/terminal/pause/input guards, and original tutorial regression.
6. Build Mac; run focused then applicable existing automation serially. Inspect actual compact/mobile and desktop campaign captures, including target obstruction. Package Android after source checks when available. Record physical-device and novice-play evidence separately.

Source/automation/package checks do not prove learning quality or phone performance. Human acceptance: newcomers finish the early missions without needing external instructions, can recover from a lost worker/queue cancellation, and can explain why they scouted, expanded, rallied and changed composition. Completion/retry rates are playtest tuning inputs, not claims made by this implementation.

## Resolved disagreements and assumptions

- Six missions balance the UI's four-chapter proposal and UX's eight. The four-chapter finale was overloaded; a full 14-lesson opening repeated the existing problem.
- Practice access is unrestricted; completion/mastery remain honest. No skip or Show action performs gameplay for the player.
- Checkpoints save existing simulation plus director state, rather than only metadata or invented phase reconstruction. Boundary resume is explicitly labeled.
- Story carries only campaign progress between missions; each mission starts with its own stable scenario.
- No new art or renderer required for the learning experience. Existing maps/roster remain the scope.
- Device gesture correctness, thermal behavior and mission difficulty need later physical and novice playtests. Failures discovered locally must be fixed before delivery.

## Consensus record

Two deliberation rounds and two ratification rounds completed. Revision 1 was blocked on missing concrete phase predicates; revision 2 resolved that issue. All four roles explicitly approved revision 2:

- Product: APPROVE — implementable predicates, valid early victories and recovery, existing rules, unchanged Normal-AI finale.
- UX: APPROVE — observable learning outcomes, short sequential guidance, recoverable mistakes and unguided transfer.
- UI: APPROVE — compact single-action guidance, honest practice access, safe menu states and full-bleed gameplay.
- Engineering: APPROVE — current command/state APIs, paid production, isolated checkpoints and director-owned outcomes.

## Implementation notes

The optional HINT button stays available without an automatic arrow. SHOW and demonstrated actions retain pointers; the final match remains free of compulsory guidance. A delayed hint pulse is deferred pending novice playtests.

Recovery now checks a replacement mine and Siphon site for reachability, retries after new workers become available, and credits a final ore delivery even when automatic mining allocation has already changed the worker's target. Enemy raids advance through normal paid production with finite three- and four-unit budgets.

## Implementation tracker

- [x] Six missions and normal-rule objective validation.
- [x] Guidance, recovery and bounded paid enemy pressure.
- [x] Campaign menu, briefings, pause and debrief.
- [x] Monotonic progress and validated boundary checkpoints.
- [ ] Local automated and visual verification.
- [ ] Packaged build validation.
- [ ] Physical-device and novice playtests (separate acceptance).
