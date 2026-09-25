# Patrol and escort

Updated 2026-09-15. Status: **implemented; local feature checks passed, combined workload and physical acceptance incomplete**. This is P1.2 in the [gameplay roadmap](SC2_GAMEPLAY_ROADMAP.md), governed by the [order contract](PATROL_ESCORT_PLAN.md). Formation controls remain P1.3, and the player has requested that work stop after P1. No P2 work is authorized by this report.

## Behavior

Patrol repeats between the unit's captured starting point and its assigned destination. A group retains distinct destinations. An incidental engagement has a fixed pursuit anchor and a bounded leash. When pursuit ends, the unit completes the interrupted endpoint with new attacks suppressed before starting another leg. This prevents repeated bait from pulling a patrol progressively off its route.

Escort follows an owned mobile unit in stable relative slots. Selecting the leader among the recipients leaves its orders unchanged. Separate Escort commands reserve existing followers' slots. Local edge projection keeps goals inside the map, and a moving follow goal can reopen an exhausted route. Target loss starts an explicit queued successor or defends the last follow position. It never silently switches leaders or resumes mining.

The Orders sheet provides Patrol and Escort targeting, cancellation and acknowledgement feedback. P and E are desktop shortcuts. Ordinary friendly taps still select. Current routes and owner-only patrol endpoints, escort target and guard feedback remain available alongside global production controls. Actual rendered acceptance is recorded separately below.

Both sustained orders replace current work and clear future steps only after complete validation. Future Move/Attack-move steps may wait behind them; Clear queued removes the tail, while Stop clears current and future work. Menders keep healing. Explicit worker orders preserve cargo and correctly release construction links.

## Movement corrections

The sustained workloads exposed congestion that smaller order tests did not reproduce. Settled escorts now give way to passing followers with deterministic one-sided priority, then reclaim their immutable goals. A leader's collision priority is separate from temporary traffic-yield timers. Non-work leaders do not continually pause their own followers; working Drudges retain the space-clearing behavior needed for mining and construction.

An escort crossing around its own ground leader uses a short local tangent while the direct route intersects that leader's clearance. The detour retains the accepted goal and releases when the direct route is clear. Swept movement and later collision corrections both protect the leader's footprint. Returning Drudges also exit contact with the resource they just serviced before planning their unchanged depot delivery; that exit ignores only the serviced resource and still checks other collisions.

The [diagnostic notes](../artifacts/patrol-escort/NOTES.md) retain failed iterations and distinguish fixture corrections from gameplay fixes. The exact synthetic dense-row failure is preserved as [a version-12 regression fixture](../Tests/Fixtures/escort-row-jam-v12.cinder).

## Compatibility

This implementation uses save **12** and protocol **9**. Versions 1–11 migrate with empty sustained state. Current saves, recordings, state hashes and owned snapshots carry the new persistent state; opaque identity mapping and private fog remain enforced. Malformed states, cycles, foreign targets and oversized plans reject before authoritative mutation. Client, server and worker require a coordinated compatible release.

The previously installed iPhone package remains save 10/protocol 7. This pass does not record packaging, installation, public deployment or physical play.

## Local evidence and unresolved acceptance

- The dedicated portable suite covers patrol laps and bounded pursuit, blocked-route recovery, stationary patrols, escort assignment and cycles, leader loss, mining and construction, current/legacy persistence, and the captured traffic failures.
- The [sustained workload](../artifacts/patrol-escort/sustained-baseline.json) passes Patrol and Escort at 16, 160 and 200 mobile units with unchanged accepted points and matching state, trajectory and recording repeats. Patrol requires four completed legs per unit. Escort requires every follower to move during the leader's three-waypoint route and converge afterward for 20 stable ticks. This is synthetic simulation/snapshot CPU evidence, not continuous tight formation, rendered frame time or phone performance evidence.
- Local checks pass: 20 Release and 20 optimized ASan/UBSan suites, 35 server tests with the current worker, the Mac Unreal build, 39 Unreal tests and two actual loopback transport tests. The queue workloads preserve all 16 steps at 16/160/200 units; ordinary navigation reaches all 160/200/400 destinations with matching repeats. All eight desktop/compact renders were inspected; controls and pending labels are legible. The open Orders sheet can cover world markers.
- The final paid four-player workload exposed a benchmark handoff defect: workers could finish Gather and attack during the isolated army march. A worker-only holding Move scheduler now preserves combat suppression while leaving the measured army goals untouched. The zero-combat and complete-arrival gates remain unchanged. The corrected four-player run then caught valid combat by an early arrival against an opponent still returning from a long route. An experimental 1400-unit staging gate failed to become ready under the continuously moving holding pattern and was removed from the live benchmark; its source, binary and failed attempt are preserved. The paid-four-player noncombat-isolation gate remains unresolved, and no combined success receipt has been issued. These failures do not demonstrate an attack during a current Move order.
- Matching-package mouse/iPhone touch, a complete physical match, LAN/Internet play and sustained CPU/GPU/thermal acceptance remain open.

P0 also remains partial. Its preparation-time spikes and device acceptance are not closed by passing sustained-order tests. After P1.3 and final P1 feedback work, stop and report those remaining gates instead of continuing to P2.
