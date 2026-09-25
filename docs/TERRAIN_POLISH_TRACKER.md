# Terrain visual overhaul tracker

Updated: 2026-09-22. Current work is the authored-map redesign below. The earlier terrain-art pass is retained here as history, following the rejected first pass in `SC2_TERRAIN_PASS.md`.

## Target and acceptance

Polished stylized science-fiction frontier terrain. Use StarCraft II for battlefield readability and composed geology, and Clash of Clans for clear silhouettes at phone size. Original assets, consistent materials, strong shape hierarchy. Technical success alone does not establish comparable art quality.

The representative scene is map 0's western expansion: base entrance, fractured cliff, choke and mineral deposit together at the normal gameplay camera. Judge actual Unreal frames with units present. First finish that area, then check the other maps and fallback configurations.

## Status

| Stage | State | Done | Left / acceptance evidence |
| --- | --- | --- | --- |
| 1. Representative gameplay scene | Done | Actual base, choke, ore and units; ordinary fog; fixed gameplay camera | Baseline retained for comparison |
| 2. Cliff and rock geometry | Complete; desktop verified | Original fractured Blender kit; exact LOD/color/bounds import checks; full mesh cliff replaces the western mesa | Applied across all canonical layouts; foundation tiles, broad centers and narrow ridges reviewed |
| 3. Materials and lighting | Complete; desktop verified | Earthy ground, controlled cliff-foot texture, worn route edges, dark rock palette, restrained lighting/grade | Desktop maps and phone-aspect captures reviewed; physical launch waits for unlock |
| 4. Environmental composition | Complete; desktop verified | Grouped grass and shallow talus; clear paths, pads and ore approaches; imported foliage tests pass | Broad/narrow forms and phone framing reviewed; physical launch waits for unlock |
| 5. Motion and feedback | Complete; desktop verified | Walking/mining cadence fix; simulation-clock grass wind; soft-edged exhaust; verified 90-frame live sequence | Final 90-frame phone-aspect clip verified; physical cadence waits for unlock |
| 6. Map coverage and validation | Desktop complete; phone in progress | 16 targeted tests pass; two unrelated platform-discovery warnings diagnosed; fallback capture fixtures prepared | Build/rebake, seven map fixtures, 16 clean tests, fresh iOS package/install done; device capture/profile wait for phone unlock |


## Next pass: authored map design

The user accepted the visual improvement and supplied two StarCraft reference images on 2026-09-22. The next gap is map composition: base districts, expansion pockets, purposeful routes, real terrace/ramp connectivity and surrounding scenery.

Detailed scope and evidence are in [MAP_REDESIGN_PLAN.md](MAP_REDESIGN_PLAN.md). **Shattered Rift / Standard / two players** now has the first authored layout: raised mains, usable ramps, six resource sites, flank routes, paving, retaining walls and expansion cliff backdrops. M0–M4 are complete for that configuration. M5 has its first composition pass; richer landmarks and less repetitive surfaces remain. M6 desktop checks are complete (30 portable suites, 37 real-worker checks, 63 strict Unreal checks, rendered fallback and phone-proportion motion); physical verification remains pending. M7 other maps/sizes/four-player authored layouts remains open. Aeon is currently disconnected; the prior installed art-pass build is not proof for this map redesign.

## Previous terrain-art pass review artifacts

- Finished sector: `artifacts/terrain-overhaul/final-fog/run-20260922T112410Z-lygAsO/sector.png`
- Fog-edge check: `artifacts/terrain-overhaul/final-fog/run-20260922T112410Z-lygAsO/fog.png`
- All-map and fallback coverage: `artifacts/terrain-overhaul/final-maps/run-20260922T111808Z-vu9g5X/`
- Finished phone-aspect motion: `artifacts/terrain-overhaul/final-phone-motion/run-20260922T112600Z-WkCCVh/motion.mp4`
- Clean automated checks: `artifacts/terrain-overhaul/final-automation/index.json`
- Signed installed iOS identity: `artifacts/terrain-overhaul/aeon/verification.json`
- Source Blender files: `RawAssets/Canyon/CinderCanyonAssets.blend` and `RawAssets/FrontierFoliage/CinderFrontierFoliage.blend`.

## Current authored-map review artifacts

- Main exit: `artifacts/map-redesign/verified-visual/run-20260922T152512Z-IsB3Ld/sector.png`
- Layout overview: `artifacts/map-redesign/final-visual/run-20260922T145641Z-wDVslt/map0.png`
- Phone-proportion Home framing: `artifacts/map-redesign/verified-phone/run-20260922T153007Z-OpVTyQ/sectorbase.png`
- Final live motion: `artifacts/map-redesign/final-motion/run-20260922T154951Z-7ouKu9/motion.mp4`
- Strict checks: `artifacts/map-redesign/unreal-verified/index.json`
- Full completed/remaining list and compatibility notes: [MAP_REDESIGN_PLAN.md](MAP_REDESIGN_PLAN.md)

## Remaining hardware check

1. Connect Aeon and leave it unlocked during verification. Latest discovery reports it disconnected.
2. Verify and install the newly cooked archive for this authored-map pass. The older installed build `56702186.0.36` belongs to the previous art pass.
3. Launch the verified build with `-- com.cinderline.game -unattended '-ExecCmds=cinder.quality,cinder.canyonpreview sector'` after `devicectl device process launch` options. The `--` delimiter prevents Unreal arguments from being parsed as devicectl flags.
4. Verify fresh readiness logs and capture the device screen.
5. Run a separate foreground, non-unattended performance sample and stop only the verification-owned process. Record hardware visuals and frame timing separately from Mac rendering and fixed-step video.

## Initial critique (before this pass)

- Cliffs are rectangular layer cakes. Rounded uniform rims, horizontal bands and flat tops dominate the battlefield.
- Cap rocks resemble dark tokens with white lids. Their geology does not match the terrain.
- Ground is uniformly busy gravel. Broad pale paths look airbrushed and lack edge structure.
- Cliff scale overwhelms units. Large empty tops and repeated forms weaken composition.
- Pale material highlights substitute for real shape and lighting. Terrain contrast competes with gameplay subjects.
- The whole scene lacks a shared art language and convincing local detail.

## Rules for this pass

- Work through the stages in order, checking the representative scene before extending changes.
- Preserve existing dirty work. No broad resets, staging or asset deletion.
- Serialize Unreal build/import/capture/test processes.
- Keep visual boundaries consistent with simulation obstacles; coordinate gameplay geometry if boundaries change.
- Preserve fog privacy, deterministic placement, shared materials, instancing and mobile-oriented budgets.
- Record implemented, visually verified and device verified separately. No claim of SC2/Clash parity without evidence.

## Work log

- 2026-09-22: Started second pass. Reviewed the rejected map 0 frame. Selected real western expansion for the representative scene. Blender MCP is unavailable because the addon is not connected; the installed Blender executable can run the reproducible asset pipeline in an isolated background process.

## Evidence

- Rejected baseline: `artifacts/sc2-terrain/map0.png`, `map1.png`, `map2.png`, `delivery-base/mac-base.png`, `delivery-base/mac-battle.png`.
- New evidence will be stored under `artifacts/terrain-overhaul/`.

- Stage 1 baseline: `artifacts/terrain-overhaul/sector-before/run-20260922T095851Z-yiukCY/sector.png`. Build succeeded; ordinary vision preserves 3171 unknown fog cells.
- Stage 2 asset source: `RawAssets/Canyon/CinderCanyonAssets.blend`; contact sheet: `RawAssets/Canyon/contact-sheet.png`. Runtime import and cliff integration pending.

- Stage 2 integration: low 40 cm terrain support and full 220–340 cm mesh body implemented for the western cliff only. Aircraft clearance now accounts for mesh bodies. C++ build passes; authored map rebake and visual check pending.
- Import validation caught UE Interchange dropping authored vertex colors; importer fix in progress. Blender source and FBX roundtrip colors verified.

- Stage 2 import completed: 12 assets, every source/render LOD retains vertex colors; 3 Landscapes rebuilt, 12 components validated. Evidence: `stage2-import.json`.
- First shape review: `stage2-shape/run-20260922T102202Z-kos8BM/sector.png`. Broken crest and recessed faces are better; lit tops remain too pale and low base reads as a separate slab. Stage 3 addresses those before geometry is considered visually finished.
- Correctness follow-up: partial cliff reveal now keys off explored footprint; unknown vertices flatten under fog. Aircraft picking, labels, hulls and effects share their visible altitude and actual bound terrain mode. Runtime tests pending.

- Stage 3 material import and C++ build pass. Explicit rock LOD screen thresholds retain sculpted detail; palette, lighting and ground pass captured next. No device test yet.
- Device discovery: paired physical iPhone 17 Pro Max is available. An existing installed build is not evidence for this pass; fresh build/install/measurement still required.

- First focused runtime test run: all 15 selected tests report success in `stage3-automation/index.json`, including fog privacy, partial reveal, aircraft picking/effects and 120Hz/20Hz motion cadence. Later foliage/all-map changes still require fresh testing.
- Stage 3 first review rejected the overly smooth grey floor. Revision v4.1 restores controlled mid-scale soil detail and olive/brown patch contrast; runtime review pending.

- Stage 3 v4.1 imported and Landscape materials finalized successfully (`stage3-earth-import.log`). The report validator confirms all 15 expected tests completed with no errors, warnings, or unfinished tests.
- Stage 4 integration is now in source: optional instanced grass, grouped planting away from travel/production/mineral approaches, a small talus population, and simulation-clock wind. New asset import and fresh runtime checks remain pending.

- Stage 3 v4.1 runtime review: `stage3-earth/run-20260922T105327Z-ve8X56/sector.png`. Olive/brown soil and cliff-foot detail restored, pale rock caps removed; gameplay objects remain legible. The continuous foundation silhouette remains a Stage 6 composition refinement.

- Stage 4 imported and visually checked: `stage4-import.json` and `stage4-foliage/run-20260922T105543Z-4rG3jq/sector.png`. Sparse grouped vegetation and low rubble frame the cliff; no plants crowd the mineral line, building pads or open route. Grass is one 92-triangle opaque mesh, maximum 128 instances, one added batch. Imported-asset runtime regression still pending.

- Stage 4 regression: all 16 tests returned Success, including the imported `SceneryFoliage` placement/wind/privacy branches. Strict validation rejected two asynchronous engine warnings in WorldLifecycle: old `idevice_id` tool cannot execute on this CPU. Investigating supported device-discovery suppression for final Mac automation; not treating this as clean proof yet.
- Stage 5 live capture validates all 90 PNG frames and a 3-second 30 FPS MP4 (`stage5-motion/run-20260922T105636Z-DEqT5w/motion.mp4`). Mining, patrol and paid factory production advance correctly. Visual review caught rectangular factory dust particles; radial opacity fix pending before acceptance.

- Stage 5 repaired dust-only material imported (`dust-import.json`). A second complete 90-frame sequence, `stage5-soft-dust/run-20260922T110325Z-3uAJUd/motion.mp4`, removes sharp particle corners while retaining ordinary mining/production/patrol activity. Fixed-step capture validates animation sequence, not real-time performance.
- Stage 6 begun: extend low terrain support and composed mesh bodies across six canonical map/player layouts and all three sizes; replace continuous foundations with smaller overlapping pieces; preserve custom geometry fallback. Added Short 2-player map 2 and Long 4-player map 1 preview coverage in progress.
- Mac automation warning diagnosis: bundled legacy `idevice_id` is x86_64; its 10-second discovery poll happened to overlap WorldLifecycle in Stage 4. Final Mac automation will use supported host/target platform discovery flags, without suppressing diagnostics.

- Stage 6 build succeeded in 68 seconds; three profile-6 Landscapes rebaked and all 12 component materials finalized (`stage6-landscapes.json`).
- Final targeted automation passed strict validation: 16/16 expected tests, zero errors, warnings or unfinished tests (`final-automation/index.json`). This includes all 18 map/player/size configurations, actual transformed cliff bounds, custom fallbacks, fog, grass placement/wind, aircraft effects/picking and motion cadence. The Mac-only discovery configuration removed the unrelated device-poller warnings without filtering logs.

- Final map coverage: all seven fixtures captured successfully at 1440x900 in `final-maps/run-20260922T111808Z-vu9g5X/`: sector, maps 0/1/2, fog, Short map 2 and Long four-player map 1. Broad two-row outcrops and narrow chains reviewed; all capture readiness checks passed.
- Fog review found coplanar collapsed rock/ground at Z=-1. Rock-only unknown WPO now sinks to Z=-32; visible/explored positions and sample counts unchanged. Imported/verified in `fog-depth-import.json`; clean sector and fog images confirmed in `final-fog/run-20260922T112410Z-lygAsO/`.

- Final phone-aspect motion sequence passed at 1170x540: `final-phone-motion/run-20260922T112600Z-WkCCVh/motion.mp4` (90 frames, 3 seconds, 30 FPS). Framing and HUD reviewed; this is Mac rendering at phone proportions, not hardware performance evidence. Fresh physical iOS build started.

- Independent final image review found no rendering/readability blocker in the final sector, broad map 1, Short map 2 and phone-aspect frames. Remaining quality limits: broad smooth ground, repeated faceted rock forms and dark side faces still make this a modest stylized art set; SC2/Clash production parity is not established.

- Physical iOS build passed (98 actions, 262 seconds), cook/package/sign/archive passed (141 seconds). Fresh build `56702186.0.36` installed on Aeon / iPhone 17 Pro Max / iOS 27.2; package identity and installed build match (`aeon/verification.json`). Launch is currently blocked by iOS device lock (FBSOpenApplicationErrorDomain 7). Requested unlock; no physical visual or performance claim yet.

- Optional desktop performance harness run is **not accepted performance proof**: it recorded 600 rendered frames but all were background frames, and its CSV validator rejected missing model/submission markers. No desktop FPS claim is made from this run. Existing script was left unchanged; the physical sample remains the useful outstanding performance check.
- Current stop condition: all implementation and desktop visual/correctness work is complete; only the physical capture/performance step awaits the requested phone unlock. No verification-owned game process is running on the phone because launch was denied by iOS.

- Authored map pass: first-map implementation and desktop verification are recorded above and in MAP_REDESIGN_PLAN.md. Physical ARM64 compilation and final cook/sign/archive passed; fresh signature/platform/UUID proof is in `artifacts/map-redesign/package-verification.json`. Further environmental composition, additional authored maps/variants, device proof and coordinated online deployment remain open.

- Ramp visibility fix verified: the adjacent Anchor now reveals the ramp within its existing range; actual upper-terrace coverage retains its height requirement. All 13 formerly darkened patrol cells remain visible. New regressions fail before the fix and pass after it; 30 portable suites and 37 real-worker Node cases have passing results (six initial contention timeouts passed unchanged isolated retries), and 63 selected Unreal tests are strictly clean. The new 90-frame phone-proportion capture was independently reviewed from its encoded opening/middle/end frames and shows the ramp staying lit. Local physical-iOS compilation, cook/sign/archive and fresh identity/signature/platform verification passed for build56702186.0.38. Aeon is still disconnected, so installation and hardware proof remain pending. Current evidence and remaining mixed-crest limitation are in the final section of MAP_REDESIGN_PLAN.md and `artifacts/ramp-visibility-fix/verification.json`.
