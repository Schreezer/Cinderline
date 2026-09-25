# Reachable production exits

Updated: 2026-09-14. Status: bounded pass locally verified; broader P0 acceptance remains open. This is a bounded P0.1/P0.2 pass in [the gameplay roadmap](SC2_GAMEPLAY_ROADMAP.md).

## Problem and intended behavior

A paid Cinderthrow could finish training at a locally clear exit enclosed by its Crucible and nearby Siphons. Another side of the same Crucible had a route to the rally point. Choosing the first clear point stranded the unit before the player had issued an order.

The [original paid-command reproducer](../artifacts/match-baseline/production-exit-repro.log) records the blocked exit and reachable alternative. The original failed full-workload saves and their geometric diagnosis remain in `artifacts/match-baseline/`.

Production now considers locally clear, unoccupied exits against the unit's actual formation destination. When the first exit has a direct route, it keeps that exit. Otherwise one shared navigation search chooses a reachable origin from the candidate exits. A new Drudge's automatic or explicit ore assignment checks both its outward route and a return route to a completed depot. Ground rally overrides retain their destination. Air units retain their ground-obstacle bypass.

If every local exit is occupied or blocked, the paid job remains ready at the head of its queue with crew reserved. If local exits exist but the distant rally is unreachable, the unit is released at the first local exit and retains its Move order with route-failure feedback. This distinguishes a blocked producer from an unreachable rally. Exhausting the per-step route allowance defers completion without charging again or dropping the job.

## Implementation and limits

- `SimulationProduction.cpp` owns production completion, exit choice and the existing research/construction transitions. `Simulation.cpp` no longer duplicates that function.
- `Navigation::routeFromAny` canonicalizes sources and preserves the existing single-source route. Multiple sources share an adaptive-graph pass followed, when needed, by a full-graph pass under one cumulative expansion allowance. Its result includes the chosen origin. It returns a deterministic reachable route without claiming global shortest paths. Goal attachment caching, geometry invalidation and copy isolation remain in effect.
- The existing 16-query per-step allowance and 40,000-node per-query expansion cap remain. Source attachment preparation still runs per candidate; these bounds do not establish a wall-clock or phone frame-time guarantee.
- The fresh-worker planner receives all usable exits. Its existing resource ranking and persistent candidate cursor remain; this pass does not claim globally optimal mining allocation.
- A compute-limited worker query preserves the current ore candidate and paid job. The producer's existing saved/hashed `repath` and `pathGeometry` fields provide a one-second retry delay, bypassed when geometry changes. Rally changes, cancellation of the front job and new jobs in an empty queue clear stale retry state. This avoids repeating a capped query every simulation tick; it is not a resumable search or a guarantee that every complex route will complete.
- Save version 10 and protocol 7 remain unchanged. No persistent fields or network payloads are added.

## Verification ledger

| Check | Status | Evidence |
| --- | --- | --- |
| Reproduce disconnected Cinderthrow exit before the change | Done | Original paid Train reproducer and preserved before-core archive manifest in `artifacts/production-exit/before-inputs-sha256.json` |
| Same production tests against preserved before core | 3 passed / 7 expected failures | `artifacts/production-exit/before-regressions.log`; compiled with its matching saved headers. Alternate exits, retry lifecycle and production budget tests fail before the fix; all ten pass on the production-only revision. |
| Multi-source route regressions | 8/8 Release cases passed | `artifacts/production-exit/release-current-detail.log`; includes a large U-shaped detour that exhausted the first implementation and now reaches its destination, plus a genuine capped search that retains its indeterminate result |
| Production exit regressions | 10/10 Release cases passed | `artifacts/production-exit/release-current-detail.log`; exact Cinderthrow, explicit/default/ore worker exits, air bypass, paid-ready persistence, unreachable-rally fallback, 17 simultaneous producers and backoff/save/rally/front-replacement coverage |
| Portable regression suites | 15/15 passed on final traffic revision | `artifacts/production-exit/release-current.log`; exact inputs in `current-source-sha256.json`. The earlier 14-suite production-only receipt is retained separately. |
| Traffic regressions | 5/5 passed, including recorded paid-match continuation | `release-current.log`. The same five tests against the preserved pre-current-stall-gate archive produce 4 passes and the expected recorded-crowd failure in `traffic-before-current-stall-gate.log`. |
| Sanitizers, server and Unreal integration | Passed | 15/15 sanitizer suites in `sanitize-current.log`, 32/32 server tests in `server-current.log`, Mac build in `unreal-current-build.log`, and 37/37 engine checks with zero report warnings in `unreal-current-report.json` |
| Intermediate paid four-player workload | Preserved failure before traffic repair | `artifacts/production-exit/four-player-intermediate.json`: all four paid armies prepared, 318/320 army arrivals, identical repeated trajectories and hashes. Team 0 Anvil 186 and Cinderthrow 262 retain reachable goals but fail to clear traffic. Combat stage did not run. Preserve this failure; do not relax the goals or arrival criteria. This historical failure is retained as the regression fixture. |
| Final paid four-player workload | Passed | `four-player-current.json`: all four paid armies prepared, all 320 original army destinations reached, combat ran, and state/trajectory/recording repeats matched. |
| Final paid two-player workload | Passed | `two-player-current.json`: both paid armies prepared, all 160 original army destinations reached, combat ran, and state/trajectory/recording repeats matched. |
| Independent source review | Accepted for this bounded pass | Navigation, production integration, backoff lifecycle, worker intent, current-stall avoidance and the recorded traffic fixture reviewed. Device and broader P0 acceptance remain separate. |
| iOS package, physical match, sustained thermal/GPU profile | Pending separate device session | This pass has no package/install or physical acceptance evidence |

## Crowded movement follow-up

The first paid four-player capture with reachable production exits prepared all 400 mobile units but left Anvil 186 and Cinderthrow 262 short of their unchanged march goals. The [single-pass diagnostic](../artifacts/production-exit/crowd-stall-intermediate.json) reproduces the same state/trajectory/recording hashes and retains both march-start and march-end saves. Reloaded navigation confirms both destinations are reachable.

Before the traffic fix, continuing the captured end state for another 120 simulated seconds leaves the Anvil about 670 units and the Cinderthrow about 824 units from their destinations. Nearby displaced idle workers block them while their stall time and failure count remain zero. The movement loop resets progress tracking whenever it sees a nearest blocker, even when avoidance makes no forward progress.

The repair retains measured stall history, gives persistently stalled explicit orders priority over displaced Idle reclaimers, and repairs a current path segment when avoidance makes it statically invalid. Strong lateral recovery requires current lack of progress, so an old failure does not keep steering a fresh route sideways. Mining/construction destinations survive displacement-only route repair.

All five traffic cases pass, including the unchanged paid-match fixture; the original saved Anvil and Cinderthrow both arrive within the unchanged 35-unit tolerance by 800 ticks and remain there through 2400 ticks in `continuation-current-stall.log`. Fresh 160/200/400 synthetic workloads pass all arrivals and deterministic repeats in `synthetic-current.json`. The final fresh four-player run also reaches all 320 original army destinations and completes combat with matching state, trajectory and recording repeats.

Its preparation p99 is 6.64 ms and worst whole-simulation step is 307.75 ms. These are desktop simulation measurements from a changed-behavior revision, not a same-trajectory speedup or phone-performance result. The fresh two-player run also reaches all 160 original army destinations and completes combat with matching full repeats. Its preparation p99 is 1.88 ms and maximum step is 182.99 ms.

The [verification record](../artifacts/production-exit/verification.json) confirms all applicable local gates and exact artifact hashes. The verified core and matching headers are preserved under `Saved/P0ProductionTrafficVerified/`, indexed by `verified-core-sha256.json`, for future comparisons. The recorded traffic fixture remains separate from the earlier static spawn enclosure, and both preserve their original accepted destinations and clearance checks.

## Source continuity and remaining work

The Mac build-entry recheck passed. The frozen manifest detected concurrent Linux packaging edits to `Cinderline.uproject` and `scripts/unreal.sh`; their exact before/current hashes and reviewed diff are retained in `reviewed-input-drift.json` and `concurrent-linux-packaging.diff`. They add a Linux target/action without changing the shared simulation or existing Mac build action. This pass does not validate that separate Linux packaging work. P0's long individual simulation steps, renderer/device costs and physical full-match acceptance remain separate open items. P1 queued tactical orders remain planned in [TACTICAL_ORDERS_PLAN.md](TACTICAL_ORDERS_PLAN.md).
