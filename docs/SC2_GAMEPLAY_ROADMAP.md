# Cinderline gameplay capability roadmap

Updated: 2026-09-15. Overall status: **stopped after P1 implementation; acceptance partial; capability parity has not been reached**.

Execution boundary requested by the player on 2026-09-14: finish the current P1 tactical-command work, including patrol/escort and formation controls, then stop. Do not begin P2 or later milestones without a new instruction. P0 remains partial; unresolved latency spikes and physical CPU/GPU/thermal/full-match checks stay visible. P2–P7 below remain the longer-term roadmap, not authorization to continue past this stop point.

## Goal and scope

Bring Cinderline's competitive RTS gameplay capabilities to a level comparable with StarCraft II: precise army control, varied tactical interactions, meaningful economies and terrain, three asymmetric factions, capable opponents, reliable multiplayer, and tools for learning from matches. Keep Cinderline's original setting, assets and rules. Use SC2 as a capability benchmark, without copying its content or assuming that adding the same number of features establishes equal quality.

The mobile command model stays: permanent Build, Train, Research and Army access; automatic work allocation; explicit manual overrides; readable touch targets and optional detail. Players choose strategy, position and timing. Automatic allocation must remain predictable and preserve explicit orders.

This is the current priority and acceptance ledger. [TASKBOARD.md](TASKBOARD.md) remains the entry point and historical evidence index. Individual pass reports retain evidence for their own revisions. Update this file whenever a milestone changes; do not replace it with another disconnected roadmap.

Campaigns, cinematics, co-op campaign content, an Arcade/mod editor, cosmetic progression and frame generation are outside this competitive-gameplay goal. They remain possible later projects. Team modes beyond four players are included below as a later capability target, with a separate device-capacity gate.

## Status rules

- **Done:** the named bounded deliverable has its required evidence. It does not mean every related future capability is complete.
- **Partial:** some implementation or acceptance exists; list what remains.
- **Pending:** work has not been delivered.
- **Active:** the next bounded work item being pursued. Keep the active list small.
- **Blocked:** name the actual dependency and the work that can continue independently.
- Check a milestone box only when all its acceptance criteria pass. A build, automated test, render capture, installation and human playtest are different kinds of evidence.
- Every completed milestone must record source revision or manifest, applicable package/protocol, checks, evidence paths and unresolved limitations. Tests from an earlier revision remain historical unless rerun or their continued applicability is established.

## Verified starting point

Baseline recorded from current source and pass reports on 2026-09-14. These are existing implemented capabilities, not a claim that all have passed current-package physical acceptance.

| ID | Existing deliverable | Implementation | Evidence and remaining acceptance |
| --- | --- | --- | --- |
| B01 | Fixed-step shared simulation; finite ore, supply, construction, production, research, combat, fog and saves | Done | [Mechanics](MECHANICS_PASS.md), [code quality](CODE_QUALITY_PASS.md). Current rule set has one faction, eight mobile types including Drudge, six building types, one harvested resource and three technology tiers. Broader strategic depth remains below. |
| B02 | Builders travel to sites, stop mining, resume interrupted work and return to mining | Done | [Construction](CONSTRUCTION_PASS.md), [navigation](NAVIGATION_PASS.md). Current-package crowded-site physical playtest pending. |
| B03 | Global Build/Train/Research, automatic assignment, visible jobs, cancellation and queues up to 20 per producer | Done | [Command allocation](COMMAND_ALLOCATION_PASS.md), [queues](PRODUCTION_QUEUE_PASS.md). Do not reimplement these as missing features. Current-package touch acceptance pending. |
| B04 | Individual selection/deselect, army roster, three squads, Defend, shared army rally and producer overrides | Done | [Army commands](ARMY_COMMAND_HUD_PASS.md), [rally controls](ARMY_RALLY_CONTROL_PASS.md). [P1.1 waypoint queues](TACTICAL_ORDERS_PASS.md) are locally verified; matching-package physical acceptance, patrol and broader formation behavior remain pending. |
| B05 | New workers automatically choose reachable explored ore; explicit worker-rally overrides take precedence | Done | [Rally controls](ARMY_RALLY_CONTROL_PASS.md). Broad physical congestion acceptance pending. |
| B06 | Clearance-aware routing, work slots, construction preflight, traffic yielding, bounded recovery and route-failure feedback | Done | [Navigation](NAVIGATION_PASS.md), including a 160-unit crossing fixture with all arrivals. That fixture does not establish every large-battle case. |
| B07 | Ground/air roles, anti-air, armor, siege, automatic healing, attack-move and combat feedback | Done | [Definitions](../Source/Cinderline/Public/Sim/Simulation.h), [combat feedback](COMBAT_FEEDBACK_PASS.md). Player-triggered abilities and several tactical interactions remain pending. |
| B08 | Scouting-informed paid AI, five difficulties, first-run guided match and tutorial microsteps | Done | [AI strategy](AI_STRATEGY_PASS.md), [difficulty](AI_DIFFICULTY_PASS.md), [guided match](GUIDED_MATCH_PASS.md), [microsteps](TUTORIAL_MICROSTEPS_PASS.md). Human difficulty calibration and current tutorial acceptance pending. |
| B09 | Three map layouts; Short/Standard/Long solo presets; bounded black map edges; authored canyon relief | Done | [Match length](MATCH_LENGTH_PASS.md), [borders](MAP_BORDER_PASS.md), [canyon](CANYON_HUD_PASS.md). Gameplay movement remains level; tactical elevation is pending. Online remains Standard. |
| B10 | Compact HUD, portraits, information/range views, touch camera and menu-only safe-area layout | Done | [HUD](HUD_OVERHAUL_PASS.md), [army commands](ARMY_COMMAND_HUD_PASS.md), [iOS readiness](IOS_READINESS.md). The player accepted an earlier menu layout; current-package full-match touch acceptance remains pending. |
| B11 | Authoritative private 1v1 and four-player FFA; LAN discovery, ready/reconnect/elimination and private fog | Done locally | [Four-player](FOUR_PLAYER_PASS.md), [LAN](ONLINE_LAN_PASS.md), [server](../Server/README.md). [P1.1](TACTICAL_ORDERS_PASS.md) advances current local clients/workers to protocol 8 with two/four-player Unreal loopback tests. Public deployment, physical multi-device matches, durable rooms/results, accounts and ranking remain pending. FFA is not 2v2. |
| B12 | Command recording, deterministic checks and save/load | Partial | [Simulation](../Source/Cinderline/Public/Sim/Simulation.h), [quality pass](CODE_QUALITY_PASS.md). Recording exists; a replay viewer and spectator experience do not. |
| B13 | Menu world-render suppression, thermal/Low Power policy, capped pacing and MetalFX spatial upscaling | Done locally | [Thermals](IOS_THERMAL_PASS.md), [MetalFX](METALFX_UPSCALING_PASS.md). Current physical CPU/GPU, memory, battery and sustained thermal acceptance pending. MetalFX frame generation is not active. |
| B14 | Baseline iPhone package installed on AEON | Done for installation | Build `1.0 (56702186.0.35)`, executable SHA-256 `409069397be77bcef7f857026f8bd546fbc1a91688f1b44276dc3f839b28ba78`. [Install receipt](../artifacts/aeon-latest/install-verification.json). Existing Documents retained; left closed. This package predates the P0 movement fixes below. Installation adds no physical gameplay or thermal result. |

Last packaged compatibility baseline: save version 10 and network protocol 7. The verified P1.1 local build uses save version 11 and protocol 8. P1.2 save12/protocol9 is preserved in its source/binary archive with passing targeted feature checks, but its combined paid-four-player workload gate remains unresolved. P1.3 save 13/protocol 10 now has its own [local feature verification](FORMATION_ORDERS_PASS.md); it has not inherited physical or combined-workload acceptance from earlier receipts. Client, server and worker must move together. No deployment or device update is included in these passes.

## Milestones and priority

All milestones below are open. Implement in small reviewed passes, preserving the baseline above. P0 and P1 establish the control and performance baseline; P2 and P3 deepen the first faction; P4 expands strategic variety. The later P5 multiplayer, P6 playback and P7 combined acceptance milestones remain planning context only; the current instruction stops execution after P1.

### P0. Establish measurable match quality

Status: **Partial; work stopped at the requested P1 boundary**. Bounded passes now cover the [baseline](GAMEPLAY_BASELINE_PASS.md), [order completion](ORDER_COMPLETION_PASS.md), [legal match instrumentation and routing cache](MATCH_SCALE_PASS.md), and [production exits and traffic recovery](PRODUCTION_EXIT_PASS.md). They repair displaced formation recovery, moving-target retries, interrupted worker delivery, disconnected production exits and the recorded two-unit friendly-traffic jam. The pass reports retain their own source manifests and before/after evidence.

The preceding production/traffic revision passed all 15 portable suites, all 15 sanitizer suites, 32 server tests, the Mac Unreal build and 37 Unreal integration checks. All 160/200/400 synthetic arrivals pass with deterministic repeats. The fresh paid four-player workload prepares 100 mobile units/184 crew per seat, reaches all 320 original army destinations and completes combat with matching state, trajectory and recording repeats. The fresh two-player workload also reaches all 160 original army destinations and completes combat with matching full repeats. The [verification record](../artifacts/production-exit/verification.json) records the exact tested inputs, reviewed concurrent packaging edits and all local results.

P0 remains open. The [terrain visibility pass](NAVIGATION_VISIBILITY_PASS.md) reduces complete paid two/four-player preparation peaks from about 137/221 to 71/145 ms after the endpoint-check pass. All 17 Release suites, 17 sanitizer suites, 32 server tests, the current Mac build and 37 Unreal checks pass. Both complete workloads preserve every non-timing result and deterministic repeat, and synthetic 160/200/400 arrivals remain exact. The remaining 145 ms spike still needs work: current CPU traces identify dynamic circle checks and cold terrain intersections. Actual input/network latency, renderer costs, supported-device limits, a complete physical match and sustained iPhone CPU/GPU/thermal acceptance remain open. Current measurements cover desktop simulation and fog-filtered snapshot CPU costs, not phone rendering or an SC2 parity claim.

- [ ] **P0.1 Command reliability:** cover single and mixed selections, friendly actors crossing a tap, deselection without canceling orders, crowded destinations, narrow turns, repeated retreat orders, construction blocking, mining congestion, completed worker assignment and rally inheritance. Reuse existing fixtures; add only missing behavior coverage. Every command must either execute or explain why it cannot.
- [ ] **P0.2 Scale and responsiveness:** define supported entity counts before benchmarking. Include the existing 160-unit route case, a proposed 200-unit mixed-army case and a proposed 400-unit four-seat case. Record simulation p50/p95/p99, route deferrals/failures, command-to-state latency and arrivals. The two larger counts are test targets, not supported-device claims. Accept only after all reachable units arrive or retain a documented valid order, with no permanent deadlock or unexplained order loss.
- [ ] **P0.3 Sustained device budget:** measure CPU and GPU frame times separately, frame pacing, memory, battery change and thermal state during a complete Standard match on AEON. Include crowded combat, menu, pause and background/resume. Start with a provisional 30 rendered FPS floor at the supported quality tier, p95 frame time at or below 40 ms, and a 20-minute soak or repeated workload if the match ends earlier. Record ambient/charging/initial thermal conditions. Any sustained serious/critical thermal state, crash or progressive memory growth requires investigation before acceptance. Treat 60 FPS as an additional measured tier.
- [ ] **P0.4 Physical full match:** exercise both landscape orientations, pan/pinch, single/group selection, build/train/research, cancel, rally, defense, pause/resume and a normal win or loss on the exact package. Record player friction and defects. Older user confirmation remains useful context, not a substitute for this run.

Exit: publish the baseline and resolved regressions with exact build evidence. Device availability blocks only device capture, not local fixtures, instrumentation or fixes. Do not force-relaunch a match the player has started.

### P1. Complete the tactical command model

Status: **Implementation and local feature checks complete; acceptance partial; work stopped**. [P1.1 tactical orders](TACTICAL_ORDERS_PASS.md) is implemented and locally verified: 16-step per-unit routes, Shift/touch append, numbered waypoints, clear/stop/replace, worker/support completion, save 11 and protocol 8. Portable/sanitizer, server, Unreal, loopback, synthetic and paid workload gates pass. Matching-package physical input and full-match acceptance remain open. P1.2 is implemented with targeted local evidence and an unresolved paid-workload isolation gate. P1.3 facing/spacing and P1.4 feedback are implemented with [current local evidence](FORMATION_ORDERS_PASS.md): 21 Release suites, 21 sanitizer suites, 35 server tests, the Mac build, 40 Unreal tests, two real loopback tests, repeated formation/queue/sustained/navigation workloads and 12 reviewed captures. Physical and combined paid-workload acceptance stay open. Work has stopped here; P2 is deferred.

- [ ] **P1.1 Order sequences:** implementation and local checks complete; [acceptance ledger](TACTICAL_ORDERS_PLAN.md#acceptance-ledger) retains real mouse/touch and complete-match checks. Supports explicit queued Move/Attack-move, visible waypoints, append/replace/cancel, desktop Shift and touch Queue next, with bounded routes and defined interruption/target-loss behavior.
- [ ] **P1.2 Patrol and escort:** [implemented with local feature evidence](PATROL_ESCORT_PASS.md); repeating patrol and owned-unit escort have explicit target-loss and pursuit limits. Combined paid-workload isolation and physical acceptance remain incomplete. Stop, Hold and direct-move semantics are retained; the final P1 Defend contract permits yielding followed by anchor reclamation. Current targeted local checks pass; the combined paid-workload gate and physical acceptance remain open.
- [ ] **P1.3 Group movement and defense:** implementation and local checks complete; [acceptance checklist](FORMATION_ORDERS_PLAN.md#acceptance-checklist) retains physical mouse/touch and full-match checks. Tight/Standard/Wide spacing, drag-facing, mixed-speed independent travel and replacing-Move withdrawal adapt to footprints, terrain and explicit Holds. Existing squads use the same TACTICS sheet. This does not add a rigid marching block or shared speed.
- [ ] **P1.4 Feedback:** implemented across P1.1–P1.3; current order, numbered route, route-blocked status and on-demand details are available. Targeting is cancellable, ordinary friendly selection is distinct from an explicit destination command, and global production remains accessible. [Coverage](FORMATION_ORDERS_PASS.md#p1-feedback-coverage); matching-package physical acceptance remains open.

Exit: every new order passes simulation, fog/ownership, save/load, replay and multiplayer checks; stress routes recover; desktop and physical touch runs demonstrate the same intent without accidental orders.

### P2. Add tactical combat depth

Status: **Pending**. Depends on P1 targeting and queue semantics.

- [ ] **P2.1 Ability system:** define shared ability data and authoritative commands with target type, cost, cooldown, range, line of sight, interruption and visible feedback. Define autocast where appropriate and a manual override. Implement original abilities that create distinct decisions, such as a timed defensive shield, suppression and a deployed siege mode; exact designs require a short rule/counter brief before coding.
- [ ] **P2.2 Damage and counters:** make armor classes, damage bonuses, shields and area damage explicit and inspectable. Define projectile travel versus instant-hit rules, splash/friendly-fire policy and death timing. Existing tracer animation must not silently become authoritative damage.
- [ ] **P2.3 Information warfare:** add stealth/detection and temporary vision where they create useful counterplay. Hidden targets, effects, sounds and spectator views must respect the same rules.
- [ ] **P2.4 Mobility and support:** support transport/load/unload and useful deployment or transformation mechanics. Define cargo loss, valid unload space, mode-switch interruption and repair/heal interactions.
- [ ] **P2.5 Readable battles:** each ability and unit role has a distinguishable silhouette/cue at ordinary phone zoom, understandable range and concise counter information. Update the tutorial with one action per step.

Exit: each new mechanic has a documented counter and cost; interaction tests cover death, cancel, simultaneous effects, lost vision, invalid targets and network validation. Human matches must show more than one useful army composition. Do not count an ability menu alone as combat depth.

### P3. Make economy and maps strategically consequential

Status: **Pending**. Map geometry, simulation and rendering must share explicit rules.

- [ ] **P3.1 Economy:** introduce a second harvested strategic resource alongside ore and crew, with a distinct role in technology/advanced production. Design resource income, worker allocation, transfer and saturation feedback together. Preserve automatic allocation and manual overrides; expose shortages clearly.
- [ ] **P3.2 Technology choices:** add branching research and meaningful commitments rather than only higher tier/weapon/armor numbers. Define scouting tells and responses to each branch. Carry all costs and progress through jobs, cancellation, AI, saves and multiplayer.
- [ ] **P3.3 Elevation and vision:** implement authored height levels, ramps and deterministic line-of-sight rules. Specify uphill visibility, air vision, cliff movement, building placement and effect visibility before changing terrain assets. Validate reachability and avoid revealing units through scenery.
- [ ] **P3.4 Map control:** design defensible starting areas, exposed expansions, flanking routes, chokes and contested vision/resource positions. Add destructible blockers where they produce a useful route decision. Validate symmetry or intentional asymmetry for every spawn and player count.
- [ ] **P3.5 Pacing:** tune Short/Standard/Long using completed human matches and economy traces. Distinguish resource exhaustion from pathing stalls and excessively slow cleanup. Extend online preset choice only after admission, snapshot and balance coverage exists.

Exit: representative maps support expansion, scouting, harassment and multiple attack routes in human play; height/vision/navigation/placement tests agree; there is no visual-only claim of tactical elevation. Record measured match lengths instead of asserting the old 20–30-minute aspiration as achieved.

### P4. Build three asymmetric, viable factions

Status: **Pending**. First complete the Cairn faction's rules and counter relationships through P2/P3.

- [ ] **P4.1 Cairn identity:** publish its strengths, weaknesses, economy, production constraints, technology branches and at least three intended openings with scouting tells and responses.
- [ ] **P4.2 Second original faction:** deliver distinct economy/production behavior, roster, technology and tactical mechanics. Share common systems without forcing faction differences into duplicated simulation loops.
- [ ] **P4.3 Third original faction:** meet the same bar. Colour swaps or renamed Cairn definitions do not satisfy asymmetry.
- [ ] **P4.4 Matchups:** cover all six unordered 1v1 matchup types, including mirrors, with both spawn positions. Verify legal starts, costs, unlocks, counters, AI, tutorial/reference content, replay and protocol behavior.

Exit: three complete factions can finish matches through multiple strategies. Publish a predeclared human test matrix by matchup, map and player skill; collect at least ten exploratory completed matches per matchup before the first balance review. That small sample finds problems and does not prove statistical balance. Continue collecting results and report uncertainty.

### P5. Deliver a reliable competitive multiplayer product

Status: **Partial foundation**. Private 1v1/FFA and LAN already exist; the following extensions remain open.

- [ ] **P5.1 Public service:** deploy a version-compatible TLS service with health checks, resource limits, logs/metrics, controlled rollout and rollback. Complete real-device 1v1 across separate Internet networks and four-device FFA; separately verify LAN discovery, permission, manual join and a full match on another machine/router.
- [ ] **P5.2 Durable identity/results:** add persistent player identity, room recovery policy, durable match records and reconnect credentials. Use stable match IDs and exactly-once result/rating application. Test server/worker restart, duplicate completion, stale reconnect and abandoned matches. Publish whether an interrupted match resumes or becomes an explicit no-contest.
- [ ] **P5.3 Matchmaking and ranking:** add casual/ranked queues, appropriate rating and placement, region/latency selection and match history. Test cancellation, admission races, mismatched versions and disconnect penalties before enabling ratings.
- [ ] **P5.4 Adverse-network behavior:** predeclare latency/jitter/loss/disconnect profiles and measure command responsiveness, recovery and result consistency. Test abusive command rates, ownership, fog privacy and replay leakage. Authoritative workers are a foundation, not complete anti-cheat proof.
- [ ] **P5.5 Teams and lobby options:** add 2v2 alliances, shared-vision policy, team victory, configurable human/AI slots and faction selection. Define ally collision, targeting, resource-sharing policy and surrender. Later add 3v3/4v4 only with explicit supported-device and server-capacity tests; this remains pending until accepted, not silently excluded from the longer-term team capability target.
- [ ] **P5.6 Release compatibility:** test coordinated client/server/worker upgrades, version admission and in-flight match draining or recovery. Preserve committed match results and ratings across upgrades; retain compatible replay readers or publish an explicit replay-support/expiry policy. An incompatible release must not silently discard results, apply ratings twice or admit clients that cannot interpret the match.

Exit: completed physical LAN and Internet matches, durable correct results, exercised restart/reconnect policy and measured supported capacity. Deployment is separate from local transport tests. Avoid introducing paid infrastructure commitments until a concrete deployment choice is reviewable.

### P6. Improve opponents, learning and match analysis

Status: **Partial foundation**. Five AI levels, fog-based scouting and command recording already exist.

- [ ] **P6.1 Replay viewer:** persist versioned match metadata, initial state and accepted commands; implement playback, pause/speed, seek and player fog views. Verify playback reaches the recorded final state, reject incompatible data clearly and retain compatible historical readers where feasible.
- [ ] **P6.2 Spectators:** add permissioned or delayed observation and observer controls, with explicit live-match privacy rules. Eliminated FFA players currently receiving survivor counts do not already have spectator vision.
- [ ] **P6.3 AI repertoire:** extend the existing scout/counter opponent with several openings, harassment, expansion defense, retreats and ability/transport/elevation use. Make each faction playable by AI. Keep resource and vision rules explicit; difficulty must not secretly change unit stats or reveal hidden state.
- [ ] **P6.4 Coaching:** use replays and after-match summaries to explain idle resources, lost workers, expansion timing and composition mistakes. Teach new mechanics through short guided scenarios. Validate that a new player can complete training and explain the main controls without outside instruction.

Exit: deterministic replay checks, privacy-safe observation, human-tested tutorials and measured AI behavior across difficulties. Automated victory alone does not establish opponent quality or a monotonic human difficulty curve.

### P7. Close the capability and quality gap through play

Status: **Pending**, recurring after each earlier milestone and a final combined gate.

- [ ] **P7.1 Balance ledger:** record map, factions, spawn, skill estimate, strategy, duration, expansion/technology timing, unit losses, outcome and player feedback for completed matches. Separate AI simulations from human results. Identify dominant strategies and ineffective units with uncertainty stated.
- [ ] **P7.2 Controls and readability:** resolve repeated player mistakes caused by interface ambiguity. Test small/large supported phones and tablets; evaluate selection, commands and combat at ordinary zoom. Preserve unobstructed world input and accessible touch targets.
- [ ] **P7.3 Performance/reliability:** repeat the P0 sustained device matrix at intended maximum armies and supported multiplayer counts. Attribute CPU and GPU costs, set supported quality tiers, investigate crashes and test lifecycle recovery. A brief uncapped FPS sample cannot close this gate.
- [ ] **P7.4 Capability review:** audit every milestone and unresolved defect. Complete matches demonstrating resource denial, scouting/counterplay, ability timing, positional advantage, diverse viable compositions, faction matchups and online recovery. Record an independent gameplay review and the player's acceptance; technical feature coverage alone does not establish SC2-quality play.

Exit: all included capability milestones accepted with current evidence and no unresolved release-blocking command, privacy, crash or sustained-performance defect. Report capability coverage and remaining quality differences explicitly; do not invent a percentage of SC2 parity.

## Immediate queue

| Order | Work | Status | Next evidence |
| --- | --- | --- | --- |
| 1 | P0.1/P0.2 inventory, synthetic Release baseline and two demonstrated movement fixes | Locally verified | [Coverage, before/after and remaining scope](GAMEPLAY_BASELINE_PASS.md) |
| 2 | Direct/shared-target completion, fog-transition preservation, depleted ore and interrupted depot delivery; pointer-created mixed-selection coverage | Locally verified | [Order-completion pass](ORDER_COMPLETION_PASS.md), before/after regressions and current engine/portable/sanitizer/server evidence |
| 3 | P0.2 legal mixed-army workloads, CPU phase attribution and snapshot costs | Locally verified bounded pass; P0.2 partial | [Legal match workload pass](MATCH_SCALE_PASS.md): paid 100-unit/184-crew armies per player, all original march arrivals, matching complete trajectories, and same-source before/after captures; peak latency, broader limits, renderer and device acceptance remain open |
| 4 | P0.3/P0.4 current-package physical match and sustained profile when AEON is available | Pending device session | Exact package, complete match log, CPU/GPU/frame/memory/thermal record and player feedback |
| 5 | P0 production exits, crowded movement and long navigation-query steps | Locally verified bounded pass; P0 remains partial | [Production exit pass](PRODUCTION_EXIT_PASS.md) adds reachable exits, worker route checks and paid-ready backoff. The recorded two-unit traffic jam now passes a fixed-goal regression, and a fresh four-player run reaches all 320 army destinations with matching full repeats. All local gates and both fresh paid workloads pass in the linked verification record. Next capture peak call stacks before changing cold-route/preflight work |
| 6 | P0.2 peak simulation latency | Portable verified; current integration covered by pass 7 | [Navigation latency pass](NAVIGATION_LATENCY_PASS.md): exact CPU stack attribution and redundant endpoint-check removal. Complete paid workloads preserve every non-timing result; two/four-player peak steps fall from 183/308 to 137/221 ms. Remaining collision cost and device gates stay open; pass 7 verifies the current engine integration |
| 7 | P0.2 terrain visibility reuse and exact cold box checks | Locally verified bounded pass; P0 remains partial | [Navigation visibility pass](NAVIGATION_VISIBILITY_PASS.md): 17 Release + 17 sanitizer suites, 32 server tests, Mac build and 37 Unreal checks; exact oracle/routes, complete workloads and repeats. Two/four-player peaks are 71/145 ms. Next CPU work targets circle checks and cold intersections; physical acceptance remains open |
| 8 | P1.1 order sequences and route feedback | Locally verified; physical acceptance pending | [Tactical order pass](TACTICAL_ORDERS_PASS.md), [verification](../artifacts/tactical-orders/verification.json); 19 Release + 19 sanitizer suites, 34 server tests, 38 Unreal checks, two real loopback tests, four HUD captures and complete deterministic workload repeats |
| 9 | P1.2 patrol/escort, P1.3 facing/spacing and P1.4 feedback | Local feature checks complete; combined paid-workload and physical acceptance incomplete; work stopped | [Final P1 report](FORMATION_ORDERS_PASS.md), [verification](../artifacts/formation-orders/verification.json); 21 Release + 21 sanitizer suites, 35 server tests, Mac build, 40 Unreal tests, two real loopback tests, complete synthetic repeats and 12 reviewed captures. P2 remains deferred |
| 10 | Basic P6 replay playback and P5 public-service planning | Deferred by the player's stop-after-P1 boundary | Requires a new instruction before implementation or deployment planning |

## Update procedure

At the end of each implementation pass, update the affected IDs, status and immediate queue; link the pass report and artifacts; record the exact tested revision and package. Keep remaining device, multiplayer and human acceptance visible. If scope changes, explain the decision here before marking a smaller target done. Add defects with a reproducer and severity to the active pass instead of silently changing the goal.

| Date | Change | Result / evidence |
| --- | --- | --- |
| 2026-09-14 | Created the gameplay capability goal, baseline inventory and P0–P7 acceptance plan. Reconciled older task-board installation/deployment wording. | Documentation completed; no new gameplay implementation, match, build or device test in this planning pass. |
| 2026-09-14 | P0 coverage inventory, synthetic 160/200/400 Release baseline, displaced formation recovery and moving-target retry fixes; stable separate-file Unreal compilation. | [Gameplay baseline pass](GAMEPLAY_BASELINE_PASS.md). Full synthetic arrival checks and local engine/portable/sanitizer/server checks pass; broader order intent and physical acceptance remain open. |
| 2026-09-14 | Completed-order and interrupted-delivery repairs; actual controller-coordinate mixed-selection coverage. | [Order-completion pass](ORDER_COMPLETION_PASS.md): 10 portable and 10 sanitizer suites, 32 server tests, 37 Unreal checks and current 160/200/400 navigation runs pass. No new iOS install, physical input, public deployment or sustained thermal result. |
| 2026-09-14 | P0.2 instrumentation, legal paid-army workload and bounded static goal-attachment cache. | [Match-scale pass](MATCH_SCALE_PASS.md): 12 portable and 12 sanitizer suites, 32 server tests and 37 Unreal checks pass. Original synthetic hashes/arrivals remain unchanged. Both legal workloads pass original march goals, full per-tick/recording invariance and identical snapshot payload distributions. Preparation p99 improves; peak stalls and an isolated production-exit defect remain open. [Verification](../artifacts/match-baseline/verification.json). |
| 2026-09-14 | Reachable production exits, paid-ready worker retry lifecycle and crowded-movement recovery, including a recorded real paid-match jam. | [Production exit and traffic pass](PRODUCTION_EXIT_PASS.md): 15 portable and 15 sanitizer suites, 32 server tests, 37 Unreal checks, all 160/200/400 synthetic arrivals and both 160/320 paid army marches pass with deterministic repeats. Peak steps of about 183/308 ms and physical acceptance remain open. [Verification](../artifacts/production-exit/verification.json). |
| 2026-09-14 | P0.2 exact slow-step traces and redundant navigation endpoint-check removal. | [Navigation latency pass](NAVIGATION_LATENCY_PASS.md): 16 Release and 16 sanitizer suites, 32 server tests, exact 21-case route corpus, all synthetic arrivals and complete paid workload invariance pass. Two/four-player peaks improve from 183/308 to 137/221 ms. Current Unreal gates wait for Android initialization; renderer/device acceptance and further peak reduction remain open. |
| 2026-09-14 | Bounded terrain-only visibility cache and exact finite-domain box-check shortcut. | [Navigation visibility pass](NAVIGATION_VISIBILITY_PASS.md): all local gates and complete workload invariance pass. Two/four-player preparation maxima improve to 71/145 ms; fresh CPU trace retained. P0, device rendering/thermals and P1–P7 remain open. [Verification](../artifacts/terrain-visibility/verification.json). |
| 2026-09-14 | P1.1 queued tactical routes, worker/support completion, save 11/protocol 8 and acknowledged touch targeting. | [Tactical order pass](TACTICAL_ORDERS_PASS.md): local core, sanitizer, server, Unreal and real loopback checks pass, with desktop/compact captures and current deterministic synthetic/paid workloads. Four-player preparation still peaks around 145 ms. Physical input, current package, thermals and P1.2/P1.3 remain open. [Verification](../artifacts/tactical-orders/verification.json). |

## Benchmark references

- [Blizzard's StarCraft II overview](https://starcraft2.blizzard.com/?s=18): races and game modes. Content breadth is separate from this core competitive goal.
- [Blizzard's map-elements guide](https://news.blizzard.com/en-us/article/4546768/game-guide-map-elements): ramps, vision and high-ground decisions.
- [Blizzard's leagues and ladders guide](https://news.blizzard.com/en-us/article/110519/leagues-and-ladders-faq): matchmaking and team-mode capability benchmark. This historical guide is not a specification for Cinderline's rating algorithm.

Acceptance thresholds and milestone designs above are Cinderline project targets, not performance or implementation guarantees attributed to Blizzard.
