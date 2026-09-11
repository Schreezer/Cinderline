# Cinderline task board

## NOW
- Checkpoint 0 acceptance: build and launch the Unreal project after UE 5.8.2 installation completes.
- Validate the generated map, materials, camera, touch input, command HUD and a real skirmish inside Unreal.

## NEXT
- Launch the project in Unreal on macOS; test touch camera and selection on iOS.
- Verify economy, construction, production, combat, technology and AI in the engine.

## LATER
- Tune measured match pacing toward 20–30 minutes.
- Full mobile gesture pass, physical device performance and thermal profiling.
- Asymmetric second faction, multiplayer, matchmaking, replay viewer and content.

## DONE
- Inspected repository: empty initial Git checkout.
- Confirmed Xcode 26.6 / Apple Clang 21 are available.
- Shared C++ simulation compiles with fixed-step economy, queues, construction, research, movement, combat, fog, paid AI and saved matches.
- Three map layouts and a first faction with eight units and six buildings are implemented.
- Standalone simulation rule tests and address/undefined-behavior sanitizer runs passed.
- 200-unit movement regression passes after repairing blocked formation goals and displacement after arrival.
- Three automated matches end through HQ destruction. The 20–30 minute balance target is still unproven.
- Native macOS runner builds and launches. Actual UI checks covered menu, worker production, group selection, build preview, occupied-site rejection, construction, queue cancellation, pause/manual, save/load and army movement.
- Native HUD crash traced to a nil text-attribute dictionary value. Font cache/fallback and safe attribute insertion passed a 6,000-frame saved-match rendering stress run.
- Live native playtest reached natural defeat, displayed results, and successfully rematched. Exact construction health and damage preservation now have regression coverage.
- Final frozen-source verification passed 12 simulation groups, all 3 CTests including 3,000 offscreen frames, and ASan/UBSan. Native subgroup filtering, viewport bounds, shortcut modifiers and priority damage alerts passed regression checks.
- Unreal game mode, camera, controller, battlefield rendering, fog/minimap, HUD, touch controls and asset bootstrap are implemented and independently source-reviewed.
- User started installing UE 5.8.2 in Epic Games Launcher.

## BLOCKED
- Unreal build and launch: UE 5.8.2 installation is in progress. The earlier UE 5.3 directory was empty. `scripts/unreal.sh doctor` has not yet confirmed an installed editor.
- iOS Unreal package and physical-device validation require a working engine and compatible toolchain.

Checkpoint 0 remains incomplete until the Unreal development target launches successfully.
