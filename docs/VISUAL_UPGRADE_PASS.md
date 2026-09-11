# Desktop visual upgrade

User priority, 2026-09-11: improve the menu and battlefield appearance now. Preserve RTS mechanics and defer iOS.

## Goal and status

Complete. Deliver sharper Retina output and typography, a stronger menu, directional shadows and ambient contact, PBR terrain/model materials, original rock formations and softer fog. Verify the real desktop and compact game, record rendering settings and frame cadence, and save a local checkpoint.

## Checklist

- [x] Capture current scenes and diagnose rendering, typography and assets.
- [x] Enable and verify a supported high-quality Mac rendering path.
- [x] Replace scaled font glyphs and improve menu/HUD hierarchy and contrast.
- [x] Add original rock formations and detailed PBR terrain/model materials.
- [x] Add selective shadows, balanced lighting, restrained bloom and softer fog.
- [x] Import and validate new assets and existing-asset refresh.
- [x] Build, run relevant checks and inspect before/after screenshots.
- [x] Record effective rendering settings and bounded frame samples.
- [x] Update the roadmap, preserve evidence and save this local checkpoint.

## Diagnosis

The earlier prototype used FXAA, disabled shadows/bloom/reflections, did not enable Retina game output, enlarged cached 14-point font glyphs, and used a color-only ground material. These choices made the game look much cheaper than the engine could render. Existing models already had beveled geometry and valid normals; their lighting and shared materials needed improvement.

Fresh before captures are `artifacts/visual-upgrade-before-menu.png`, `visual-upgrade-before-battlefield.png` and `visual-upgrade-before-pause.png`. They are from this pass, not older checkpoint captures. The weapon comparison uses the same explicit development fixture and camera focus at both resolutions. The previous viewport was 1316×740; the new viewport is 2560×1440, with roughly the same logical desktop size under Retina.

## Implemented

The visual-upgrade checkpoint rendered a 2560×1440 framebuffer with high-DPI awareness, TSR, 100% primary and secondary screen percentages, no dynamic resolution, 16× texture filtering, four cascaded shadow maps, screen-space reflections, ambient occlusion and restrained bloom. Motion blur, depth of field and color fringing remain disabled for tactical readability. The iOS profile remains separate.

The HUD rasterizes fonts at their final size and uses matching Slate measurement. The menu has a larger wordmark, quieter backdrop scrim, clearer map selection and a prominent Start action. The shared HUD and pause controls use the same typography and palette. Compact layout selection uses logical window height under Retina; mouse drag and picking tolerances scale with DPI while drawing and input remain in framebuffer coordinates.

Four original Blender basalt cliff variants replace rectangular obstacle blocks. Each uses one material and 1,128 triangles, exact normalized centimeter bounds, a bottom-centered pivot and authored UV0. Four unchanged 2K PBR maps from Poly Haven supply fine surface color, DirectX normals, roughness and AO. Ground albedo is desaturated and tinted slate, with restrained broad variation from the original Cinderline basalt artwork. Model and cliff detail follows UV0, avoiding texture swimming and projection stretching. Material generation is versioned as `desktop-pbr-v2.2.1`.

Visible models and cliffs cast shadows. Building pads ground the structures. A 256×256 transient fog texture feathers only inward into visible cells; hidden cells remain opaque. Enemy models and shadows still follow simulation visibility. Static terrain uploads and fog uploads are cached. The small aircraft bob is presentation only.

Asset origins and source hashes are in [VISUAL_ASSET_SOURCES.md](VISUAL_ASSET_SOURCES.md), [TERRAIN_ASSETS.md](TERRAIN_ASSETS.md) and [ART_ASSETS.md](ART_ASSETS.md). The downloaded texture is [Gravel Floor 04](https://polyhaven.com/a/gravel_floor_04), by Charlotte Baglioni, under [CC0](https://polyhaven.com/license). No paid assets were acquired.

## Rendered flow review

1. **Main menu: improved.** The wordmark and labels are crisp, the outpost artwork remains visible, and the filled Start control has clear priority. [Before](../artifacts/visual-upgrade-before-menu.png) / [after](../artifacts/visual-upgrade-after-menu.png). Enter opened the battlefield. Automated absolute mouse clicks still did not activate Start; this tool limitation remains separate from the user's earlier successful physical-mouse test.
2. **Battlefield: improved.** Models have grounding shadows and different paint/metal responses. Terrain has quieter, physically scaled grain and broad variation; fog no longer ends at a hard black line. The first pass looked too beige, so it was corrected before acceptance. [Matching combat fixture](../artifacts/visual-upgrade-after-battlefield.png), [cliffs and both palettes](../artifacts/visual-upgrade-after-cliffs.png), [normal match](../artifacts/visual-upgrade-after-normal-match.png). Small units still rely heavily on faction color at distant zoom. Rigged animation and richer terrain composition remain future art work.
3. **Compact layout: improved.** At 1334×750 pixels and DPI 2, the window is about 667×375 logical points. Buttons retain their intended size and text stays within controls. [Menu](../artifacts/visual-upgrade-after-compact-menu.png) / [battlefield](../artifacts/visual-upgrade-after-compact-battlefield.png). This is desktop layout evidence, not a physical-touch or safe-area test.
4. **Pause: verified.** Esc paused at 1:13 and Enter resumed. The heading and buttons use the new font and palette. [Pause capture](../artifacts/visual-upgrade-after-pause.png). Screen-reader behavior, color-vision accessibility and physical control acceptance are not established by these screenshots.

Every linked after capture was opened and inspected. The game was restored to a fresh main menu at desktop Retina resolution.

## Validation

- UE 5.8.2 Mac Development build passed.
- All 26 portable rule groups and all three CTests passed, including 3,000 native render frames. No simulation or native source changed in this pass; a new sanitizer run was not needed for those unchanged sources.
- All four strict Unreal integration paths passed with zero test errors or warnings: WorldLifecycle, EconomyAndProduction, AIKnowledgeAndPersistence and CombatFeedback. Final report run: `20260911T161206Z-13681`.
- Material import passed with four graphs, zero shader errors, exact graph readback, five model instances, four validated cliffs, four 2048×2048 PBR textures and an opaque default fog mask. Existing graph replacement was exercised, not just first creation.
- Runtime logged 15/15 models, zero model fallbacks, four loaded cliff kinds, two visible cliff instances in the staged scene and the 256-texture fog path.
- A fresh normal match started through Enter. Ore rose from 500 at 0:05 to 1,166 at 0:50, then 1,526 at the 1:13 pause. Arrow pan, Space home, pause and resume were exercised. No user save was written by the visual fixtures.
- No runtime Error, Fatal or ensure was found in the final play log. Existing engine/editor startup warnings remain, including Enhanced Input settings and editor widget registration warnings. Integration test warnings were zero; the entire engine log is not warning-free.

The manifest `artifacts/visual-upgrade-tested-source-sha256.txt` records 132 source, configuration and asset inputs. Machine-readable evidence is [visual-upgrade-verification.json](../artifacts/visual-upgrade-verification.json), [import results](../artifacts/visual-upgrade-import-results.json), [runtime evidence](../artifacts/visual-upgrade-runtime-evidence.txt) and [engine results](../artifacts/unreal-visual-upgrade-integration-results.json).

## Frame samples

Actual 2560×1440 output, DPI 2, TSR and 100% primary/secondary resolution. Each row includes 180 foreground frames after 30 warmup frames, with zero background frames.

| Scene | Mean | Median | p95 | FPS | Sample duration |
| --- | ---: | ---: | ---: | ---: | ---: |
| Frozen weapon fixture | 21.833 ms | 21.779 ms | 23.093 ms | 45.80 | 3.930 s |
| Live cliff/roster fixture | 21.445 ms | 21.443 ms | 22.398 ms | 46.63 | 3.860 s |
| Normal gathering skirmish | 21.016 ms | 20.911 ms | 22.091 ms | 47.58 | 3.783 s |

These measure rendered-viewport wall-clock cadence in the Development game on this M1 Max. They are not GPU timings, sustained late-game measurements, shipping-build benchmarks or iOS results. A stable 60 FPS target remains work to measure and optimize.

This pass uses the supported Mac SM5 renderer. Software Lumen is available on M1 according to [Epic's Mac requirements](https://dev.epicgames.com/documentation/unreal-engine/macos-development-requirements-for-unreal-engine?lang=fr); it was not enabled or benchmarked here. Nanite, virtual shadow maps and hardware ray tracing were not claimed as part of this pass. The measured native-resolution baseline should guide the next rendering choices.

## Issues found and repaired

- Protected Canvas text-size API and missing module links: use Slate's matching font-measure service and explicit Slate/ApplicationCore dependencies.
- One-pixel translucent strip overlap: remove overlap to avoid vertical menu seams.
- Retina compact breakpoint and mouse sensitivity: base the breakpoint and thresholds on logical display scale.
- Beige, uniform first terrain pass: use slate albedo and bounded macro variation.
- World-projected moving/vertical surface detail: use authored UV0 for models and cliffs.
- Unreal bulk material-expression deletion skipped nodes while mutating its own array: delete from a snapshot, require zero remaining nodes, and validate exact node identities before and after compilation. The failed script had editor exit code zero, so success-marker/report checks caught it correctly.

## Checkpoint report

CHECKPOINT: Desktop visual upgrade.

STATUS: Complete for the bounded desktop rendering and art pass; saved as a local checkpoint.

WORKING: Retina menu/HUD, normal skirmish, imported roster, shadows, PBR terrain/materials, authored cliffs and softened fog.

IMPLEMENTED: Rendering profile, font/menu changes, original terrain pack, repeatable material importer, cached fog/environment updates and development quality/visual inspection commands.

TESTED: Portable/native CTests, four Unreal integrations, asset/graph readback, desktop and compact screenshots, three frame samples and a normal gathering session.

ISSUES FOUND: Prototype quality settings, enlarged font glyphs, flat materials, hard fog, first-pass sandy tone, UV projection, Retina scaling and the engine's bulk graph deletion behavior.

FIXES MADE: Listed above; final source/assets match the recorded hashes.

CURRENT PLAYABLE EXPERIENCE: The game is open at a fresh desktop menu. Enter starts a normal skirmish with the upgraded presentation. Existing RTS rules are preserved.

NEXT CHECKPOINT: Unit locomotion and work animation, stronger environment composition, sustained late-game profiling toward 60 FPS, then a full human skirmish. Physical iOS work remains deferred.

## Remaining work

- [ ] Rigged locomotion, construction/mining motion and more expressive combat animation.
- [ ] Richer terrain composition and color-vision readability at distant zoom.
- [ ] Dimming remembered static cliffs/resources along with explored ground fog; current remembered geometry remains lit while hidden enemy geometry is omitted.
- [ ] Sustained late-game GPU/CPU profiling and a measured 60 FPS quality budget.
- [ ] Hands-on construction acceptance and a full human match/mix review.
- [ ] Later iOS packaging, device interaction, performance and thermals.

## Subsequent performance pass

The Mac anti-aliasing default was subsequently changed to native TAA after a matched Metal comparison. Historical TSR screenshots and timings above remain evidence of this visual checkpoint. See [METAL_PERFORMANCE_PASS.md](METAL_PERFORMANCE_PASS.md) for the current configuration and results.
