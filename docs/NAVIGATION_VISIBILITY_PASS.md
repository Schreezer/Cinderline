# Terrain visibility reuse pass

Status: locally verified bounded pass. Complete workloads, all 17 Release suites, all 17 ASan/UBSan suites, 32 server tests, the current Mac Unreal build and all 37 local Unreal checks pass. Continues P0.2 after the [endpoint-check pass](NAVIGATION_LATENCY_PASS.md). P0 remains open. [Verification record](../artifacts/terrain-visibility/verification.json).

## Complete workload results

The unchanged paid two- and four-player workloads finish preparation, original-destination army movement and combat, then repeat from a fresh simulation. Every non-timing JSON field matches the preserved endpoint-only baseline: state, trajectory, command recording, route counts, formation destinations, snapshot sizes, visibility and outcomes. The two-player run reaches all 160 army destinations; the four-player run reaches all 320. Synthetic 160/200/400-unit workloads also preserve every non-timing field and reach every original destination.

| Workload / phase | Before p99 / max (ms) | After p99 / max (ms) |
| --- | ---: | ---: |
| Two players: preparation | 1.596 / 136.574 | 1.273 / 71.348 |
| Two players: march | 0.746 / 3.893 | 0.546 / 2.867 |
| Two players: combat | 2.184 / 5.067 | 1.593 / 5.978 |
| Four players: preparation | 5.762 / 220.532 | 3.946 / 145.371 |
| Four players: march | 2.239 / 13.342 | 1.689 / 8.040 |
| Four players: combat | 4.061 / 8.052 | 3.061 / 6.054 |

These are desktop simulation measurements, not FPS or iPhone thermal results. Baseline measurements come from the preceding verified pass; OS scheduling varies between captures. The two-player combat maximum increased despite its lower p99. The remaining 145 ms preparation spike is still a responsiveness problem. One interrupted four-player attempt produced no result and is retained separately; it is excluded from acceptance.

## Measured cause and approach

The after trace still attributes most slow-step samples to box intersection/distance checks when attaching cold mining goals. The full goal-attachment cache must reset when ore deposits or buildings change. Terrain boxes usually stay fixed during those changes.

The implementation preserves exact ordered box-loop results for goal-to-adaptive-target pairs across circle-only geometry updates. Keys contain the exact goal, clearance and target coordinate bits; graph node IDs are never retained. Cached-clear results still run every current circle check. The cache preserves the box predicate result; the cold-query shortcut below changes its implementation while retaining the legacy decision rules. Exact oracle and route comparisons gate acceptance. World or normalized-box changes clear the cache; copies begin with an isolated cache.

The cache uses a bounded registry of 16,384 exact target coordinates and at most 1,024 pages. Two bits per target distinguish unknown/blocked/clear; fixed 4 KiB pages cap result payload at 4 MiB. Registry and page metadata are also bounded by those counts. On registry saturation, unregistered targets use the original predicate; a pending reset clears every page and the registry before the next page acquisition, never while a page is borrowed. Diagnostics are not saved or hashed.

A naive swept-AABB index was deferred because the legacy orientation/tolerance checks can behave unexpectedly around floating-point boundaries. Cache reuse itself does not alter collision decisions. In the first cache-only prototype, tick 8827 recorded 758,472 cache hits / 22,308 misses and fell from about 176 to 58 ms. Tick 6416 remained cold (840,192 misses, no hits) at about 200 ms. The final finite-domain shortcut below reduces that cold cost as well. All retained prototype prefixes match state, trajectory and recording hashes.

## Acceptance

- [x] Measure cache hits/misses at the previously traced ticks and demonstrate worthwhile complete-workload improvement.
- [x] Preserve exact predicate, route, command, state and trajectory results for the covered corpus; retain before/after receipts.
- [x] Cover circle revalidation, terrain invalidation, copy isolation, clearance keys, page eviction and registry saturation.
- [x] Complete portable, sanitizer and server checks on final source.
- [x] Run current Unreal build/automation when Android initialization permits.

Engine builds were paused during the user's Android setup. After its earlier Unreal command process exited, the final Mac build succeeded and fresh automation passed all 37 expected tests with no errors, warnings or unfinished tests. Automation used NullRHI; it does not establish rendering or physical input performance. Device, GPU, thermal, network transport and player acceptance remain open. No platform package or installation is part of this pass.

## Cold box checks

The first boolean edge shortcut barely improved the cold peak (about 200 to 196 ms), confirming that clear segment/box pairs dominate. The accepted implementation keeps the original four intersection checks, then bounds every computed projection foot using the actual reconstructed endpoint `fl(a + fl(b-a))`. Both subtraction directions must have a strictly positive squared one-axis gap at least as large as the existing clearance square before distance work is skipped. This accounts for the legacy collinearity tolerance before rejection and needs no epsilon margin. Near pairs still run the unchanged point/segment predicates; duplicated corner terms are evaluated once.

The shortcut is limited to coordinates within `1e12` and finite clearance squared, which keeps every legacy intermediate finite. Extreme inputs and builds using fast-math retain the original full minimum, including its NaN behavior. Existing Mac and iOS Navigation response files specify `-ffp-contract=off` and no fast-math. No routing or gameplay semantics are intentionally changed.

Independent readers reviewed the finite-domain arithmetic, projection envelopes, directed subtraction, borrowed-page lifetime and cache identity. The final oracle suite contains seven cases, including 76 reconstruction/threshold comparisons whose endpoints must pass public clearance checks. Its output is byte-identical against the immutable endpoint-only library and the optimized library; the 21-route fingerprint remains `0x0a097241ddad648f`. Six cache regression cases pass. Two fixture mistakes were corrected before acceptance and their failure logs retained; production source stayed fixed throughout the complete workloads.

## Current CPU attribution

The fresh Time Profiler capture joins samples to the exact simulation-update signpost, PID, TID and interval. Tick 6416 takes 145.45 ms with 146 joined samples; 51 leaf samples are in current circle checks, 44 in segment intersections and 34 in the terrain loop. Tick 8827 takes 55.92 ms with 56 samples; 40 leaf samples are in circle checks. The cache counters remain 0 hits / 840,192 misses at the cold tick and 758,472 hits / 22,308 misses at the later tick, with no saturation or resets in either interval.

These sampled stacks identify the next work, not exact component time or GPU utilization. The 10,000-tick diagnostic capture explicitly reports incomplete preparation and no repeat; only the separate complete workloads above establish match acceptance. The raw trace is under `Saved/Latency/terrain-visibility-after.trace`; interval and counter receipts are in `artifacts/terrain-visibility/`.
