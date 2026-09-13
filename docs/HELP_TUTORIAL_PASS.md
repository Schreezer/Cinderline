# Help and tutorial pass

Started 12 September 2026 at the player's request.

Superseding note, 13 September 2026: the original nine-objective practice mission below is now historical. The current first-run experience is the implemented 14-objective guided match in [GUIDED_MATCH_PASS.md](GUIDED_MATCH_PASS.md), with a one-time accept/skip offer, a staged paid opponent, research, reinforcement, defense and a real-victory completion gate. All 21 Unreal tests, all 14 Mac render cases, the signed SDK 27 package and all four native iOS-on-Mac cases pass. A human full-tutorial playthrough and physical AEON touch acceptance remain pending; AEON was not changed during this pass.

## Scope

- [x] Add a field guide reachable from the main menu, battlefield and pause screen.
- [x] Explain trackpad/mouse and touch controls, economy, construction, army roles, fog, technology, common failures and saving/winning. Derive reference numbers from simulation definitions.
- [x] Add a separate guided practice match with no attacking AI, ordinary costs/build times and automatic objectives based on actual actions and state.
- [x] Teach camera movement, selection, mining, worker production, Kiln construction, an Ember squad, Siphon capacity, scouting and attack-move.
- [x] Keep the guide modal, pause while reading, protect the normal skirmish save during training, and support restart/exit without stale input.
- [x] Verify objective progression, rejected-action handling, lifecycle/input isolation and save protection.
- [x] Build and inspect desktop and compact help/tutorial layouts in Unreal. Record what is actually tested.

## Design decisions

Training starts from the main menu. It uses a fixed practice scenario and never overwrites the saved skirmish. It can be restarted; full training-session persistence is outside this pass. Completion is remembered locally. Reading the guide during play pauses the match; closing it returns to Pause so resuming remains explicit. The guide has selectable desktop and touch instructions. The training objective card stays visible during play and offers a location hint and relevant help.

## Evidence

The final Mac Development build succeeded with two compile actions. `./scripts/test-unreal.sh --tutorials` passed all eight expected tests with zero errors, warnings or unfinished tests. The exact report is [integration-results.json](../artifacts/help-tutorial/integration-results.json); the original run is `Saved/Automation/Integration/20260911T210349Z-40955/`.

The four new tutorial tests cover rejected and canceled commands, completed production, early actions, definition-driven content, focus behavior and the full nine-objective practice sequence using normal economy, construction and combat. The existing world lifecycle test now covers guide input isolation, explicit resume after backgrounding/help, training save/load routing, restart confirmation, restoring a normal AI match and the F1 development binding.

Live Mac checks covered menu entry, all nine desktop guide topics, desktop/touch copy switching, reference navigation, training start, real camera-input progression to selection, help pause/close/resume, compact section paging and restart cancellation/confirmation. The last compact run used a 1334×750 framebuffer at 2× DPI, corresponding to a 667×375 content area. Desktop captures used a 2560×1440 framebuffer. UI automation screenshots include the window title bar.

Two issues found through rendered inspection were corrected: F1 also triggered Unreal's inherited wireframe command, and the first compact card covered the camera's focus point. The final input config removes that binding; a regression verifies it. The compact card now places its 44-unit controls in the header and leaves the battlefield center clear. Compact topic navigation also uses readable fixed labels.

Useful captures:

- [Desktop menu](../artifacts/help-tutorial/desktop-menu.png) and [field guide](../artifacts/help-tutorial/desktop-guide-controls.png)
- [Desktop tutorial](../artifacts/help-tutorial/desktop-training-camera.png) and [paused training](../artifacts/help-tutorial/desktop-training-paused.png)
- [Compact menu](../artifacts/help-tutorial/compact-menu.png) and [compact objective card](../artifacts/help-tutorial/compact-training-select.png)
- [Compact touch section](../artifacts/help-tutorial/compact-guide-touch-command.png) and [reference unlock](../artifacts/help-tutorial/compact-reference-unlock.png)
- [Restart confirmation](../artifacts/help-tutorial/compact-training-restart.png) and [reset practice](../artifacts/help-tutorial/compact-training-restarted.png)

Preview launches used `t.MaxFPS 30` and disabled shader workers. Final diagnostics confirmed the 30 FPS ceiling and native viewport/render-target dimensions; this is configuration evidence, not a GPU utilization or sustained frame-time measurement. Unreal was closed after the checks. [verification-summary.json](../artifacts/help-tutorial/verification-summary.json) records the limits and evidence; [source-hashes.json](../artifacts/help-tutorial/source-hashes.json) identifies the tested source/config inputs.

## Remaining acceptance

- Play the full guided mission with a physical mouse/trackpad and assess instruction pacing, selection, placement, named objective selection, DETAILS/HIDE and waypoint readability. The whole sequence passed automation through real simulation commands; it was not a complete human tutorial playthrough. See [the tutorial control correction](TUTORIAL_CONTROLS_PASS.md).
- Physical pointer clicks remain unreliable through the Mac automation bridge. Keyboard navigation and engine command/lifecycle tests provide separate evidence; real player pointer interaction still needs acceptance.
- Validate on an actual iOS build: touch section buttons, safe areas, all later objective cards, dynamic recovery hints and completion preference persistence across relaunch. Compact Mac rendering does not prove iOS behavior.
- Assess whether later lessons should teach technology, counters and expansion. Those topics are documented in the field guide but are outside this first guided mission.
