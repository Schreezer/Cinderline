# Gameplay reliability and stress baseline

Updated 2026-09-14. Roadmap: [P0](SC2_GAMEPLAY_ROADMAP.md#p0-establish-measurable-match-quality).

Status: this bounded local pass is complete, including two movement fixes, engine integration, regression checks and reviewed stress evidence. Full P0 remains open. Physical input, rendering, CPU/GPU frame attribution, battery and sustained thermal acceptance remain separate.

## Existing coverage and missing cases

The pre-change portable run passed all eight suites. [Before log](../artifacts/gameplay-baseline/portable-before.log).

| P0 behavior | Existing coverage | This pass / remaining work |
| --- | --- | --- |
| Single/mixed selection and group commands | `FCinderArmyControlIntegration` in [army integration tests](../Source/Cinderline/Private/Tests/CinderArmyControlTests.cpp) | Most setup assigns selection directly. An actual pointer/box-created mixed selection through every relevant contextual action and physical touch acceptance remain open. |
| Friendly actor crossing a tap | `FCinderWorldTapIntegration` in the army integration tests covers both crossing directions | Reuse the existing engine tests; no duplicate simulation substitute. |
| Deselect without losing orders, squads or jobs | `FCinderWorldTapIntegration` preserves state hash, recording, existing orders and queue identity | Passed again in the rebuilt engine; physical touch acceptance remains open. |
| Narrow turns and construction access | `narrowTurningGap`, `blockedWorkSiteSides`, `reachableAlternativeWins`, `inaccessibleBuildStatusMatchesCommand`, `foundationRouteInvalidation` in [navigation tests](../Tests/NavigationTests.cpp) | Preserve static clearance and atomic inaccessible-build rejection. |
| Mining congestion and exhausted work slots | `busyMiningCorridor`, `workSlotExhaustionSemantics` in navigation tests | Preserve temporary-slot retries and explicit impossible-route state. |
| Fresh worker planning and rally inheritance | `freshWorkerMiningAssignment`, `deferredWorkerAssignmentPersistence`, `persistentArmyRallyAndOverrides`, `replayAndPersistence` in [production allocation tests](../Tests/ProductionAllocationTests.cpp) | Reuse production and network suite coverage, including deferred planning and explicit overrides. |
| Repeated retreat while moving | No direct repeated mixed-army reversal regression | Added `repeatedMixedArmyRetreat`: 24 mixed units, twelve reversals around terrain, unchanged acknowledged final destinations, static clearance, and save/load continuation. It fails on the previous simulation. |
| Crowded formation destinations | The older 160-worker benchmark reports arrival when a unit is Idle **or** within 70 units of its goal | New stress acceptance requires proximity to the assigned goal. Idle by itself cannot establish arrival. This exposes displaced units that the older report counted as arrivals. |
| Moving-target recovery after a failed approach | No direct aircraft-over-terrain pursuit regression | Added `movingTargetRecoversFromFailedApproach`: keep an aircraft over blocked ground, observe bounded failed attempts, move it into reach and require the original attack order to reach a firing position and deal damage. It fails on the previous simulation. |

## Defects and fixes

### A unit could stop reclaiming its assigned formation slot

The simulation changes an arrived Move order to Idle. Later traffic can push that unit away from its assigned slot, and `updateMovement` then tries to reclaim it. The separation pass still treated every Idle unit as stationary and repeatedly renewed its yield timer. The unit therefore kept yielding instead of returning. Small residual overlaps could keep this happening after the visible traffic had passed.

The movement layer now distinguishes a resting Idle unit from a displaced unit returning to its goal. Only a resting unit receives the stationary-unit yield treatment. Explicit Hold and settled Defend anchors retain their existing protection. The change preserves the assigned goal; it does not teleport the unit or count a displaced Idle state as success.

The pre-fix repeated-retreat fixture leaves a Lancer approximately 98 units from its assigned slot after the final 80-second simulation window. [Failure](../artifacts/gameplay-baseline/retreat-before.log). The focused corrected run reaches every saved destination. [Result](../artifacts/gameplay-baseline/retreat-after.log).

A smaller regression, `displacedIdleDrainsYieldAndReclaimsGoal`, synthesizes traffic displacement after a unit has reached its genuine movement goal. A held friendly neighbor sits at a floating-point contact boundary. The test requires the displaced unit to return within two seconds while preserving both its original goal and the held neighbor's position. This isolates the timer starvation from the larger retreat workload.

### Failed pursuit could remain stuck after the target moved

A ground unit can fail to route to a firing approach beneath an aircraft over blocked terrain. The failure was cached against static geometry. Moving the aircraft did not change that geometry, so the attacker never searched again even when a valid approach became available.

Target-tracking orders now retry using the existing capped retry delay and per-step navigation budget. Fixed unreachable movement destinations retain their existing persistent failure behavior. No new entity, save or snapshot fields are introduced.

The regression requires actual movement and damage after the target moves, and also bounds retry work while it stays inaccessible. [Before](../artifacts/gameplay-baseline/pursuit-before.log), [after](../artifacts/gameplay-baseline/pursuit-after.log).

## Reproducible portable workload

[Tools/GameplayBaseline.cpp](../Tools/GameplayBaseline.cpp) is a standalone CMake executable linked to the same simulation library as the game and authoritative worker. It emits JSON and returns failure when a workload is invalid, destinations are not reached or deterministic repeats disagree.

| Workload | Mobile units | Setup | Measured simulation window |
| --- | ---: | --- | ---: |
| `navigation_160_crossing` | 160 | Two friendly worker groups cross the existing central-gap geometry in opposite directions | 80 seconds |
| `mixed_army_200` | 200 | Two friendly mixed-speed groups cross a central gap; includes ground, air, worker and support roles | 120 seconds |
| `four_seat_400` | 400 | Four isolated seats each run two friendly crossing groups; the workload avoids inter-seat combat | 100 seconds |

These are synthetic `debugSpawn` stress fixtures, not ordinary economic build orders or a claim of supported phone capacity. The current game limits each player to 200 crew; unit types consume different amounts of crew. The larger mixed fixtures can exceed that economic limit. A full supported-match profile must also account for legal production, buildings, combat, fog, snapshots and rendering.

Each run declares warmup and measured ticks; preserves active headquarters so the simulation cannot end early; validates setup/goal geometry and finite, in-bounds positions for all mobile units every tick, plus static clearance for ground units; checks issued commands enter Move on the issue tick; reports step p50/p95/p99/max, command-call time, route searches/expansions/failures/deferrals, surviving and arrived counts; and reruns the workload to compare state hashes. Command-call timing is a synchronous simulation measurement, not touch-to-screen or Internet latency. The per-tick check samples the resulting positions; narrow-turn regressions and the production movement code separately enforce swept movement clearance.

The 35-world-unit arrival tolerance is fixed before comparison. Arrival must refer to the original per-unit destination accepted by the command, with no silent goal replacement. Timings cover simulation stepping, not rendering or the external validation code. First-use route work is included in the measured window.

Reproduce with a Release build and two build jobs:

```sh
cmake -S . -B Saved/P0BaselineBuild -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=OFF -DCINDERLINE_BUILD_SERVER=OFF -DBUILD_TESTING=OFF
cmake --build Saved/P0BaselineBuild --target CinderlineGameplayBaseline --parallel 2
Saved/P0BaselineBuild/CinderlineGameplayBaseline > artifacts/gameplay-baseline/stress-after.json
```

Run from the repository root. Keep the nonzero exit status if the workload fails. Capture the source manifest and build environment alongside each result. Do not run another build or stress test during timed captures.

## Final Release comparison

Both runs use the same finalized harness source with Apple Clang 21, `-O3 -DNDEBUG`, on an ARM64 M1 Max Mac. The old library was preserved before the production edits; the new library contains the two movement fixes. [Environment](../artifacts/gameplay-baseline/environment.json), [before JSON](../artifacts/gameplay-baseline/stress-before.json), [after JSON](../artifacts/gameplay-baseline/stress-after.json).

| Workload | Before arrivals | After arrivals | After step p50 / p95 / p99 | After maximum step | Total command-call cost |
| --- | ---: | ---: | --- | ---: | ---: |
| 160 workers | 154 / 160 | 160 / 160 | 0.107 / 0.393 / 0.461 ms | 8.298 ms | 0.153 ms |
| 200 mixed units | 148 / 200 | 200 / 200 | 0.179 / 0.460 / 0.514 ms | 21.143 ms | 1.213 ms |
| 400 units / four seats | 301 / 400 | 400 / 400 | 0.652 / 0.706 / 0.751 ms | 0.811 ms | 3.243 ms |

All missing arrivals before the fix were alive Idle units displaced beyond the fixed accepted-goal tolerance. After the fix there are no missing/dead units, changed goals, exhausted routes, unfinished moves or per-tick static-position violations. Deterministic repeat hashes match for each workload. The old binary exits 1 and the corrected binary exits 0; the check does not classify failures as a successful performance sample.

These short CPU samples establish correctness and an optimized simulation baseline, not a sustained whole-game speedup. The 21 ms tail in the mixed case deserves further attribution. An earlier unoptimized pair is retained only as diagnostic correctness evidence in `stress-o0-before.json` and `stress-o0-after.json`; it is not the Release performance baseline.

## Engine build reliability

The Mac rebuild exposed pre-existing collisions between file-local helpers when Unreal combined previously separate source files into unity translation units. Examples include HUD/help `Name`, preview state/timers and simulation `Pi` helpers. Adaptive grouping made compilation depend on which files Git considered modified.

The Cinderline module now sets `bUseUnity = false`, so each C++ source keeps its own file-local scope, matching the portable simulation build. This changes compilation organization, not gameplay or rendering settings. The [first build log](../artifacts/gameplay-baseline/unreal-build.log) records the reproduced failure; the final build and engine tests are tracked below.

## Verification ledger

| Check | Status | Evidence / limits |
| --- | --- | --- |
| Pre-change portable suites | Passed, 8/8 | [Log](../artifacts/gameplay-baseline/portable-before.log) |
| Repeated mixed-army retreat | Reproduced failure; focused corrected run passes | Before/after logs above; save/load continuation included |
| Moving-target pursuit | Reproduced failure; focused corrected run passes | Before/after logs above; bounded retry and static clearance included |
| Final comparable 160/200/400 captures | Passed for corrected simulation | Same finalized Release harness, preserved old/current libraries, full arrivals and matching repeat hashes; before run fails as intended |
| Address/undefined-behavior sanitizer suites | Passed, 8/8 | [Sanitizer log](../artifacts/gameplay-baseline/sanitize-after.log), [detailed tests](../artifacts/gameplay-baseline/sanitize-detail.log); includes navigation, Hold/enemy collision, production, persistence and multiplayer invariants |
| Mac Unreal build and engine checks | Passed | [Build](../artifacts/gameplay-baseline/unreal-build-final.log), [37 expected Unreal checks](../artifacts/gameplay-baseline/unreal-tests.log), no errors/warnings/unfinished tests; NullRHI automation, not physical input or rendered performance |
| Corrected portable and server checks | Passed | [Eight portable suites](../artifacts/gameplay-baseline/portable-after.log), [32 server tests](../artifacts/gameplay-baseline/server-after.log), no skipped tests; server tests use the freshly built authoritative worker |
| Current iPhone package / physical match / sustained profile | Pending | Installed `.35` predates these fixes; no device replacement or runtime result claimed |

The pre-change simulation archive and file hashes are recorded in [source-before.json](../artifacts/gameplay-baseline/source-before.json). The [verification record](../artifacts/gameplay-baseline/verification.json) records current hashes, exact commands, build results and remaining scope. P0.1/P0.2 remain open until their outstanding coverage and acceptance requirements are addressed; P0.3/P0.4 require the device session.

## Next demonstrated issue

The independent movement review reproduced a separate stale-goal transition: after an explicitly attacked unit dies, the attacker changes to Idle but retains the target's location as its goal. It can then walk toward the corpse as if reclaiming a formation slot. The [isolated reproduction](../artifacts/gameplay-baseline/order-abandonment-repro.cpp), rerun against the corrected simulation, moved 142.5 units in the following second and exited 1 as expected. [Pending failure log](../artifacts/gameplay-baseline/order-abandonment-pending.log). Gather exits without a usable depot or remaining resource have similar source patterns that need their own reproductions.

Track these deliberate-abandon transitions as the next P0.1 fix. Preserve the distinction between continuing an AttackMove destination, reclaiming a displaced formation slot and completing an explicit target order. This pass does not claim that broader order-intent problem is fixed.

Follow-up: the demonstrated stale-goal issue and related depot-delivery recovery are now covered by the [order-completion pass](ORDER_COMPLETION_PASS.md). This report and its manifests retain the earlier baseline revision.
