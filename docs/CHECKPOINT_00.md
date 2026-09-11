# Checkpoint 0 report

CHECKPOINT: Project foundation

STATUS: Implemented and tested through the shared simulation and native development runner. Unreal acceptance remains pending engine installation.

WORKING:

- Native macOS playtest starts from a menu, simulates workers gathering and depositing ore, constructs buildings, produces armies and accepts contextual orders.
- Automated matches reach HQ destruction without injected resources, debug armies or a forced winner.

IMPLEMENTED:

- Original Cinderline roster, three map layouts and centralized balance definitions.
- Shared C++ state and command interface, economy, construction, production, research, movement/formations, combat, vision, economic AI, saved state and command recording.
- Unreal module, game mode, procedural instanced battlefield, camera, player controller, touch/mouse input, fog, minimap and command HUD.
- Unreal build/bootstrap/play/package helpers and native/simulation test commands.

TESTED:

- Rule tests cover actual deposits and depletion, spending, supply, placement, production, research, visibility, movement, combat, victory, save/load and replay reconstruction.
- AddressSanitizer and UndefinedBehaviorSanitizer checks.
- 50/100/200-unit simulation scenarios and sustained 200-unit formation movement.
- Actual native UI operation: menu start, worker queue, box selection, placement rejection and acceptance, completed Kiln, mixed production, cancellation, pause/manual behavior, saved-match restore and army movement. The resumed playtest reached natural defeat at 6:05, displayed statistics and rematched into a fresh battlefield.
- Desktop and compact native screenshots. Native rendering stress crosses construction completion and the results screen.
- Unreal source review, shell/Python/JSON validation and compact layout geometry. These are not engine runtime checks.

ISSUES FOUND:

- Workers stopped outside gather/drop-off range because path completion tolerance exceeded interaction range.
- New buildings could overlap friendly units, and automatic worker assignment could reveal unexplored ore.
- Large formations had blocked goals and units displaced after arrival.
- Native HUD could crash when an attributed-text dictionary received a nil value.
- Native camera/selection and completed-building health had further polish issues during live testing.
- Single-seed automated match durations vary outside the requested 20–30 minute range.

FIXES MADE:

- Tight final approach tolerance, validated placement/ownership/vision and explored-only automatic resource selection.
- Valid spaced formation destinations, return to assigned slots and spaced production rally goals.
- Bounded simulation catch-up after large elapsed-time gaps.
- Cached fonts with fallbacks and nil-safe attributed-text construction; added an offscreen rendering regression.
- Construction health now reaches its exact maximum when undamaged while preserving damage received during construction.
- Native subgroup chips filter the current selection, camera bounds account for viewport size, modified shortcuts are ignored, and damage alerts take priority over production notices.
- Independent Unreal review fixed small phone targets, selection/input cancellation, air-unit projection, subgroup filtering, save-path conversion, map restoration and bootstrap plugin dependencies.

CURRENT PLAYABLE EXPERIENCE:

- A native Mac development runner exercises the actual shared C++ RTS simulation. Its visuals are procedural placeholders with a complete first skirmish loop.
- The Unreal project has not yet compiled or launched. No iOS build, simulator run, physical-device run, multiplayer session or competitive-balance result is claimed.
- Exact command output, timings, match results and tested source hashes are in [the verification record](../artifacts/verification-summary.md).

NEXT CHECKPOINT:

- Finish UE 5.8.2 installation, run `./scripts/unreal.sh setup`, then `./scripts/unreal.sh play`.
- Fix engine build/runtime failures and verify camera navigation before completing checkpoint 0.
- Continue touch camera and command validation in the actual Unreal runtime, then on iPhone/iPad.
