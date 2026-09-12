# Trackpad camera and building feedback

## Player findings

The Mac trackpad could not use the old middle-mouse camera gesture. The player also reported that a Drudge would not build a Crucible. The current paused match was inspected at 05:34 with 2,678 ore and T1: T1 is below the Crucible's required T2. This explains a definite build restriction in that match, though the precise earlier click sequence was not recorded.

Two related presentation problems were confirmed in source: placement looked valid when only geometry/vision passed, and a primary drag in placement mode could box-select and clear the assigned worker selection. Desktop hints also showed touch instructions.

## Changes

- Option + primary click-drag pans on Mac. Other desktops use Alt. Middle-drag, arrow panning and ordinary drag selection remain available.
- Camera gesture intent is latched at press and consumed on release, including a tiny Option-click or releasing Option first. It preserves selection and pending build/attack modes. Focus loss, pause and interaction reset cancel it.
- Dragging during placement preserves the selected Drudge and does not place accidentally on release. Secondary click still cancels placement, now with explicit feedback.
- A const `buildStatus` query shares validation with actual paid Build commands. Menu queries skip site/range only; full previews check selected/owned Drudges, range, operational buildings, tier, legal visible site and ore. Queries do not spend ore or record commands.
- Build menus show cost, required tier and READY/LOCKED. Selecting a locked item explains why. A placement preview turns green only when the full build check succeeds. Messages use bounded, visible screen regions.
- Desktop/compact-Mac hints explain Option-drag. iOS/Android keep touch hints.

## Crucible progression

Finish a Kiln, then build and finish a Resonator (350 ore). Select the Resonator and choose TECH TIER (500 ore, 100 seconds from T1 to T2). At T2, a Crucible costs 400 ore and takes 85 seconds of active worker construction. Select a Drudge and choose a clear explored site within its 700-unit build range.

## Validation

- [x] 26 portable rule groups and all 3 CTests, including 3,000 native offscreen frames.
- [x] AddressSanitizer/UndefinedBehaviorSanitizer rule run.
- [x] UE 5.8.2 Mac Development build.
- [x] Four Unreal integration paths, including input-gesture and construction regressions (zero test warnings/errors).
- [x] Restart the updated game and inspect its rendered battlefield HUD.
- [ ] Inspect final desktop/compact build-menu and placement-label layouts in the rendered game.
- [x] Physical Option-drag confirmed by the player after restart: camera moves.
- [ ] Physical placement confirmation by the player.

Portable query tests cover readonly state/recording, precise failures, prerequisites, visibility, range and a paid accepted Crucible. Unreal gesture tests cover regular selection, nine pan cases, placement drift, cancellation, pause/reset and touch-state behavior. NullRHI tests cannot establish physical trackpad delivery, viewport targeting or camera motion.

The user authorized discarding the paused match and restarting Unreal to apply these changes. The old process was closed, the new build launched, and a read-only CUA screenshot showed a new 00:26 match with a selected Drudge labelled BUILDING / MINING PAUSED and two visible unfinished structures. The match had already been started during launch; further UI automation was stopped to leave that new session intact. This screenshot confirms the rendered construction HUD, not physical gesture acceptance or every new build-menu/preview label. No existing player save was overwritten by verification. iPhone control refinement, gestures, safe areas and performance remain in the later iOS pass.

All four integration tests passed in `Saved/Automation/Integration/20260911T181255Z-77425/index.json`; a report snapshot is included with the artifacts. A read-only controller review found no important actionable issue. The tested source/asset manifest contains 132 files. The live game uses `Saved/Logs/CinderlineTrackpadBuildPlay.log`.

## Follow-up: fog while panning

The player reported that moving the camera extensively may expose hidden areas. Physical panning itself works. A read-only trace found no camera writes to either visibility mask: live friendly entities determine current visibility, while explored cells persist. Enemy models are separately excluded when not currently visible. The fog material samples world XY, and its CPU texture keeps hidden cells opaque. A screenshot at 04:19 showed normal base terrain and a fog boundary, without capturing the reported leak. Whether the symptom is transient terrain showing through, persistent exploration, or enemy visibility remains unconfirmed; do not label it fixed. Preserve the active match while gathering evidence.

A second presentation review found no definite camera-dependent leak. Known behavior that can resemble one: finishing construction doubles that building's sight radius from its unfinished value; moving units continue to reveal ground; remembered cliffs/ore protrude above the depth-tested ground fog and remain lit when revisited. Temporal-AA/translucency or occlusion artifacts are hypotheses only until a rendered reproduction exists. No speculative fog-renderer change was applied.

Evidence is under `artifacts/trackpad-build-feedback/`. Engine/visual checks are separate from portable simulation checks; physical control acceptance remains separate from both.
