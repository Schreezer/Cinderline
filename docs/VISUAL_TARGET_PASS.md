# Visual target art pass

12 September 2026. Final rendered acceptance is in progress. This record will be updated with the finished package and captures before delivery.

## Delivered content

- Six replacement industrial buildings and an amber mineral cluster, authored in Blender. The buildings have recessed machinery, plate divisions, piping and faction panels. Source meshes retain the game's five material slots and bottom-centered placement contract.
- Eight replacement unit meshes and eighteen compatible motion parts. Existing locomotion, mining, hovering, turning and weapon articulation drive the new geometry.
- Three imported LODs for every building and unit mesh, with transition distances adjusted after checking the actual RTS camera. Vertex-baked ambient occlusion and a shared, restrained metal wear finish add surface shading without individual unique texture sets.
- Twelve fixed scenery batches: scanned boulders, low geological masses, fragments, foundation details, service roads, pipes and crates. Small props are culled by distance. Static scenery updates when exploration or base composition changes; identical transforms are not uploaded repeatedly.
- World-space flashes, projectiles, impacts, dust, healing and destruction follow the existing simulation events. Mining and construction effects follow real activity. Fixed instance limits bound the work, and pause/reset handling avoids replaying retained events.
- Warm sunlight, cool fixed sky fill, a lower camera angle, restrained bloom and a tested mobile HDR/FXAA/shadow configuration. The mobile HUD continues to leave the center and bottom gap available to the battlefield.

All maps and ordinary matches load this presentation kit. Simulation, navigation and terrain height rules remain unchanged; decorative cliffs do not create new traversable elevations.

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

- Mac Development build: passed with final integration and camera/HUD polish.
- SDK 27 physical-iOS Development build: passed with final integration and camera/HUD polish. Asset cook pending.
- Portable Release simulation and network CTests: 2/2 passed. No simulation or protocol changes are part of this visual pass.
- Focused Unreal automation: 15/15 passed, including scenery bounds/observed state, world-effects fog/reset/budget, the hidden-target direction regression, gameplay integration, tutorials and compact HUD geometry.
- Native Designed for iPad on Mac HDR probe: rendered the world, shadows and compact touch HUD. Final new-roster package verification pending.
- Final base/battle captures, actual window recording and frame-time sample: pending.

The `cinder.artpreview` development command creates a deterministic, unsaved local fixture with six buildings, mining workers, a mixed army and an incoming raid. It uses ordinary simulation orders and fog, refuses online matches, and is excluded from Shipping. Its screenshots are composed test scenes, not evidence of a complete human skirmish. Editor captures wait for pending mesh/shader compilation so gray fallback materials cannot masquerade as the final art.

## Remaining acceptance

Physical Aeon gameplay, multitouch, sustained large-army performance and thermal behavior remain separate checkpoints. Native iOS-on-Mac validates the iOS rendering path on the Mac's GPU; it does not establish iPhone frame rates. The previously documented iOS Simulator engine-library gap is unchanged by this art pass.

The supplied reference is a cinematic concept. Matching its scale of authored landscape, broad armies, sky traffic and dense set dressing would require more level content and continued art production. This pass replaces the playable game's asset and effects foundation; screenshots below will record its actual achieved appearance.
