# Canyon terrain and portrait HUD

Authorized 2026-09-13 from the user's two selected recommendations and desert-canyon RTS reference.

## Scope

- Author real Unreal Landscapes for the three existing maps. Raise only existing blocked terrain, with flat guards around obstacles and level construction/travel areas.
- Add a coherent stratified sandstone cliff kit, gravel transitions, macro ground variation and blended building approaches.
- Bind terrain to existing fog, preserve strict black map borders, disable terrain collision/navigation and retain deterministic simulation rules.
- Replace the large default army grid with compact model portraits, counts, health and squad indicators. Keep individual selection, squads and information in explicit expanded views.
- Slim the resource/minimap presentation, retain touch-sized hit targets, preserve tutorial corrections and menu-only safe-area behavior.
- Keep instancing, mesh LODs, finite scenery counts and current mobile performance policy. Device heat/FPS acceptance requires later physical testing.

## Progress

- [x] Landscape authoring/runtime and geometry safeguards.
- [x] Canyon mesh/material imports with one-section LOD contracts.
- [x] Ground blending and fog-safe material integration.
- [x] Model portrait assets and compact HUD implementation.
- [x] Mac build, relevant simulation/UI/render tests and visual iteration.
- [x] Comparable Mac GPU samples at a fixed phone viewport.
- [x] SDK 27 iOS build/package and evidence record.
- [ ] Physical touch/performance acceptance on AEON, deferred.

## Delivered implementation

- Three authored Landscapes, one per canonical map, each use four components and a 127 by 127 heightfield. Only the matching canonical map can appear. Collision and navigation stay disabled, playable lanes and borders remain flat, and the active Landscape is fixed at LOD 0: 31,752 triangles.
- The ground material binds the live fog and terrain masks on desktop and ES3.1. It uses six pixel texture samples plus one vertex sample and flattens unknown relief; the rock material uses two samples. Missing textures, mismatched geometry or missing cooked mobile material data retain the flat-ground fallback.
- Nine unit portraits and eight canyon meshes were imported. Each canyon mesh has three one-section LODs. The compact portrait HUD, army ribbon, drawers and corrected tutorial controls are included.

All 26 Unreal tests pass. Nine Mac HUD captures, four Mac canyon captures and two tutorial captures were inspected and pass. Four exact packaged-binary iOS-on-Mac captures at 2052 by 1536 pass for the ribbon, army drawer, map 1 and fog boundary. They report ES3.1, Mobile HDR and one visible Landscape; protected saves remained unchanged and no new crash report appeared.

The signed SDK 27 IOS/arm64 package at `Saved/Packages/IOS/Cinderline.app` has executable SHA-256 `aac695ba1f7db76f990dbc8ebb8ae024317b639d3bc1c29af9bce0fb7eaafbed`. Its 77-action build, strict signature, development profile, IoStore and native platform checks pass.

At the matched Mac fixture, 120-frame GPU samples changed from mean 8.576 ms, p95 11.748 ms to mean 9.706 ms, p95 14.997 ms. The new run used MetalFX at 80% with no fallback. This is a modest measured increase on Mac and provides no phone heat result.

## Reproduction and limits

Run `scripts/unreal_canyon_import.py` in a render-capable `UnrealEditor-Cmd` process without `-NullRHI`; it imports the owned assets, authors the three Landscapes, waits for shader compilation, validates material resources and saves the owned map/material packages. Serialize that import, `./scripts/unreal.sh build -MaxParallelActions=2` and `./scripts/test-unreal.sh` rather than running Unreal build/test processes together.

Evidence is under `artifacts/canyon-hud`, including [automation results](../artifacts/canyon-hud/automation.json) and [final verification](../artifacts/canyon-hud/verification.json). Working logs and captures remain under `Saved/CanyonHUD`.

This completes the first canyon terrain and portrait-HUD production pass. It does not claim parity with the cinematic reference. Physical AEON installation of this package, native touch interaction and sustained phone thermal performance remain deferred. The installed AEON baseline remains MetalFX `594f5a62...` and was not changed during this pass.
