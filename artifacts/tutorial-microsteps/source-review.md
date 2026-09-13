# Tutorial microsteps source review

Review date: 2026-09-13

The five previously reported integration findings are resolved in the current source:

- `tutorialclearmode` clears attack, move, defend, rally, build placement, automatic-build, and local-build-menu state while preserving selection.
- Compact abandoned-foundation recovery opens `SITE` (`sheet`, argument `1`), then targets the real `resumeconstruction` button.
- Recovered construction uses a stable eight-action sequence, closes `SITE`, and ends at wait step `8/8`.
- Ore entity guidance now requires current visibility, matching the controller's real world picker.
- Tutorial-card placement retries with an explicit `SHOW` action when the selected card position would overlap the required world hit target.

The button path matches entity-bearing actions by `EntityId`; world entity guidance verifies the normal projected picker resolves to the requested entity. `SHOW` only focuses the camera and does not select an entity or issue a command. Tutorial state is reset when starting a normal match, loading a match, applying an online match, or returning to the menu.

The final focus, placement-preview, and recovery changes introduce no concrete source-level regression:

- `FocusTutorialTarget` first reduces the camera rig's clamped target distance to at most 900, then focuses the current live entity or ground target. It still issues no simulation command, changes no selection, and grants no camera-input tutorial credit.
- Tutorial build placement draws its pre-gesture ring at the current guide point and queries placement validity without submitting a command. A real non-UI placement press latches `bPointerPlacement`, immediately restoring the live pointer-derived preview and the ordinary release path that submits the chosen point. The early return skips only the redundant placement-status label at the end of `DrawWorldIndicators`.
- Scout, attack-move, defense, and final-assault waiting states now require a surviving unit with a live order toward the current objective. A normal `Stop` returns guidance to an actionable ATTACK step; destroyed forces enter the existing paid replacement-unit or producer-rebuild path. The focused guidance tests cover stopped Scout, Ember attack-move, and defending-army recovery.
- The tutorial TRAIN drawer's unchosen state now leaves every portrait and the queue control visually unaccented and shows `CHOOSE A UNIT TO TRAIN`. Choosing a real `trainkind` sets the existing tutorial choice flag, after which the selected portrait, allocation status, and normal queue accent return. This branch changes presentation only; the button identities, arguments, and command dispatch remain the existing paths.

This is a source-only review. It does not claim compilation, automated-test, rendered-layout, app-launch, or physical-device evidence.
