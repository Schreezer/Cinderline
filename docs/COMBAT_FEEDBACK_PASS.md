# Combat feedback checkpoint

Current priority: mechanics and logic → aesthetics and assets → full playtest → iOS. The completed mechanics checkpoints are recorded in [TASKBOARD.md](TASKBOARD.md).

Goal: make firing, hits, healing and destruction distinguishable in the Unreal battlefield, with combat sound cues tied to real simulation events. Keep combat timing and balance unchanged, respect fog of war, and prevent old sounds replaying after loading a match.

## Work

- [x] Inspect the existing simulation effects and audio integration; identify the shared trace/explosion representation and missing combat sound dispatch.
- [x] Add explicit, deterministic combat feedback events with stable identity and visibility information.
- [x] Render distinct weapon, impact, healing and destruction feedback in Unreal and keep the native development renderer compatible.
- [x] Dispatch combat audio once per new observable event, with bounded playback and safe match/load lifecycle behavior.
- [x] Add focused simulation and Unreal regressions, including fog isolation and save/load behavior.
- [x] Review and run portable/native, sanitizer, Unreal build and integration checks.
- [x] Inspect a rendered combat scene in Unreal and record what was actually verified.
- [x] Update the task board, record test evidence and save a local checkpoint commit.

## Acceptance

- Weapon fire, healing and deaths have different visible treatment; siege fire is identifiable.
- Effects and sound never disclose the position or existence of an unseen enemy combat event.
- Feedback does not change damage, cooldowns, resources, AI decisions or simulation stepping.
- Loading or restarting a match does not replay stale combat audio; older save versions remain readable.
- Automated checks exercise real combat and the Unreal adapter. Rendered inspection and audio submission evidence are distinguished from listening and a human full-match test.

## Deferred

- Rigged unit animation, additional environment art and a full audio mix/listening review.
- Human match balance and the 20–30 minute pacing target.
- iOS packaging, input and performance tests.

## Progress

- 2026-09-11: Started after the completed scouting AI checkpoint. The current effect records conflate healing with weapon fire and siege fire with deaths; combat audio cues exist but are not dispatched from simulation events.
- Added Weapon, Impact, Heal and Death events. Version 4 persists event IDs, lifetimes, kinds and visibility masks; versions 1–3 still load, discarding their ambiguous cosmetic records.
- Renderers now distinguish Needle beams, Cinderthrow arcs, light/heavy traces, Mend support links, local hit sparks and footprint-scaled death rings/debris. Animation changes presentation only.
- Review found that spaced fog samples could miss a 1.4-unit hidden-cell corner crossing. Exact segment/cell intersection now rejects it; the regression also verifies that revealing the same cell permits the beam.
- Review also changed a shared eight-request sound budget to one request per combat cue per update. A large volley can no longer crowd out impacts. Hidden, offscreen and duplicate events are consumed without later replay.
- The first portable pass passed all 26 rule groups and all three CTests, including 3,000 native render frames. The Unreal Development Editor build passed. A harmless test range-loop copy warning was cleaned up before final verification.
- The final portable rebuild is warning-free; all 26 groups pass again. All 26 also pass with ASan/UBSan. All four Unreal integration paths pass with zero test warnings/errors.
- Inspected weapon and support fixtures at 1280×720 and 667×375. Adjusted the development preview camera so combat actors sit above the command panel. A live fixture continued to 16 seconds with actual damage, unit deaths, expired effects and new audio requests.
- Live Unreal diagnostics recorded 120 consumed events in the active fixture. Across that game-instance session, 45 weapon, 44 impact and 5 explosion submissions succeeded; nine requests were throttled, with zero missing assets or unavailable-device requests. These counts establish submissions, not listening quality.

## Checkpoint report

CHECKPOINT: Combat feedback

STATUS: Complete for this bounded implementation, verification and local checkpoint. Listening and human full-match playtesting remain open.

WORKING: Distinct fire profiles, hit sparks, support links, death rings/debris and event-driven combat sounds in the Unreal battlefield.

IMPLEMENTED: Typed deterministic events, stable IDs, event-time/current visibility, exact fog-cell link gating, version 4 persistence with legacy readers, matching native rendering and at most one request per combat cue per adapter update. Match/load transitions skip retained events.

TESTED: 26/26 portable groups, three CTests including 3,000 native render frames, 26/26 groups under ASan/UBSan, Unreal Development Editor build and four strict engine integration paths. Actual desktop/compact screenshots and live combat/audio diagnostics are saved below. Final source review found no remaining important issue.

ISSUES FOUND: A sampled link check missed tiny fog-cell corner crossings; a shared audio budget allowed volleys to crowd out impact cues; the initial development camera framed some actors behind the command panel.

FIXES MADE: Exact segment/cell intersections with a corner regression; per-cue coalescing; a wider, offset preview camera. Cleaned up one test compiler warning.

CURRENT PLAYABLE EXPERIENCE: The rebuilt game is open at the normal skirmish menu. The ordinary skirmish uses the new feedback. Development fixtures demonstrate its rendering and event flow. They do not establish human match balance, listening quality, touch input or mobile performance.

NEXT CHECKPOINT: Hands-on construction and full-skirmish feedback, combat mix listening, then roster animation and remaining presentation polish. Keep iOS deferred.

Evidence: [portable/native](../artifacts/combat-feedback-portable-native-details.txt), [final portable rules](../artifacts/combat-feedback-final-rules-details.txt), [sanitizers](../artifacts/combat-feedback-sanitizer-details.txt), [Unreal integration](../artifacts/unreal-combat-feedback-integration-results.json), [runtime/audio](../artifacts/combat-feedback-runtime-evidence.txt), [tested source](../artifacts/combat-feedback-tested-source-sha256.txt) and [verification metadata](../artifacts/combat-feedback-verification.json).

Rendered development fixtures: [desktop weapons](../artifacts/combat-feedback-weapons.png), [desktop support/destruction](../artifacts/combat-feedback-support.png), [compact weapons](../artifacts/combat-feedback-weapons-compact.png), [compact support/destruction](../artifacts/combat-feedback-support-compact.png) and [live fixture](../artifacts/combat-feedback-live.png).
