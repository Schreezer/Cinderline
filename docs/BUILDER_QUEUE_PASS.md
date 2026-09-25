# Drudge construction queues

Requested 2026-09-15. This is a separately authorized feature; campaign validation remains paused while the other visual pass has Unreal.

## Behavior

A selected Drudge can receive several construction orders and a final mining assignment. Ordinary commands replace its current plan. Shift on desktop and the touch queue control append work for that same Drudge. Global automatic construction keeps its existing worker allocation behavior.

- A new queued structure is a plan. Its outline marks the site, but it has no collision, health, ownership of the ground, or ore charge until construction starts.
- Starting each planned structure checks the current site, prerequisites, ore and route. An invalid job is skipped with feedback; the remaining plan continues. Queuing does not guarantee future affordability or reserve a site against other builders.
- A queued resume order references an existing paid foundation. It does not charge again.
- An explicit mining order after construction sends the Drudge to the chosen deposit. Without that order, a builder previously mining resumes mining after its construction chain.
- The existing limits remain: 16 future orders per unit, 4,096 per player, plus the network snapshot size limit. Rejected append commands preserve the accepted plan.
- Clear queued orders keeps the active job. Stop and ordinary replacement commands discard the future plan and leave an interrupted paid foundation paused. A dead worker's plan is discarded.
- Each simulation step or separately submitted command validates at most four queued worker jobs. Excess work waits in its saved plan; appending or clearing orders also works while waiting.
- Future plans are private to their owner online. The server executes and validates the same rules as solo play.

## Compatibility

Save version 14 adds building kinds to queued orders and continues to read earlier skirmish saves, including versions 11–13. Network protocol 11 adds the corresponding snapshot data and command semantics. Updated clients and their server/worker must be released together; existing installed packages and a public backend are not updated by source changes.

The unreleased campaign checkpoint adapter also writes version 14 metadata to match its simulation file. Prototype campaign checkpoints from version 13 remain unsupported by that adapter; ordinary skirmish save migration is separate. Campaign runtime validation is still paused. Its build objective now records a queued site and binds the foundation when the Drudge actually starts it; a source regression covers that delayed observation. The later campaign package verifier is aligned to save 14/protocol 11, but its source snapshot and runtime evidence still need refreshing when work resumes.

## Progress

- [x] Inspect construction, order completion, input, save and network paths.
- [x] Agree the execution, payment, interruption and privacy rules above.
- [x] Implement safe sequential construction, explicit mining and failure handling.
- [x] Implement desktop/touch controls and selected-worker plan feedback.
- [x] Implement save migration and authoritative network support.
- [x] Pass portable behavior, persistence and network regressions.
- [x] Pass focused address/undefined-behavior sanitizer checks.
- [ ] Build Unreal and validate actual mouse/touch UI after the visual pass releases it.
- [ ] Package and physically playtest the matching build.

## Verification

Local validation completed on 2026-09-15. The [verification receipt](../artifacts/builder-queues/verification.json) records source hashes, binaries, logs and the remaining acceptance limits.

- 22 portable Release suites pass. The final focused builder executable passes all 14 cases, including budget-deferred appends, natural Clear/Stop mining recovery and intermediate save/load.
- 38 Node/server tests pass with zero skips. Real clients exercise private mining plans and unpaid construction that later creates exactly one paid foundation.
- Four focused AddressSanitizer/UndefinedBehaviorSanitizer suites pass: builder queues, tactical queues, network protocol and production allocation.
- Review findings were fixed and the confirmation pass was clean. `git diff --check` passes.
- No Unreal build, preview, package or phone install was attempted in this feature pass, preserving the user's explicit pause. Desktop/touch controller tests and the campaign compatibility regression are source-ready, not runtime-verified.

Portable simulation tests do not establish Unreal rendering, real touch behavior, device thermals or a deployed online service.

## Remaining playtest

1. Select one mining Drudge, begin a structure and append two different structures. Verify numbered plans, one working foundation at a time, and ore charged at each start.
2. Append a chosen mine. Observe the same worker complete all sites, travel to that deposit and deliver ore.
3. Clear future orders during the first build. Confirm that building still completes. Repeat using Stop and confirm its foundation pauses.
4. Obstruct a planned site or spend its required ore before the worker gets there. Confirm useful feedback and progression to a valid later job.
5. Save/load midway; repeat with two online clients and a reconnect. Only the owning player should see the future plan.
6. Repeat the controls on Android and iPhone, checking that queue buttons and placement taps never issue unintended world movement commands.
