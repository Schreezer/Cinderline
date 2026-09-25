# Formation controls and P1 completion ledger

Updated 2026-09-15. Status: **implementation and local feature checks complete; acceptance partial; work stopped**. This is the last implementation slice of P1 in [SC2_GAMEPLAY_ROADMAP.md](SC2_GAMEPLAY_ROADMAP.md). The player requested that work stop after P1. P2 and later milestones remain deferred.

## Player behavior

The Orders sheet offers Tight, Standard and Wide spacing. The preset is local to the current match. Move, Attack-move and Defend use it; other commands retain their established behavior. `FACE NEXT` lets the player press the desired formation center, drag toward its facing direction and release one order. Short drags retain targeting for a retry. Selection changes, cancellation, a second touch and lifecycle transitions cancel unsent targeting. A missing-position mouse release also clears the captured gesture before another camera pan can begin.

The drag overlay is a nominal formation guide. Actual accepted destinations can adapt to footprints, terrain, bounds and held units. The HUD then shows authoritative destinations and arrival arrows. An existing squad's `TACTICS` action opens the same Orders sheet. Global Build, Train, Research and Army access remain available.

Queued Move/Attack-move steps retain independent arrival facings. A replacing Move provides withdrawal: it clears the previous tail and suppresses attacks while travelling. Defenders can make room for friendly traffic, then reclaim their unchanged anchors. They retain target-facing during combat cooldown and restore their chosen arrival-facing at the anchor after losing the legal target. Explicit Hold remains immovable. Different movement speeds remain independent; this does not implement a rigid marching block or a shared speed limit.

## P1 feedback coverage

P1.4 is implemented across the queue, sustained-order and formation slices. The selection strip and Orders sheet show current order and route-blocked state; world indicators show the current destination and numbered future route. The information drawer exposes unit details on demand. Targeting is cancellable, ordinary friendly taps select without replacing an existing army order, and explicit destination modes deliberately command through a friendly hit. The global Build, Train, Research and Army rail remains available.

Evidence is in `CinderHUD` (`DrawSelectionIdentity`, `DrawWorldIndicators`, `DrawInfoDrawer`), controller destination/cancellation handling, and the WorldTapSelection, ArmyControl, TacticalOrderPointerPaths, PatrolEscortPointerPaths, FormationPointerPaths, ProductionControls and UnitInfo Unreal suites. Blocked feedback identifies an unavailable route; the simulation does not expose a separate terrain/obstacle/edge diagnosis.

## Authority and compatibility

The shared nominal layout helper is used by authority and the HUD guide. Authority normalizes the complete recipient set before changing any order. It reserves projected Hold footprints, rejects impossible complete placements atomically and keeps accepted points during recovery. Same-layer air movement and later collision corrections protect held aircraft as well as ground Hold behavior.

Save **13** adds canonical current/future arrival intent and recorded command modifiers. Versions 1–12 migrate without inventing facing intent or changing the previously rendered facing. Protocol **10** carries owned formation intent, hides it from opponents and validates flags, angles, enums and payload bounds. Append admission projects the complete order and checks all active seats before authority mutation. Client, server and worker need a matching release.

## Verification ledger

Current checks on save 13/protocol 10:

- All **21 Release** suites and **21 optimized ASan/UBSan** suites pass. The formation suite contains 16 cases, including immutable saved-jam continuation, held-air swept clearance, a held ground corridor, constrained turns, withdrawal, cooldown-facing, migration and malformed-state rejection.
- All **35 server** tests pass against the current native match worker.
- The Mac Unreal build and **40 Unreal** tests pass. One test contains only the two explicitly allowed host `idevice_id` architecture warnings. Both actual **two/four-player loopback transport** tests pass without warnings.
- Formation workloads at **16/160/200 mixed units** complete Tight Move, rotated Standard Move and Wide Defend. Every original destination remains unchanged, all units arrive for 20 stable ticks, and state/trajectory/recording repeats match. The 200-unit synthetic step p99 is 0.329 ms; its largest measured command is 5.139 ms. These are desktop simulation measurements, not phone frame times or a general latency bound.
- Current queue workloads at **16/160/200**, all six Patrol/Escort workloads at **16/160/200**, and navigation arrivals at **160/200/400** pass their complete repeat and intent-preservation gates.
- All **12 desktop/compact formation captures** were generated and individually reviewed. Labels and essential controls fit; drag and accepted states show direction/slot/route indicators. The compact open Orders sheet covers the starting units in this fixture and closes during the drag. Protected player state remained unchanged. [Verification receipt](../artifacts/formation-orders/verification.json) records 159 source inputs, tested binary hashes and every evidence file; [validator](../artifacts/formation-orders/verify_formation_pass.py) checks them.
- Native mouse/iPhone touch, a matching device package, full physical matches, public deployment and sustained device CPU/GPU/thermal acceptance remain open.

Review and regressions found and repaired air/ground Hold penetration, dynamically blocked Hold recovery, stale pointer capture after a missing-position release, and a 16-unit Defend convergence jam. Explicit Hold keeps its original physical guarantees. Defend preserves order/goal/facing intent while allowing transient friendly yielding, then reclaims its anchor. The old production-traffic test was migrated to that contract with stable reclamation checks; it was not removed. Failed attempts and the exact saved jam remain in [verification notes](../artifacts/formation-orders/NOTES.md).

The preceding [Patrol/Escort report](PATROL_ESCORT_PASS.md) retains its source archive and passing feature checks. Its paid-four-player noncombat-isolation gate remains unresolved: early arrivals can legitimately attack opponents still returning from long routes. An experimental staging gate was not retained after it failed to become ready. This is not a passing workload result and does not close P0's remaining navigation/performance acceptance.

Stopped after this bounded P1 implementation and local verification. P0 remains partial and P1 physical/combined-workload acceptance stays open. P2–P7 are deferred until another user instruction. No new device package, installation, public deployment, commit or push was performed in this pass. Test processes were closed.
