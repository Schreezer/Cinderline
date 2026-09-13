# Visual target art pass

Current package note, 2026-09-12: navigation package `2bf6039a...` passed SDK 27 build, package, signing and native iOS-on-Mac checks, then installed successfully on AEON, an iPhone 17 Pro Max running iOS 27. The physical menu is visually verified and the app remains running. The reported disappearance most likely came from the agent forcing a post-install relaunch after the user had started playing; no new crash or memory-termination report was found. Physical routing, touch, boundary and sustained thermal tests remain pending. Historical `1870dcf9...` phone thermal evidence, `7c603b00...` native edge captures and `4ae4305a...` native thermal runs remain tied to their original packages. See [NAVIGATION_PASS.md](NAVIGATION_PASS.md).

12 September 2026. The visual kit, final material import, builds, automated checks and Mac still captures are complete. The final SDK 27 safe-area package also installed and rendered the corrected menu on AEON. The player confirmed that the revised spacing is perfect. Physical gameplay and thermal acceptance remain open.

## Delivered content

- Six replacement industrial buildings and an amber mineral cluster, authored in Blender. The buildings have recessed machinery, plate divisions, piping and faction panels. Source meshes retain the game's five material slots and bottom-centered placement contract.
- Eight replacement unit meshes and eighteen compatible motion parts. Existing locomotion, mining, hovering, turning and weapon articulation drive the new geometry.
- Three imported LODs for every building and unit mesh, with transition distances adjusted after checking the actual RTS camera. Vertex-baked ambient occlusion and a shared, restrained metal wear finish add surface shading without individual unique texture sets.
- Twelve fixed scenery batches: scanned boulders, low geological masses, fragments, foundation details, service roads, pipes and crates. Small props are culled by distance. Static scenery updates when exploration or base composition changes; identical transforms are not uploaded repeatedly.
- World-space flashes, projectiles, impacts, dust, healing and destruction follow the existing simulation events. Mining and construction effects follow real activity. Fixed instance limits bound the work, and pause/reset handling avoids replaying retained events.
- Warm sunlight, cool fixed sky fill, a lower camera angle, restrained bloom and a tested mobile HDR/FXAA/shadow configuration. The mobile HUD continues to leave the center and bottom gap available to the battlefield.

All maps and ordinary matches load this presentation kit. Simulation, navigation and terrain height rules remain unchanged. There is no Unreal Landscape implementation in this pass. The flat playing surface remains, and decorative cliffs do not create new traversable elevations.

## Asset sources and regeneration

Custom Blender source and exported FBX files are in `RawAssets/VisualTarget/`. Generators and importers are the `scripts/create_visual_target_*.py` and `scripts/unreal_visual_target_*.py` files. The aggregate import order is materials, shared metal finish, buildings, units/motion, then scenery. Import reports under `artifacts/visual-target/` validate dimensions, pivots, material slots, triangle counts and LODs.

Third-party assets are downloaded files from Poly Haven, with official metadata and hashes retained beside the files:

| Asset | Use | Credit and license |
| --- | --- | --- |
| [Gravel Floor 04](https://polyhaven.com/a/gravel_floor_04) | Packed-earth road color and retained earlier ground assets | Charlotte Baglioni, CC0; original provenance in [VISUAL_ASSET_SOURCES.md](VISUAL_ASSET_SOURCES.md) |
| [Dry Ground Rocks](https://polyhaven.com/a/dry_ground_rocks) | Coarse ground with matched 2K color, normal, roughness and ambient occlusion | Rob Tuytel, CC0; `RawAssets/VisualTarget/Materials/dry_ground_rocks/source.json` |
| [Blue Metal Plate](https://polyhaven.com/a/blue_metal_plate) | Subtle shared model wear, normal and roughness variation | Rob Tuytel, CC0; `RawAssets/VisualTarget/Models/SurfaceFinish/provenance.md` |
| [Namaqualand Boulders 01](https://polyhaven.com/a/namaqualand_boulders_01) | Six decimated, UV-preserving rock variants with 2K diffuse, normal and packed AO/roughness | Greg Zaal and Jenelle van Heerden, CC0; `RawAssets/VisualTarget/Scenery/External/namaqualand_boulders_01/` |

No paid assets were purchased. Runtime evidence uses real Unreal rendering of the imported assets.

## Validation and evidence

- Final material import: passed as `cinematic-visual-target-materials-v1.6.1`.
- Custom mesh import: all 33 meshes passed validation with three LODs each. This count covers seven building/mineral meshes, eight unit meshes and eighteen articulated motion parts.
- Scenery import: all twelve scenery batches passed validation.
- Mac Development build: passed with final integration and camera/HUD polish.
- Full SDK 27 physical-iOS cook and package: passed with 636 cooked packages.
- Portable Release simulation and network CTests: 2/2 passed. No simulation or protocol changes are part of this visual pass.
- Focused Unreal automation: 15/15 passed, including scenery bounds/observed state, world-effects fog/reset/budget, the hidden-target direction regression, gameplay integration, tutorials and compact HUD geometry.
- Native Designed for iPad on Mac HDR probe: rendered the world, shadows and compact touch HUD.
- Physical AEON launch: the packaged app installed and launched on an iPhone 17 Pro Max running iOS 27. The captured 2868x1320 screen and the player's observation confirm that the normal menu rendered. This proves package installation, engine startup and menu rendering. It does not prove gameplay or touch behavior.
- Final safe-area package: the strictly signed SDK 27 package with executable SHA-256 `8defa1a25ec194a3c4fc5b707278b7ddc99e503052c84ae6c9fbb18aa8985cfa` installed and launched on AEON. [`menu-full-bleed-launch.png`](../artifacts/visual-target/aeon/menu-full-bleed-launch.png) is a real 2868x1320 phone capture with full-bleed art and the left buttons inside the safe area. The player confirmed that the revised main-menu spacing is perfect.
- Final Mac stills: [`mac-base.png`](../artifacts/visual-target/runtime/mac-base.png) and [`mac-battle.png`](../artifacts/visual-target/runtime/mac-battle.png) are actual rendered captures after the final ground material work.
- Battle video: [`mac-battle-before-final-ground.mov`](../artifacts/visual-target/runtime/mac-battle-before-final-ground.mov) is six seconds of actual in-engine combat, recorded before the final anti-tiling and ambient-occlusion changes. The final recording attempt did not overwrite it, so no final-polish video is claimed.
- Final Mac timing sample: 180 rendered viewport samples over 12.171 seconds produced 14.79 FPS mean wall-clock throughput. The GPU history reported 16.492 ms mean, 14.247 ms median and 20.330 ms p95 across 182 samples. The run mixed foreground and 85 background frames, including the intentional 10 FPS background cap. It is neither a steady gameplay benchmark nor evidence of phone performance.

The `cinder.artpreview` development command creates a deterministic, unsaved local fixture with six buildings, mining workers, a mixed army and an incoming raid. It uses ordinary simulation orders and fog, refuses online matches, and is excluded from Shipping. Its screenshots are composed test scenes, not evidence of a complete human skirmish. Editor captures wait for pending mesh/shader compilation so gray fallback materials cannot masquerade as the final art.

## Remaining acceptance

The first AEON check found that the Dynamic Island covered the left menu buttons because the native view did not refresh its safe-area values. The first repair refreshed Unreal's global safe area and inset the whole menu, which the player rejected. The delivered repair reads UIKit insets on the main thread into a menu-only fraction cache. It does not broadcast an engine-global safe-area change. Menu drawing temporarily applies the corrected Canvas region after engine orientation when needed, then restores it, so the background remains full bleed and gameplay layout is unchanged.

The runtime log shows a 1912x880 Canvas retaining full-bleed background drawing after UIKit supplied insets of 124, 0, 124 and 40 render pixels. Those values are render pixels, not 186 native screen pixels; the phone screenshot uses a 1.5 scale. Mac and iOS builds, all 15 focused tests, the full package and strict signing passed. A raw 1320x2868 screenshot was captured after setting landscape right, but it has not received visual review. Both-orientation acceptance therefore remains open.

The player then reported that the phone was very hot. Further play was paused so AEON could cool before bounded performance and thermal diagnostics. Physical gameplay, multitouch, a complete match, sustained large-army performance and thermal behavior remain separate checkpoints. Native iOS-on-Mac validates the iOS rendering path on the Mac's GPU; it does not establish iPhone frame rates. The previously documented iOS Simulator engine-library gap is unchanged by this art pass.

The historical prepared thermal package is native SDK 27 iOS executable `4ae4305a...`. Its schema 3 build, package, strict signing, 16 game tests and five fresh-process iOS-on-Mac launches pass. Three menu launches held about 15 FPS with no SceneRender pass; nominal and forced-serious battle captures were inspected with full-resolution HUD geometry intact. Its evidence is preserved separately. Historical bounded map-edge replacement `7c603b00...` passed its 44-action SDK 27 build, packaging, post-build check, strict signing, sustained entitlement/profile and Game Mode checks. Three fresh native iOS-on-Mac edge captures at 2052x1536 were inspected with exact-black exterior probes and no new crash reports. Neither package was installed on AEON, and the native Mac runs do not prove physical gestures, gameplay or thermal acceptance. See [MAP_BORDER_PASS.md](MAP_BORDER_PASS.md) and [IOS_THERMAL_PASS.md](IOS_THERMAL_PASS.md).

The supplied reference is a cinematic concept. This pass does not claim parity with it. The achieved game art is the rendered result in the two final Mac captures: custom industrial buildings and units, scanned rock scenery, coarse PBR ground, restrained shared wear, gameplay-driven effects, warm directional light and a compact tactical HUD.

The current priority is the fresh physical launch failure, followed by iPhone routing, boundary, touch and sustained thermal checks while preserving the previously accepted menu geometry. Persistent public online service, a dedicated HUD redesign and an authored Unreal Landscape follow, in that order. Unreal Landscape has not been implemented.
