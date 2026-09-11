# Cinderline task board

Current order, confirmed by the user on 2026-09-11: **mechanics and logic → aesthetics and assets → full playtest → iOS**. Development checks continue as mechanics change. Blender was used to create the first model pack.

## NOW
- Verify the Drudge construction fix from the first hands-on session: physical travel, interrupted mining, pause/resume and return to ore. See [CONSTRUCTION_PASS.md](CONSTRUCTION_PASS.md).
- Continue through a full skirmish using [PLAYTEST.md](PLAYTEST.md).

## NEXT
- Continue repairing player-interaction findings and assess economy, scouting, counters, expansion and match pacing.
- Add explicit combat sound events and review the in-game mix.
- Inspect the full roster and enemy team palette through gameplay, then improve animation, fog edges and late-game rendering.

## LATER
- Measured human match pacing toward 20–30 minutes.
- iPhone/iPad packaging, signing, physical gestures, safe areas, performance and thermal tests.
- Asymmetric second faction, multiplayer, matchmaking, replay viewer and more content.

## DONE
- Fixed Drudge construction: workers travel, stop mining, build on-site, pause when interrupted and resume prior mining after completion. Added free reassignment, save migration, AI builder reservation and construction state UI. Fresh verification passed 19 rule groups, all 3 CTests, ASan/UBSan and both Unreal integration tests. See [CONSTRUCTION_PASS.md](CONSTRUCTION_PASS.md).
- Created the source structure, original Cinderline design, centralized definitions and command interface.
- Implemented a fixed-step C++ simulation with economy, queues, construction, research, movement, combat, fog, paid AI and saved matches.
- Implemented three map layouts and a first faction with eight units and six buildings.
- Final mechanics verification passed 14 simulation groups, all 3 CTests including 3,000 native render frames, ASan/UBSan and both Unreal integration tests. Exact source hashes and results are recorded.
- Repaired worker final approach, invalid placement, formation destinations, native HUD stability and exact construction health.
- Native UI checks exercised production, selection, building, cancellation, pause, save/load, army movement, natural defeat and rematch.
- UE 5.8.2 Mac Editor builds and launches the generated battlefield with working simulation and HUD.
- Actual Unreal checks covered menu start, ore gathering, arrow pan, Home, wheel zoom, pause/resume and compact menu/battlefield layouts. The user confirmed START SKIRMISH works with their own mouse.
- Unreal reached natural defeat through AI gameplay at 5:30 while the player base was left undefended. This was not a balanced human match test.
- Original generated basalt and menu artwork are imported, with prompts and provenance recorded.

- Fixed cover pursuit, displaced Hold orders and support following. AI now evaluates visible remaining ore, dispatches local defense workers, advances while scouting and attaches new/orphaned Menders to armed leaders. A second review found no remaining important issue in this bounded pass.
- Authored and roundtrip-validated 15 Blender models; all imported into Unreal with exact dimensions, slots and triangle counts. Fixed Skim exhaust geometry before import.
- Integrated shared materials, readable copper ore, ambient lighting, larger fonts and compact feedback bounds. Actual desktop and compact construction/battlefield captures passed.
- Imported eight sound cues and wired interface, order and production feedback. Live interface/rejected-order submissions reached Unreal audio; listening/mix review remains open.
- Built the presentation changes and passed both engine integration tests. [PRESENTATION_PASS.md](PRESENTATION_PASS.md) separates source, runtime and outstanding playtest evidence.

## BLOCKED
- Automated absolute mouse clicks do not update Unreal's cached mouse location on this Mac. Physical mouse start is user-confirmed; command integration tests and keyboard checks provide separate evidence.
- No current mechanics work is blocked by iOS. Its deferred preflight is in [IOS_READINESS.md](IOS_READINESS.md).

Unreal desktop launch and camera navigation are now demonstrated. Physical touch acceptance and the later gameplay checkpoints remain open; source implementation alone does not complete them.
