# First-faction presentation integration

CHECKPOINT: Original models, HUD readability and sound feedback

STATUS: Desktop integration checked. The next step is a hands-on skirmish; iOS remains deferred.

WORKING:

- The Unreal menu opens the battlefield. Workers harvest through the ordinary simulation.
- The renderer loads all 15 models into 29 instanced batches. The inspected starting base rendered 10 entities with zero primitive fallbacks.
- The compact construction sheet remains readable while the opening message is visible.

IMPLEMENTED:

- Fifteen original Blender models with centimeter dimensions, bottom-centered pivots, five material slots and 29,816 triangles total. Source files, editable packs and validation scripts are included.
- Shared teal/red team materials, copper ore surfaces and a referenced Unreal daylight cubemap for ambient lighting.
- Retained 14-point runtime fonts, larger menu headings, compact minimap at top right, bounded feedback messages and clear placement-cancel space.
- Eight procedural sound assets and a cached, throttled audio subsystem. Interface, selection, accepted/rejected orders and player production completions have hooks.

TESTED:

- UE 5.8.2 Mac Development Editor build passed on Xcode 27 with the installed Metal compiler.
- A graphics-enabled bootstrap imported all 15 models and all eight SoundWaves. Exact bounds, pivots, triangle counts, material slots, material parameter values and sound metadata passed.
- Both engine integration tests passed after the model, audio, ambient-light and ore-palette changes. They exercise real world/controller/actor transitions and ordinary paid economy/production commands with NullRHI. The subsequent compact feedback fix was rebuilt and visually checked in the actual game.
- Actual desktop and compact views were inspected at 1280×720, 667×375 and 844×390. The 667×375 construction capture includes the opening feedback message above the sheet without overlap.
- Live diagnostics recorded two audio submissions, interface and rejected order, with no missing assets or unavailable audio device. This does not prove listening quality or validate the complete mix.
- The earlier 14 portable rule groups, all three CTests and sanitizer results still match the six unchanged portable-source hashes. They were not rerun for presentation-only changes.
- A bounded source review cleared import behavior, material/fog safety, resource overrides, font ownership and the compact feedback fix.

ISSUES FOUND:

- The active Xcode installation lacked Metal, then retained an SDK-specific lookup pointing at its missing-tool stub.
- UE 5.8 material setters applied values but returned false unconditionally.
- Two Skim exhaust bevels collapsed 64 triangles.
- Empty-environment skylight capture left shaded model faces nearly black, and ore blended into the ground.
- Wrapped compact feedback overlapped sheet headers and placement controls.

FIXES MADE:

- Installed Apple's Metal Toolchain and refreshed the macOS xcrun cache. The project doctor now verifies the exact macOS Metal lookup.
- Replaced material-setter Boolean checks with parameter-name and value read-back validation. Model errors are collected across the pack before bootstrap fails.
- Reduced the two exhaust bevels and added source geometry rejection checks. The corrected Skim retains all 1,180 triangles in Unreal.
- Added a hard-referenced daylight cubemap, modest ambient fill and warm ore material overrides.
- Reserved a single feedback row above sheets and separate horizontal space beside placement Cancel. Conflicting hints, minimap and metrics are suppressed in those states.

CURRENT PLAYABLE EXPERIENCE:

- A local single-faction skirmish with eight unit types, six structures, three maps, paid offline AI, fog, construction, production, research, saves and results.
- Static models and basic sound feedback are integrated. Animation, combat sound events, complete audio mixing, full-roster visual checks and late-game rendering performance remain open.
- Synthetic absolute mouse targeting is still unreliable on this Mac. The user previously confirmed physical START SKIRMISH; the new build/produce player journey is now requested in the open game.
- The three scripted pacing trials ended at 13:09, 14:05 and 14:25. The 20–30 minute human-match target remains unproven. There is no new full-match, physical touch, packaged iOS or multiplayer claim.

NEXT CHECKPOINT:

- Follow [PLAYTEST.md](PLAYTEST.md), repair interaction findings, then evaluate scouting, expansion, composition and natural match pacing before the iOS stage.

Evidence: [actual battlefield](../artifacts/unreal-battlefield.png), [compact construction](../artifacts/unreal-compact-construction.png), [asset import results](../artifacts/unreal-asset-import-results.json), [runtime diagnostics](../artifacts/unreal-presentation-results.json), [engine integration](../artifacts/unreal-integration-results.json), and [source/content hashes](../artifacts/unreal-tested-source-sha256.txt).
