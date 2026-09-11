# Verification record

Recorded on 2026-09-11 using AppleClang 21, C++17, and a Release CMake build on this Mac. `test-environment.txt` contains the machine details. `tested-source-sha256.txt` identifies the exact simulation, native client, test, and build sources; the final checksum verification passed.

The release suite passed all 12 test groups. After the final construction-health and native control fixes, CTest passed all three tests: simulation rules, native smoke, and native render stress. A separate Debug build passed the same simulation tests with AddressSanitizer and UndefinedBehaviorSanitizer enabled in 8.89 seconds. `scripts/test.sh` reproduces the normal checks, and `scripts/test-sanitize.sh` reproduces the sanitizer build.

The tests exercise delivered ore and depletion, paid production and refunds, supply reservation, construction and placement rejection, tier and upgrade prerequisites, ownership and fog, obstacle routing on all three maps, group clearance, ground and air combat, shared-vision attack pursuit, victory and defeat, save/load continuity with and without AI, command replay, reset, bounded fixed-step updates, and AI spending through recorded commands.

The construction regression verifies an undamaged Kiln finishes at exactly 1550 HP. In a separate fixture, an actual attack deals 11 damage during construction; after the attacker withdraws, the completed Kiln has 1539 HP. This checks both the reported 1549/1550 display defect and retention of combat damage. Results are in `construction-health-results.txt`.

## Live native playtest

The native Mac app was operated through its actual UI: start a match, queue a worker, box-select workers, reject an occupied building site, place and finish a Kiln, produce mixed units, cancel queued production, move the army, pause, open and close the manual, and save/load. The resumed match ended naturally in defeat at 6:05, with 5,796 ore collected, six units produced, two enemies destroyed, ten units lost, and one structure built. Rematch reset the battlefield to 0:00, 500 ore and five starting workers. No debug resources, damage or winner were injected.

The completed state is preserved in `native-completed-match.save`; [the results screenshot](native-results.png) was rendered from it using the final app. [The initial battlefield](native-first.png) also uses the final app. The subgroup screenshots are explicit control-test fixtures. The compact menu, manual and battlefield screenshots predate the last control fixes.

The subsequent subgroup, camera, shortcut, alert and construction-health changes passed the regressions below. A second complete live UI match was not rerun after those final changes. None of this is Unreal or iOS runtime evidence.

## Native rendering regression

Interactive gameplay exposed a Cocoa exception caused by a nil value in HUD text attributes. The native client now caches font lookup with a fallback and builds text attributes defensively. CTest `native_render_stress` renders 3,000 offscreen frames at 1440 × 900 while advancing 150 simulated seconds and cycling the HUD, manual, pause, and menu states. It passed in 16.29 seconds with no Cocoa drawing exception. The test opens no window, reads no user save, and has a 120-second timeout. The entire three-test CTest run passed in 17.03 seconds.

The native checks also exercise camera viewport bounds, subgroup selection and layout at desktop and compact dimensions, actual subgroup-chip activation without order mutation, modified versus unmodified A-key handling, and retention of a critical damage warning when a production notification arrives.

These are offscreen rendering and control regressions, not a measured interactive frame rate or a substitute for the separate live UI journey. Performance, sanitizer, and match-duration checks below were rerun against the final simulation source. All recorded source hashes were refreshed after the final native run.

## Simulation performance

Run with `build/native/CinderlineTests --benchmark`. These measurements cover simulation update calls. They do not measure native rendering, Unreal rendering, GPU work, or an exported Unreal build.

| Workload | Mean update | p95 update | Maximum update |
| --- | ---: | ---: | ---: |
| 50 additional combat units, plus 10 starting workers | 0.022 ms | 0.050 ms | 0.542 ms |
| 100 additional combat units, plus 10 starting workers | 0.078 ms | 0.247 ms | 2.206 ms |
| 200 additional combat units, plus 10 starting workers | 0.302 ms | 1.151 ms | 6.796 ms |
| 200 units moving for 50 simulated seconds | 0.195 ms | 0.282 ms | 2.301 ms |

The combat groups lose units during each 500-sample run. Their minimum surviving combat counts were 6, 17, and 65. The movement trial keeps all 200 units alive for 1,000 samples. All 200 finish within 100 world units of their assigned goals; median distance is 18.705 and p95 distance is 28.373. Minimum pair separation is 33.196 against a 40-unit diameter, or 83.0 percent clearance. An earlier run before the construction-health fix recorded a 32.590 ms maximum in the 100-unit combat case. The lower maxima in the final run do not erase that observation or establish a guaranteed frame rate.

## Match duration trial

Run with `build/native/CinderlineTests --match`. Team zero is a simple scripted opponent that issues normal public commands and spends its initial funds and gathered ore. Team one uses the actual game AI. Neither side receives debug units, injected resources, forced damage, or a forced winner. Each map used seed 42 once and a 30-minute timeout.

| Map | Natural match duration | Winner | Within the 20–30 minute target |
| --- | ---: | --- | --- |
| 0 | 933.75 seconds, 15m 34s | Game AI | No |
| 1 | 1227.70 seconds, 20m 28s | Game AI | Yes |
| 2 | 843.65 seconds, 14m 04s | Game AI | No |

All three matches ended through gameplay. Only map 1 met the target in this trial. A single seed against this scripted opponent does not establish human match balance, difficulty, or the overall match-duration distribution.

Raw results are in `simulation-test-results.txt`, `construction-health-results.txt`, `ctest-results.txt`, `sanitizer-test-results.txt`, `native-smoke-results.txt`, `native-render-stress-results.txt`, `performance-results.txt`, and `match-results.txt`.
