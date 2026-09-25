# Legal match workloads and simulation cost attribution

Updated 2026-09-14. Roadmap: [P0.2](SC2_GAMEPLAY_ROADMAP.md#p0-establish-measurable-match-quality).

Status: implementation and same-source two-/four-player measurements locally verified. The new measurement tools exposed an expensive worker-routing calculation, and a bounded cache now avoids repeating it on unchanged geometry. P0 remains partial: long individual stalls and a reproduced production-exit defect are open. This pass does not establish phone capacity, thermal acceptance or SC2 parity.

## What is being measured

The earlier [160/200/400-unit navigation fixtures](GAMEPLAY_BASELINE_PASS.md) isolate movement, but their larger mixed armies bypass the normal economy and crew limit. The new `CinderlineMatchBaseline` starts a normal Standard match with two or four players. A deterministic script mines ore, scouts an authored expansion, constructs facilities, researches tiers and pays for each unit through public commands.

The reference roster per player is 20 Drudges, 50 Embers, 10 Needles, 10 Skims, four Anvils, two Cinderthrows, two Mends and two Veils. That is 100 mobile units and 184 crew. Eleven paid Siphons and the starting Anchor provide 184 capacity. The minimum mobile supply cost is one; the 200-crew rule gives a theoretical ceiling of 200 mobile units per player. This reference mixed army does not test that ceiling or establish a supported device count.

Preparation has a 3,600-second simulation limit. A separate march and combat window follow successful production. This is a scripted workload, not an AI match, human balance evidence or a claim about normal match duration. The script knows expansion coordinates from the authored map, but still scouts before issuing Gather on those deposits. It does not use development spawning or free resources.

Preparation must remain free of combat. The script keeps armed units moving between two points near their base so they do not automatically attack visiting miners. Nonconstructing workers join this movement after gathering the 15,220 ore needed beyond the initial 500 to fund the complete roster and facilities. This generates a heavy, bounded stream of public Move commands; its command and control-loop costs are reported separately. It is not an implementation of player Patrol orders. Constructors retain their work, and preparation requires all facilities operational, Tier 3 complete, empty production queues and the exact paid roster.

The march retains each unit's originally accepted destination and requires arrival within 35 world units. Missing units, changed goals, exhausted navigation or unfinished routes fail the capture. The combat phase must produce both damage and casualties. Every simulation tick contributes to a trajectory hash outside step timing; the repeated run must match that hash as well as the final state and full command recording.

## Timing boundaries

- `Simulation::setProfilingEnabled(true)` enables seven CPU wall-clock phases: setup, production, movement/mining, vision, combat, AI and completion. Movement and mining retain their original interleaved order. The total remains the existing enclosing step duration.
- Profiling is disabled by default. Enabling or disabling clears the latest sample. Reset preserves the local preference but clears the sample; load and network snapshot replacement disable it. Timing fields are absent from hashes, saves and wire data.
- Each timed phase adds a clock read. Enabled results include that instrumentation overhead. The workload also repeats without phase profiling or snapshot sampling to verify the same gameplay and command-recording hashes.
- Command durations measure synchronous public command processing. Separate scheduler timings include the script, status queries and its issued commands. Scheduler work is outside simulation-step timing. These values do not measure touch dispatch, acknowledgement latency or the time until a unit visibly responds.
- The script queries production/build status before issuing those commands, so command-only timings can benefit from warm validation state. Include scheduler/preflight cost when interpreting them. A cold server command received without those status queries needs separate measurement.
- Optional snapshots run at the server default of 10 Hz. Each player retains its own `ViewMemory` across preparation, march and combat. Measurements separate fog-filtered view construction, encoding, per-message bytes and the serial batch for all seats. This excludes IPC, WebSocket/TLS transport, network delay and client presentation.
- Four-player mode disables opponent AI by rule. The scripted two-player workload also disables it. The AI timing field therefore does not benchmark a developed AI army; the invariance regression separately exercises normal AI commands.
- `cinder.simprofile on`, `status` and `off` expose the latest completed offline simulation step in Development Unreal builds. Paused, menu and finished-match samples are labelled historical. Existing `cinder.profile` and `cinder.gpuprofile` remain the tools for rendered cadence and engine GPU timing.

## Reproduction

Run from the repository root, with other builds and benchmarks stopped before collecting timed results:

```sh
cmake -S . -B Saved/P0OrderCompletionBuild -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=OFF
cmake --build Saved/P0OrderCompletionBuild --parallel 2
Saved/P0OrderCompletionBuild/CinderlineMatchBaseline --players2 --profile --snapshots > artifacts/match-baseline/two-player.json
Saved/P0OrderCompletionBuild/CinderlineMatchBaseline --players4 --profile --snapshots > artifacts/match-baseline/four-player.json
```

Keep a nonzero exit status as a failure. Do not treat a failed preparation, frozen simulation, rejected issued command or deterministic mismatch as successful performance evidence. Record build flags, hardware and exact source hashes alongside final results.

## Verification and results

The profiling and endpoint-cache implementation passed 12 portable suites, 12 ASan/UBSan suites, 32 server tests and the Mac build with all 37 local Unreal checks. Unreal reported only the two explicitly allowed `idevice_id` host-helper warnings. Logs are under [artifacts/match-baseline](../artifacts/match-baseline/): `portable-final.log`, `sanitizer-final.log`, `server-final.log`, `unreal-cache-build.log` and `unreal-cache-automation.log`. The Unreal report is `Saved/Automation/Integration/20260914T104013Z-23510/index.json`.

The [current synthetic fixtures](../artifacts/match-baseline/synthetic-current.json) retain all 160/200/400 arrivals and the prior deterministic state hashes `0x262c63d50786a124`, `0x0c0167983082b76d` and `0xff16d50b23bd1499`. These fixtures bypass normal economy rules and remain separate from the paid-army workload.

The initial [two-player diagnostic](../artifacts/match-baseline/two-player-diagnostic.json) reached the legal roster at tick 44,480, with 80/80 commanded army units arriving for each team and matching repeat hashes. It overlapped other checks, so its timings are diagnostic. The [four-player diagnostic](../artifacts/match-baseline/four-player-diagnostic.json) reached the roster and all four march destinations, but correctly failed because two teams had fought during setup. That fixture must be corrected before a successful four-player performance claim.

A [one-second CPU stack sample](../artifacts/match-baseline/preparation-sample.txt) caught 721 of 824 samples inside work-target route selection and 693 inside endpoint attachment calculations. It does not establish a sustained utilization percentage or invocation frequency. Source inspection confirms that ordinary returning workers retain their selected depot and route; they are not choosing a new depot every tick. The expensive fallback builds graph connections for hundreds of potential work points when direct access is obstructed.

The optimization caches exact static goal-endpoint graph connections per navigation instance. Exact coordinate and clearance bits form the key. Moving starts and queries that ignore an actual obstacle bypass the cache. FIFO retention is bounded to 2,048 entries and 4 MiB of attachment-vector capacity, plus bounded metadata. Geometry replacement clears it; copied navigation instances get independent caches. Cold speculative building-placement geometry still incurs its original work.

Five new regression cases compare warm and fresh routes, including exact path/cost/search results, reordered and filtered goals, copied and changed geometry, clearances and ignored obstacles, and churn beyond the entry limit. Release and sanitizer checks pass. The frozen before/after captures below additionally retain complete gameplay trajectories and command recordings.

### Frozen before/after results

[The comparison verifier](../artifacts/match-baseline/verify_comparison.py) passes for all four [captures](../artifacts/match-baseline/comparison.json). Both builds use benchmark source SHA-256 `b1a9a73e778e43471e87f670b1fbe344b446777336162a0cd2e92068aca44c2a`; exact source/library/binary hashes are in [benchmark-inputs-sha256.json](../artifacts/match-baseline/benchmark-inputs-sha256.json). The pre-cache library already contains the profiling and earlier order fixes. The cache-only patch and host/compiler/flags are retained in [environment.json](../artifacts/match-baseline/environment.json).

Captures ran sequentially with other task builds and benchmarks stopped on an Apple M1 Max, 32 GiB, macOS 27.0, Apple Clang 21, ARM64 Release `-O3 -DNDEBUG`. Four-player order was optimized then before; two-player order was before then optimized. Each capture repeats without instrumentation and snapshot sampling. This is one pair per workload, not a statistical performance guarantee; wall time includes scheduling effects.

| Players / phase | p50 before → after (ms) | p95 before → after (ms) | p99 before → after (ms) | Max before → after (ms) |
| --- | --- | --- | --- | --- |
| 2 / preparation | 0.671 → 0.594 | 1.829 → 1.388 | 2.566 → 1.738 | 158.37 → 153.70 |
| 2 / march | 0.285 → 0.277 | 0.445 → 0.418 | 0.709 → 0.716 | 6.72 → 6.46 |
| 2 / combat | 0.098 → 0.096 | 0.638 → 0.593 | 3.464 → 2.628 | 18.30 → 5.54 |
| 4 / preparation | 2.177 → 1.705 | 5.774 → 3.932 | 8.121 → 5.305 | 324.66 → 329.04 |
| 4 / march | 0.977 → 0.980 | 1.203 → 1.322 | 2.134 → 3.606 | 12.14 → 17.78 |
| 4 / combat | 0.245 → 0.260 | 1.964 → 1.675 | 6.577 → 5.233 | 15.15 → 10.05 |

Preparation p99 fell 32.3% for two players and 34.7% for four. Four-player march p99 rose, and worst preparation steps remain far above the 50 ms fixed-step interval. The cache is a targeted improvement, with further responsiveness work required. Combat distributions include army attrition across the 120-second window; they are not a constant maximum-army combat load.

Two-player preparation finished at tick 44,420; four-player at 44,180. Every player produced the exact paid 100-unit/184-crew roster. All 80 commanded army units per player arrived within the original 35-unit tolerance, with no missing or changed goals and no exhausted/pending march routes. Issued command counts were 29,156 and 58,781, with zero rejection; combat caused 143 and 316 total unit losses. Every per-tick hash, final hash, full recording hash, route counter and snapshot payload distribution matches before/after and the uninstrumented repeat.

| Players | Final state hash | Per-tick trajectory hash | Full command recording hash |
| --- | --- | --- | --- |
| 2 | `0x31e57fb924c970a7` | `0xaa0f839f1fb63a68` | `0x3948a219e7461ab7` |
| 4 | `0x5dfa62d85b901ac5` | `0x863fd0cdaf9e33c5` | `0xd1721284a45f460b` |

The optimized all-seat snapshot batch p95 stays at or below 0.181 ms for two players and 0.517 ms for four across these phases. Maximum individual messages are 20,264 and 34,196 bytes, below the protocol's 1 MiB limit. These measurements cover fog filtering and encoding only. They do not establish Internet/LAN latency or throughput under concurrent rooms.

### Correcting a self-walled benchmark base

The [original-goal diagnostic](../artifacts/match-baseline/original-goals-diagnostic.json) reached the full paid four-player roster without setup combat but left two team-3 Cinderthrows, IDs 248 and 264, with exhausted march routes. Both stood inside a closed loop of three Siphons and a Crucible. The [geometry diagnosis](../artifacts/match-baseline/march-geometry-diagnosis.json) records the four gaps: approximately 47.24, 54.70, 54.70 and 47.24 world units. Every gap is narrower than the units' 60-unit diameter. Their clear centers lie inside the blocker-center polygon, whose edges are covered by the expanded obstacle circles. This establishes a static enclosure rather than merely repeating the same failed route search.

The [isolated geometry probe](../artifacts/match-baseline/march-geometry-probe.log) also finds routes with radius 27, fails at radius 28–30 without exhausting its search budget, and restores the original radius-30 route when any one of the four blockers is omitted from a diagnostic navigation instance. The actual simulation geometry was not altered. Exact diagnostic source and start/end saves are retained beside the report.

The benchmark now reserves 72 units between each new facility footprint and existing static footprints or the world edge, while retaining normal paid AutoBuild validation and commands. More candidate rings provide room for this layout. The march goals and 35-unit arrival tolerance remain unchanged. This is a deliberately traversable reference base; actual game placement still checks worker access and permits players to wall in larger units. Broader building-clearance feedback remains a gameplay follow-up.

## Remaining acceptance

The [isolated production reproducer](../artifacts/match-baseline/production-exit-repro.cpp) confirms a related gameplay defect. A normally accepted paid Train command spawns a Cinderthrow at `(311.5,4488.5)`, inside the enclosure; `(311.5,4717.6)` on another side of that same Crucible can reach the same rally. [Output](../artifacts/match-baseline/production-exit-repro.log). The fixture initializes buildings/technology/resources synthetically to isolate this behavior; it is separate from the legal paid-army benchmark. Add a focused regression and choose a reachable alternate exit under bounded navigation work. Preserve completed paid items when no local exit is available, and define an explicit fallback/feedback when the rally itself is unreachable. Do not treat the reference fixture's wider spacing as a fix for production.

The four-player optimized capture has a 329 ms maximum step in movement/economy, despite lower preparation percentiles. Cold route/layer construction and speculative build preflight remain expensive paths to profile at the peak, including cold server commands. The current phase measurements identify the enclosing movement/economy cost but do not identify the exact call stack for that worst tick.

The workload measures local simulation and snapshot preparation. P0 remains open for physical input delivery, complete touch-controlled matches, network latency, full supported entity limits, renderer CPU/GPU attribution and sustained AEON memory/thermal/frame pacing. The previously installed phone package predates this work.
