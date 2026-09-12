# Mobile HUD placement pass

Requested 2026-09-12: reclaim the battlefield covered by the mobile bottom controls.

## Implemented

- A single row of corner controls replaces the full-width 129-unit bottom panel. The central gap and space above accept world input.
- Left: Army, Select, Home. Right: Build/Attack/Train/Tech/Site for the selection, Stop/Queue/Drudges, More.
- Selection details occupy a small focus button above the left corner only when something is selected.
- Build, production, unit types and secondary orders appear in a right-hand drawer. Placement keeps Cancel under the command thumb.
- Phone/tablet scale and native safe insets share the drawing/hit-region geometry. Buttons remain 44 logical units high.
- Training begins as a 48-unit objective card; More expands the full guidance. Opening a command drawer collapses that guidance.
- Development-only scripted HUD fixtures/reporting cover idle, worker, build, placement, producer, queue, army, types and training without changing saves.

## Verification

- [x] Mac and SDK 27 iOS builds; freshly signed package and native wrapper match the tested executable.
- [x] All five Unreal tests passed: mobile layout geometry plus the four existing integration tests, including controller/lifecycle/gesture handling.
- [x] Actual native iOS-on-Mac rendering at 2052x1536: all nine fixture states passed viewport bounds, minimum target size, button-overlap and bottom-center pass-through checks. Keyboard pause/resume verified; no runtime assertion/fatal markers.
- [x] Mac renderer at 844x390 using the compact layout: all nine fixture states passed the same checks. Pure geometry also covers native cutouts/home insets at 667x375, 844x390, 956x440 and 1024x768, with doubled-resolution variants.
- [ ] Physical taps, drag/pinch and comfortable thumb reach on Aeon when available.

Automated fixtures are layout evidence, not physical gesture acceptance.

## Evidence and follow-up

- [Verification and source hashes](../artifacts/mobile-hud/verification.json), [Unreal test results](../artifacts/mobile-hud/automation.json).
- [Native iOS window](../artifacts/mobile-hud/native-ios/window.png), [native build drawer](../artifacts/mobile-hud/native-ios/build.png), [native queue](../artifacts/mobile-hud/native-ios/queue.png), [native geometry report](../artifacts/mobile-hud/native-ios/verification.json).
- [Phone preview](../artifacts/mobile-hud/phone/worker.png), [phone build drawer](../artifacts/mobile-hud/phone/build.png), [phone geometry report](../artifacts/mobile-hud/phone/verification.json).
- Mobile queues now show six items per full page, with a shorter drawer for a partial row. The 20-item production limit is unchanged.
- A focused second review confirmed drawer closing and queue alignment, and fixed the training-complete card overlapping selection details.
- Rendered checks used a 30 FPS cap. Both test apps were closed afterward. This pass does not claim new GPU or physical-phone performance measurements.
- On Aeon, check thumb reach, tap/drag/pinch, placement cancellation, repeated training, queue paging/cancellation and the More drawer in normal play. The earlier native UI automation click limitation remains; fixture setup is explicitly programmatic.
