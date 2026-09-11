# Worker-driven construction

CHECKPOINT: Drudge construction mechanics

STATUS: Implemented, rebuilt and verified in automated simulation/native/Unreal checks. Current-build pointer playtesting remains open.

WORKING:

- One assigned Drudge travels to a paid foundation, stops mining and constructs at its perimeter.
- Other miners keep their jobs. Group selection assigns one builder at the normal construction rate.
- Workers retain cargo; completion, cancellation and site destruction restore an uninterrupted prior mining job.
- New orders and worker death pause the site. A replacement resumes retained progress without another ore charge.

IMPLEMENTED:

- Construction ownership and physical work checks in the shared simulation, including retained combat damage.
- Free resume command, deterministic worker selection and AI reservation/recovery of builders.
- Version 2 saves retain construction assignments and mining context. Version 1 unfinished sites load paused.
- Unreal and native construction state labels and resume actions. Unreal shows an amber work beam only during active construction.
- Desktop left-click selects a site; a selected Drudge's right-click resumes it. Touch uses contextual tapping. ASSIGN DRUDGE on a paused site chooses an available worker without taking another builder.

TESTED:

- All 19 portable rule groups passed. All 3 CTests passed, including native controls and 3,000 offscreen render frames (18.35 seconds total).
- All 19 groups passed with AddressSanitizer and UndefinedBehaviorSanitizer (42.06 seconds). This was a fresh run against the final source.
- UE 5.8.2 Mac Development Editor rebuilt successfully. Both exact Unreal integration tests passed with zero warnings, errors or unfinished tests in run `20260911T143702Z-40096`.
- The real engine economy fixture checks a paid foundation, physical travel with no early progress, Stop and free resume, completion and worker release. The simulation tests additionally check cargo/mining, cancelled/destroyed sites, worker death, inaccessible sites, multi-worker selection, retasking, AI assignment, save continuation, replay, v1 migration and invalid v2 assignment rejection.
- One normal-command scripted match per map (seed 42) ended naturally in 744.90, 742.95 and 764.10 seconds, all won by the game AI. These remain below the 20–30 minute target and do not establish human balance.
- A separate source review found no remaining important issue after the selection and fog fixes below.
- Raw output and exact source hashes are in `artifacts/construction-*` and `artifacts/unreal-construction-*`.

ISSUES FOUND:

- Initial regression tests exposed a legal construction approach blocked by a row of idle workers. The builder's movement and symmetric separation cancelled each other.
- The macOS Bash test runner rejected its empty argument expansion. The runner now uses the compatible quoted argument form.

FIXES MADE:

- Replaced independent foundation timers with attended work.
- Added construction-only steering around nearby ground units to resolve the idle-worker traffic stall. Static collision and Hold anchoring remain authoritative.
- Kept ordinary desktop selection separate from right-click assignment and prevented work beams from revealing foundations through fog.
- Added tests for actual arrival, no mining while working, interruptions, cancellation, deaths, blocked access, save migration, replay and AI builder recovery.

CURRENT PLAYABLE EXPERIENCE:

- The rebuilt Unreal game was relaunched and its menu visually confirmed. It is left open for the updated player flow.
- The existing first faction and maps use worker-driven construction. Actual pointer targeting and compact construction status still need a current-build hands-on check; headless engine tests do not establish those behaviors.

NEXT CHECKPOINT:

- Play the updated build/interrupt/resume/produce sequence, then continue the full skirmish playtest. iOS remains deferred.
