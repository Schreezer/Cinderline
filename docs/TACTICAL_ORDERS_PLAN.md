# P1.1 tactical order sequences

Updated 2026-09-14. Status: simulation, persistence, network and presentation are implemented and locally verified; matching-package physical input acceptance remains open. Results and retained evidence: [tactical order pass](TACTICAL_ORDERS_PASS.md). Parent: [SC2 gameplay roadmap](SC2_GAMEPLAY_ROADMAP.md#p1-complete-the-tactical-command-model).

## Player behavior

Let a player plan a route with several Move or Attack-move destinations. Ordinary commands continue to replace existing orders. Desktop Shift appends a destination; touch uses a clearly labelled Queue next control in the existing Orders sheet. Deselecting preserves every unit's current and queued orders. Permanent Build, Train, Research and Army controls remain available.

The selected unit's route shows the current destination and numbered future points. Use distinct Move and Attack-move markers. For mixed selections, show the primary unit's route and the number of units with other routes, avoiding a screen full of overlapping lines. Only owned units expose planned routes.

## Intended command contract

- Keep tactical orders separate from building production queues. Add a dedicated queue-mode field to commands and a bounded per-unit list of future Move/Attack-move steps. Start with a limit of 16 future steps per unit.
- Replace clears the future list and installs the new current command. Append stores each unit's assigned formation destination at acceptance time; later movement must not recompute it from a changed selection or formation.
- Append follows the current live order. It starts immediately for an idle unit. Genuine completion activates the next step before automatic idle work is chosen. An indefinite Hold, Defend or ongoing Gather can keep future steps waiting; UI must identify that current order.
- Attack-move temporarily engaging a target does not complete its waypoint. Losing an incidental target resumes the current Attack-move destination. Only actual waypoint arrival advances that step.
- A blocked route preserves its current destination and future list, reports the problem, and retries when geometry changes under the existing recovery rules. It must not silently skip a waypoint.
- A normal new Move, Attack, Gather, Build, Stop, Hold or Defend clears the future list. Stop cancels current and future orders. A separate Clear queued orders command removes only future steps.
- A group append is atomic: if any eligible selected unit would exceed the limit, reject the append and explain the limit. Preserve the existing ownership and eligibility rules; never partially queue an apparently successful group action.
- Normalize an append before changing any entity: resolve the executable selection, assign every formation point/support relation, and validate every recipient's queue and aggregate payload allowance. Commit all normalized steps together only after validation passes. Do not abandon construction or reset navigation during this preflight.
- Every Mender Attack-move keeps a waypoint and may carry a selected friendly combat leader's ID as an optional support relation. Apply the same normalization to Replace, an Append that starts immediately on Idle, and a later queued successor. This replaces today's implicit conversion of mixed Attack-move into indefinite follow. Choose the relation deterministically at acceptance. Following or healing does not finish the waypoint; losing the leader clears the relation from current/future steps and resumes travel to the saved waypoint. With no eligible leader, travel to the waypoint with normal automatic healing. Validate, hash, save and remap the optional ID; do not silently turn a waypoint into indefinite direct Attack/follow behavior. Only an explicit direct Attack/follow command retains separate follow semantics.
- Successful construction followed by an explicit queued destination must not be overwritten by automatic return to mining. Newly trained workers and shared/producer army rallies retain their current automatic behavior when no explicit sequence exists.
- A queued step after Gather waits for the assigned deposit to finish, including delivery of remaining cargo, then takes precedence over automatically choosing another deposit. Ordinary mining without a future list retains its existing automatic retargeting. A temporary missing depot preserves the delivery and future list while waiting for an operational replacement.

Touch Queue next is one-shot: consume it after a successful destination, retaining it after a rejected destination so the player can correct the point. Selection change, cancellation, pause/background and a second touch clear targeting intent without canceling authoritative orders. Desktop Shift is sampled when issuing the destination; Option remains trackpad camera panning.

Queue next arms an explicit unit Move or Attack-move destination mode. While armed, a tap on an enemy or resource chooses its ground location for that mode; it must not become a replacing Attack/Gather command. The preview and button identify the armed action. Append is valid only for unit Move/Attack-move. Production rally, Defend, Attack/Gather and global automatic commands use Replace; reject a malformed non-Replace combination. Explicit destination mode, contextual empty-ground movement and right-click movement must use one queue-mode resolver. Do not infer append for AutoRally merely because it shares `IssueDestination` today.

## Construction precedence

| Trigger | Required queue behavior |
| --- | --- |
| Append while the worker is constructing | Store the future step; leave construction ownership, travel and mining-resume state intact |
| Explicit Build or ResumeConstruction replacement | Validate first, then clear the future list and perform existing construction handoff |
| Automatic Build allocation | Exclude workers with explicit future steps; report no available automatic builder if all otherwise eligible workers have plans. Manual assignment may replace a plan explicitly. |
| Foundation completes | Clean up worker/foundation ownership, then activate exactly one explicit successor; resume mining only when no successor exists |
| Foundation canceled, destroyed or its builder link lost while the worker survives | Clean up the interrupted work, then activate the explicit successor if present; otherwise retain the established automatic mining recovery |
| Worker receives a replacing command | Clear the future list before cleanup and install only the replacement; cleanup must not activate an old successor |
| Worker dies | Clear its future list and release the foundation link; no successor runs |

Manual cancellation of a foundation is different from a replacement command to its surviving worker. Cover both paths explicitly; today's shared `releaseConstruction`/`abandonConstruction` cleanup cannot decide that distinction on its own.

## Implementation boundaries

| Boundary | Required change and regression |
| --- | --- |
| Shared simulation | Separate tactical data from `Entity::queue`, `Simulation::MaxQueue` and `Command::queueIndex`, which already belong to production. Centralize installing/finishing steps, preserving worker cargo and construction cleanup. Cover per-unit formation points, atomic rejection, replacement, completion, blocked recovery and death. |
| Determinism and recording | Record queue mode, hash future order types/destinations/support IDs/counts, and compare it in command invariance helpers. Playback must reproduce every per-tick state. |
| Save migration | Plan save version 11 with an explicit final `ORDER_QUEUES 1` section and version-gated recording fields. Existing versions 1–10 load with empty future lists and Replace mode. Require exactly one record per saved entity; only live player-owned mobile units may have a nonempty list. An Idle unit cannot retain a future tail: installing/finishing an order activates its successor atomically. Validate supported types, finite in-world points, optional friendly support IDs and aggregate limits; reject corrupt files atomically. |
| Network | Plan protocol 8 with explicit command mode and owned-entity future steps. Reject incompatible versions. Validate semantic invariants as well as lengths/types/coordinates before applying a snapshot. The encoder strips opponents' future steps and private support IDs; the decoder must also reject a hostile snapshot containing them. Remap legitimate owned support references through the existing opaque-ID policy. |
| Server and LAN | Update the C++ constant, Node protocol constant, LAN advertisement and admission tests together. Keep global automatic commands valid with empty selections. Confirm worker translation preserves the new mode and opaque identity rules. |
| Controller | Extend `IssueDestination` and the existing pointer reducer; preserve ordinary friendly taps, contextual commands, deselection and Option-drag. Mouse and touch must reach the same authoritative command path. |
| HUD | Add Queue next and Clear queued orders to the existing Orders sheet, with active/blocked/count feedback and route markers. Keep current compact controls and global catalogs. |

The implementation now uses save version 11 and protocol 8. Local validation passes; previously packaged clients and deployed workers have not been upgraded by this pass and must not be mixed with the new protocol.

Bound the aggregate representation as well as each unit: at most 4,096 future steps in a player view and 4,096 times the active player count in an authoritative save. This accommodates the current 200-mobile-unit-per-player ceiling at 16 steps each. Check the projected encoded snapshot against the existing 1 MiB frame limit before committing an append, including other entity/production fields. Reject an oversized plan atomically with feedback; never silently remove waypoints or emit an empty snapshot. Spectator formats and higher supported entity limits require a separate budget review.

Completion and interruption must use distinct transitions. Today's `finishOrder` is also called by `abandonConstruction`, so adding unconditional queue advancement there would incorrectly activate a step during replacement or cancellation. Separate cleanup from successful completion; clear the tail before a replacement, finish foundation/worker ownership cleanup before activating a successor, and choose exactly one of the explicit successor or automatic mining resumption. Ordinary arrival currently has its own inline Idle transition and must join the completion path while retaining the accepted formation destination.

## Delivery sequence

1. Implement the simulation contract with focused portable tests, including queued worker work completion and interruption. Freeze those semantics before parallelizing serialization and presentation.
2. Add save/recording migration, hash coverage, network codecs/privacy and synchronized protocol admission. Test replay and snapshot replacement.
3. Add desktop/touch intent, HUD controls and route feedback. Extend engine tests through controller-created selection and destination commands.
4. Run applicable portable, sanitizer, server and engine checks. Re-run routing workloads if movement/completion changes affect them. Inspect compact/desktop rendering and retain physical input acceptance as a separate gate.

## Acceptance ledger

- [x] Queued Move/Attack-move arrival, mixed selections and formation destinations in portable regressions.
- [x] Stop, clear-future-only, ordinary replacement, deselect and selection changes in simulation/controller regressions.
- [x] Attack-move engagement and target loss; blocked routes and geometry recovery.
- [x] Construction completion, cancellation and reassignment; depleted-deposit final delivery and missing-depot recovery without explicit queued work being overwritten or cargo lost.
- [x] Queue-bound atomic rejection and invalid/unauthorized command rejection.
- [x] Mender waypoint/support behavior, leader death and relation remapping; construction trigger matrix without premature queue activation.
- [x] Injected contextual mouse/Shift and explicit touch pointer paths, armed taps over actors, and rejection of append modes on non-tactical commands.
- [x] Current and older saves, replay determinism, owned snapshots and opponent privacy.
- [x] Duplicate/missing entity queue records, dead/building/Idle tails, hostile opponent queues and aggregate frame-size rejection.
- [x] Local server admission and actual Unreal command transport with protocol 8.
- [x] Scripted desktop/compact HUD captures inspected for readable controls, armed/pending status and visible numbered waypoints.
- [ ] Actual mouse/touch gestures and a complete match on the matching package, including rejection/reconnect behavior. Injected controller events and Mac rendering do not close this gate.

Patrol/escort and formation facing remain P1.2/P1.3. This plan does not count them as delivered by adding waypoint queues.
