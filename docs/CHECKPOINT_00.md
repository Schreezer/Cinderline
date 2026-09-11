# Checkpoint 0 report

CHECKPOINT: Project foundation

STATUS: Unreal desktop launch and camera navigation are demonstrated. The first mechanics pass also passes rule, sanitizer and engine integration checks. A fuller playtest remains open. Physical touch and iOS validation remain deferred.

WORKING:

- Native macOS playtest starts from a menu, simulates workers gathering and depositing ore, constructs buildings, produces armies and accepts contextual orders.
- Automated matches reach HQ destruction without injected resources, debug armies or a forced winner.

IMPLEMENTED:

- Original Cinderline roster, three map layouts and centralized balance definitions.
- Shared C++ state and command interface, economy, construction, production, research, movement/formations, combat, vision, economic AI, saved state and command recording.
- Unreal module, game mode, procedural instanced battlefield, camera, player controller, touch/mouse input, fog, minimap and command HUD.
- Unreal build/bootstrap/play/package helpers and native/simulation test commands.

TESTED:

- Current mechanics verification: 14/14 rule groups, 3/3 CTests with 3,000 native render frames, 14/14 ASan/UBSan, and 2/2 Unreal world/command integration tests. See [the mechanics report](MECHANICS_PASS.md) for the final corrections.

- Rule tests cover actual deposits and depletion, spending, supply, placement, production, research, visibility, movement, combat, victory, save/load and replay reconstruction.
- AddressSanitizer and UndefinedBehaviorSanitizer checks.
- 50/100/200-unit simulation scenarios and sustained 200-unit formation movement.
- Actual native UI operation: menu start, worker queue, box selection, placement rejection and acceptance, completed Kiln, mixed production, cancellation, pause/manual behavior, saved-match restore and army movement. The resumed playtest reached natural defeat at 6:05, displayed statistics and rematched into a fresh battlefield.
- Desktop and compact native screenshots. Native rendering stress crosses construction completion and the results screen.
- UE 5.8.2 Mac Editor C++ build, successful Python asset bootstrap and actual lit battlefield launch.
- Actual Unreal menu start, gathering, arrow pan, Home, wheel zoom, pause/resume, desktop and compact HUD inspection. The user confirmed physical mouse activation of START SKIRMISH.
- An undefended Unreal match ended naturally through AI gameplay at 5:30. This was not a balance test.
- Source review and camera footprint math checks supplement runtime evidence; they do not prove physical touch controls.

ISSUES FOUND:

- UE 5.8 required current target build settings and actor iterator syntax changes. Manual physical camera exposure made the initial world nearly black. Camera bounds ignored the visible ground footprint.

- Workers stopped outside gather/drop-off range because path completion tolerance exceeded interaction range.
- New buildings could overlap friendly units, and automatic worker assignment could reveal unexplored ore.
- Large formations had blocked goals and units displaced after arrival.
- Native HUD could crash when an attributed-text dictionary received a nil value.
- Native camera/selection and completed-building health had further polish issues during live testing.
- Single-seed automated match durations vary outside the requested 20–30 minute range.

FIXES MADE:

- Updated the UE 5.8 build settings and actor lookup code, disabled physical camera exposure for this RTS camera, and constrained the perspective ground footprint.
- Configured original artwork imports and instanced material usage; removed an unrelated Android file-server plugin that was writing generated settings into the project.

- Tight final approach tolerance, validated placement/ownership/vision and explored-only automatic resource selection.
- Valid spaced formation destinations, return to assigned slots and spaced production rally goals.
- Bounded simulation catch-up after large elapsed-time gaps.
- Cached fonts with fallbacks and nil-safe attributed-text construction; added an offscreen rendering regression.
- Construction health now reaches its exact maximum when undamaged while preserving damage received during construction.
- Native subgroup chips filter the current selection, camera bounds account for viewport size, modified shortcuts are ignored, and damage alerts take priority over production notices.
- Independent Unreal review fixed small phone targets, selection/input cancellation, air-unit projection, subgroup filtering, save-path conversion, map restoration and bootstrap plugin dependencies.

CURRENT PLAYABLE EXPERIENCE:

- A native Mac development runner exercises the actual shared C++ RTS simulation. Its visuals are procedural placeholders with a complete first skirmish loop.
- Unreal now builds and runs the same skirmish simulation with generated menu art, a textured battlefield and a command HUD. Camera bounds account for perspective, zoom and viewport resizing.
- No iOS build, simulator run, physical-device gameplay, multiplayer session or competitive-balance result is claimed. Automated coordinate clicks remain unreliable on this Mac; keyboard checks and user mouse confirmation are recorded separately.
- Exact command output, timings, match results and tested source hashes are in [the verification record](../artifacts/verification-summary.md).

NEXT CHECKPOINT:

- Import and inspect the original Blender models, improve HUD readability and integrate original audio cues.
- Follow the user's revised order: mechanics and logic, aesthetics and assets, full playtest, then iOS.
- Keep physical touch acceptance open until tested on the target device.
