# Combat feedback checkpoint — 2026-09-11

Current evidence for the source recorded in `combat-feedback-tested-source-sha256.txt` (101 source/content files). Everything below the following divider is historical evidence for earlier checkpoints.

- 26/26 portable rule groups; 3/3 CTests, including 3,000 native render frames. Final warning-free portable rebuild and rule rerun passed.
- 26/26 groups under AddressSanitizer and UndefinedBehaviorSanitizer.
- Unreal Development Editor build and all four strict integration tests passed with zero test errors/warnings. Final run: `20260911T153017Z-79078`.
- Actual Unreal weapon and support/death fixtures inspected at 1280×720 and 667×375. A live fixture advanced to 16 seconds and consumed 120 events.
- Session audio: 45 weapon, 44 impact and 5 explosion PlaySound2D submissions; nine throttled, zero missing assets or unavailable-device requests. This proves submission only.
- Fixed exact fog-corner visibility, sound-budget starvation and development-preview framing during review. No remaining important source-review finding.
- See `combat-feedback-verification.json`, `combat-feedback-runtime-evidence.txt`, four scene captures and `docs/COMBAT_FEEDBACK_PASS.md`. No human full-match, listening/mix, touch or iOS acceptance is claimed.

---

# Verification record

## Scouting-driven AI checkpoint

Current source implements fog-limited enemy sightings, remembered-base objectives, reconnaissance by observation age, paid counter production, immediate witnessed-death invalidation and version 3 persistence with version 1/2 migration. All 23 portable groups and all 3 CTests passed. A fresh ASan/UBSan run passed all 23 groups. The rebuilt Unreal target passed all three strict integration paths with zero warnings/errors, run `20260911T145449Z-61548`. The new engine scenario observes a travelling starting Drudge, retains its position after it leaves vision, and matches 60 continued actor-tick hashes across a temporary save/reload.

Final normal-command match trials ended naturally at 656.35, 788.15 and 843.00 seconds on maps 0/1/2, seed 42. All were won by the game AI. These trials remain below the 20–30 minute target and do not establish human balance. The rebuilt standalone game was relaunched and its menu visually confirmed. Full human match, pointer/touch, animation, rendering performance and iOS acceptance remain open.

Current evidence is in `ai-strategy-*` and `unreal-ai-strategy-*`; `ai-strategy-tested-source-sha256.txt` identifies the final source/content set. [AI_STRATEGY_PASS.md](../docs/AI_STRATEGY_PASS.md) contains the completed checklist, checkpoint report and remaining work. All sections below refer to earlier checkpoints; their use of latest/current/final is relative to those historical checkpoints.

## Worker-driven construction refresh

The latest mechanics replace automatic foundation timers with an assigned Drudge travelling and working at the site. Final source passed all 19 portable rule groups, all 3 CTests (including 3,000 native render frames), a fresh ASan/UBSan run, and both exact Unreal integration tests with zero warnings/errors. UE 5.8.2 rebuilt successfully. The engine report is `unreal-construction-integration-results.json`, run `20260911T143702Z-40096`.

Construction regressions cover travel, zero remote progress, retained cargo with no mining income, interruption/reassignment, single-worker progress, cancellation and combat deaths, blocked access, AI job reservation/recovery, version 2 persistence, version 1 migration, invalid saved relationships and command replay. An initial test exposed a Drudge trapped behind a collinear row of idle workers; construction-only lateral avoidance fixed it while the original test geometry and static collision checks were preserved. A separate review also led to desktop selection and work-beam fog fixes and then cleared the final source.

Fresh normal-command match trials ended naturally at 744.90, 742.95 and 764.10 seconds on maps 0/1/2 with seed 42. All were won by the game AI and remain below the 20–30 minute target. No human balance claim follows. Current output is in `construction-ctest-results.txt`, `construction-portable-details.txt`, `construction-sanitizer-results.txt`, `construction-sanitizer-details.txt`, `construction-match-results.txt`, and `unreal-construction-*`. `construction-tested-source-sha256.txt` identifies this tested source/content set.

The current construction flow still needs hands-on pointer and compact-layout acceptance. These automated results prove simulation and engine integration, not mouse targeting, physical touch or iOS. The checkpoint report is [CONSTRUCTION_PASS.md](../docs/CONSTRUCTION_PASS.md).

## Previous mechanics and presentation checkpoint

The remaining sections describe the earlier checkpoint, before worker-driven construction. Their captures, timings, performance measurements and hash files are retained as historical evidence; references below to the current or final source are relative to that earlier checkpoint.

Recorded on 2026-09-11 using AppleClang 21, C++17, and a Release CMake build on this Mac. `test-environment.txt` contains the machine details. `tested-source-sha256.txt` identifies the exact simulation, native client, test, and build sources; the final checksum verification passed.

The current release suite passed all 14 test groups in 1.12 seconds. CTest passed all three tests: simulation rules, native smoke, and native render stress. The simulation worker's final Debug run also passed all 14 groups with AddressSanitizer and UndefinedBehaviorSanitizer enabled. That successful run was reused after its simulation and test source hashes were checked against the frozen files; elapsed wall time was not captured. `sanitizer-provenance.txt` records the matching hashes, final review log and build configuration. `scripts/test.sh` reproduces the normal checks, and `scripts/test-sanitize.sh` reproduces the sanitizer build.

The tests exercise delivered ore and depletion, paid production and refunds, supply reservation, construction and placement rejection, tier and upgrade prerequisites, ownership and fog, obstacle routing on all three maps, group clearance, ground and air combat, shared-vision attack pursuit, victory and defeat, save/load continuity with and without AI, command replay, reset, bounded fixed-step updates, and AI spending through recorded commands.

The two additional groups cover tactical orders and AI behavior. Ground attackers move around cover before firing; a held unit stays in place under friendly traffic; Menders follow attacking allies and heal real combat damage. The AI tests verify choosing a usable expansion after a deposit is exhausted and sending a distant worker to build local defense while another base's turret remains alive. Final review regressions also verify that army advances continue with or without an expansion Scout, a new Mender joins an existing attack without redundant attack orders, and support finds another armed leader after its first leader dies in combat. These are outcome tests of the changed mechanics, not a new human playthrough.

The construction regression verifies an undamaged Kiln finishes at exactly 1550 HP. In a separate fixture, an actual attack deals 11 damage during construction; after the attacker withdraws, the completed Kiln has 1539 HP. This checks both the reported 1549/1550 display defect and retention of combat damage. Results are in `construction-health-results.txt`.

## Live native playtest

An earlier native Mac build was operated through its actual UI: start a match, queue a worker, box-select workers, reject an occupied building site, place and finish a Kiln, produce mixed units, cancel queued production, move the army, pause, open and close the manual, and save/load. The resumed match ended naturally in defeat at 6:05, with 5,796 ore collected, six units produced, two enemies destroyed, ten units lost, and one structure built. Rematch reset the battlefield to 0:00, 500 ore and five starting workers. No debug resources, damage or winner were injected. This journey predates the latest combat, support, and AI changes.

The earlier completed state remains preserved in `native-completed-match.save`. [The results screenshot](native-results.png) and [the initial battlefield](native-first.png) were captured with the earlier app build available at that stage; they are historical evidence, not captures of the latest mechanics. The subgroup screenshots are explicit control-test fixtures. The compact menu, manual and battlefield screenshots also predate the last control fixes. These saved states and images were not changed during this acceptance refresh.

The subsequent control, health, combat, support, and AI changes passed the automated checks described here. A second complete live mouse playthrough was not rerun. New automation does not replace that missing current-build journey. This native history is neither Unreal nor iOS runtime evidence.

## Native rendering regression

Interactive gameplay exposed a Cocoa exception caused by a nil value in HUD text attributes. The native client now caches font lookup with a fallback and builds text attributes defensively. CTest `native_render_stress` renders 3,000 offscreen frames at 1440 × 900 while advancing 150 simulated seconds and cycling the HUD, manual, pause, and menu states. The current run passed in 16.45 seconds with no Cocoa drawing exception. The test opens no window, reads no user save, and has a 120-second timeout. Native smoke passed in 0.38 seconds; the entire three-test CTest run passed in 17.96 seconds.

The native checks also exercise camera viewport bounds, subgroup selection and layout at desktop and compact dimensions, actual subgroup-chip activation without order mutation, modified versus unmodified A-key handling, and retention of a critical damage warning when a production notification arrives.

These are offscreen rendering and control regressions, not a measured interactive frame rate or a substitute for a current live UI journey. Match-duration checks below were rerun against the final frozen simulation source. Performance measurements were reused after verifying unchanged core and header hashes and disabled AI in the benchmark. The successful final sanitizer result was reused only after its source hashes matched. All recorded portable source hashes were refreshed and verified after the native checks.

## Simulation performance

Measured with `build/native/CinderlineTests --benchmark` before the final AI-only fixes and reused for this refresh. The core simulation, header, build settings, benchmark fixtures and measurement loops are unchanged. Every benchmark fixture disables AI, and the new AI regression subcases do not execute in benchmark mode. `performance-provenance.txt` records the original and current hashes and the unchanged results checksum. These measurements cover simulation update calls. They do not measure native rendering, Unreal rendering, GPU work, or an exported Unreal build.

| Workload | Mean update | p95 update | Maximum update |
| --- | ---: | ---: | ---: |
| 50 additional combat units, plus 10 starting workers | 0.042 ms | 0.115 ms | 0.631 ms |
| 100 additional combat units, plus 10 starting workers | 0.094 ms | 0.282 ms | 3.356 ms |
| 200 additional combat units, plus 10 starting workers | 0.270 ms | 1.123 ms | 6.503 ms |
| 200 units moving for 50 simulated seconds | 0.179 ms | 0.254 ms | 2.153 ms |

The combat groups lose units during each 500-sample run. Their minimum surviving combat counts were 4, 18, and 64. The movement trial keeps all 200 units alive for 1,000 samples. All 200 finish within 100 world units of their assigned goals; median distance is 18.705 and p95 distance is 28.373. Minimum pair separation is 33.196 against a 40-unit diameter, or 83.0 percent clearance. An earlier run before the construction-health fix recorded a 32.590 ms maximum in the 100-unit combat case. The lower maxima in the reused measurements do not erase that observation or establish a guaranteed frame rate.

## Match duration trial

Run with `build/native/CinderlineTests --match`. Team zero is a simple scripted opponent that issues normal public commands and spends its initial funds and gathered ore. Team one uses the actual game AI. Neither side receives debug units, injected resources, forced damage, or a forced winner. Each map used seed 42 once and a 30-minute timeout.

| Map | Natural match duration | Winner | Within the 20–30 minute target |
| --- | ---: | --- | --- |
| 0 | 788.90 seconds, 13m 09s | Game AI | No |
| 1 | 845.30 seconds, 14m 05s | Game AI | No |
| 2 | 865.10 seconds, 14m 25s | Game AI | No |

All three matches ended through gameplay, and none met the 20–30 minute target in this current-source trial. Before the latest mechanics changes, the same scripted trial recorded 15m 34s, 20m 28s, and 14m 04s; those earlier durations are historical. A single seed against this scripted opponent does not establish human match balance, difficulty, or the overall match-duration distribution. The target remains unproven for normal play.

Raw results are in `simulation-test-results.txt`, `construction-health-results.txt`, `ctest-results.txt`, `sanitizer-test-results.txt`, `native-smoke-results.txt`, `native-render-stress-results.txt`, `performance-results.txt`, and `match-results.txt`. Reuse is documented in `sanitizer-provenance.txt` and `performance-provenance.txt`; exact current portable source hashes are in `tested-source-sha256.txt`.

## Unreal development target

UE 5.8.2 builds the Mac Development Editor target. The first content bootstrap completed successfully after cold shader and asset preparation. It imported the original menu and basalt PNGs, created instancing-enabled materials, and saved `/Game/Maps/Frontier`. `unreal-build-results.txt` records the build; source artwork and prompts are in `docs/ART_ASSETS.md`.

Actual standalone Unreal checks displayed the menu and lit battlefield, started the skirmish, gathered ore, panned with arrow keys, returned Home with Space, zoomed with the mouse wheel, and paused/resumed with Escape and Enter. The user separately confirmed that a physical mouse click on START SKIRMISH opens the battlefield. Absolute synthetic mouse clicks on this Mac leave Unreal's cached pointer location stale, so automated coordinate clicks are not accepted as proof of selection or order targeting.

The runtime exposed and led to fixes for UE 5.8 target settings and iterator compilation, physical camera exposure that made the world nearly black, camera bounds that ignored the perspective footprint, and explicit PAUSE behaving like Escape's cancel-first shortcut. Gameplay modes now cannot arm while paused, in menus or at results; match transitions clear stale interaction state. Camera checks include zoom, Home and viewport resizing. The 667 × 375 compact menu and pause controls and an 844 × 390 battlefield were visually inspected. These desktop dimensions do not prove device safe areas or physical gestures.

[The menu](unreal-menu.png), [compact menu](unreal-compact-menu.png), and [battlefield](unreal-battlefield.png) preserve actual engine captures. [The results screen](unreal-results.png) records an earlier unattended Unreal match ending naturally at 5:30 with 4,464 ore collected. It predates the final combat and AI fixes and is not a balanced human match or a current-build full playthrough.

## Unreal command integration

`scripts/test-unreal.sh` runs two named automation tests with NullRHI and requires both exact test paths to finish successfully, with no warnings, errors or unfinished tests. It rejects missing or incomplete reports even if the process returns zero. The real engine world, game mode, battlefield actor and controller participate; the tests call actual adapter ticks and check populated instanced components. They open no gameplay window and leave the player's persistent match untouched.

The lifecycle test checks controller start and map reset, rejected training without a selection, pause during placement, idempotent explicit pause, mode gates while paused/in menus, resume and menu transitions. The economy test issues ordinary paid commands at the battlefield/simulation boundary and verifies carried ore is credited only after delivery, finite deposits decrease, production and construction complete, a produced army accepts controller Hold, and a temporary snapshot round-trips. The recorded economy scenario gathered 252 ore, produced two units, built one structure and retained 342 ore.

Both tests passed. `unreal-integration-results.json` contains their exact names, results, timings and scenario messages. This is command and adapter integration evidence. It does not test pointer targeting, a local-player viewport, physical touch, player-save UI wrappers, GPU performance, packaging or iOS.

## First model and sound integration

The original Blender pack now imports all 15 models with exactly 29,816 triangles, the specified centimeter XYZ bounds, bottom-centered pivots and five material slots. Five shared material palettes passed read-back validation. Eight SoundWaves passed imported channel/rate/duration checks. `unreal-asset-import-results.json` records the graphics-enabled bootstrap. Skim's 64 collapsed exhaust triangles were fixed in Blender before the successful import.

Live standalone diagnostics report 15/15 loaded models, 29 model batches and 10 starting-base entities rendered with no primitive fallbacks. The starting Anchor, Drudges and copper deposits were visually inspected; full-roster/enemy-palette gameplay inspection remains open. A referenced engine daylight cubemap supplies ambient fill. `unreal-presentation-results.json` records actual captures at 1280×720, 667×375 and 844×390, including the compact construction sheet with opening feedback.

Interface and rejected-order audio produced two valid `PlaySound2D` submissions with no missing asset or unavailable device. Completion hooks are implemented, but their audible runtime behavior and the complete mix have not been assessed. Combat sound hooks remain deferred until effects carry explicit semantic events.

Both integration tests passed after model/audio/lighting/ore-color integration in run `20260911T133448Z-14796`. A subsequent HUD-only feedback-bound fix was rebuilt and checked in the actual 667×375 construction screen; the earlier headless tests do not exercise Canvas layout. Final current source/content hashes and capture hashes are recorded. The six portable-source hashes still match the existing 14-rule, CTest and sanitizer evidence.

The hands-on build/produce player journey has been requested in a fresh open game. No new full human match, iOS build, touch or listening-quality claim is made. Details and remaining work are in `docs/PRESENTATION_PASS.md`.

## Deferred product work

The user set the order to mechanics and logic, aesthetics/assets, a full playtest, then iOS. Blender is installed for original models. iOS component Apply was user-reported, but installation completion, signing, packaging and physical-device gameplay remain unverified. The earlier preflight and deferred steps are in `docs/IOS_READINESS.md`. No iOS work blocks this mechanics and desktop presentation pass.
