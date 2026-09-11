# Basalt terrain assets

This pack contains four original fractured basalt outcrops for Cinderline. Blender 5.2.1 LTS generated the geometry from the authored arrangements and recorded random seeds in `scripts/create_terrain_assets.py`. No third-party mesh or texture was used.

The outcrops combine offset basalt columns, irregular horizontal ledges, tilted fractured tops and low peripheral rubble. Their different mass arrangements provide a ridge, a diagonal outcrop, a split fault and a stepped mass. The models remain inside a common normalized box so presentation can use the existing simulation obstacle boundaries.

## Delivery files

- `RawAssets/Terrain/CinderBasaltTerrain.blend` contains the four source meshes and the shared Basalt material.
- `RawAssets/Terrain/CinderBasaltTerrain_ContactSheet.blend` contains the actual contact-sheet scene, lights, camera and labels.
- `RawAssets/Terrain/FBX/SM_BasaltCliff_A.fbx` through `SM_BasaltCliff_D.fbx` contain one triangulated static mesh each.
- `RawAssets/Terrain/manifest.json` records dimensions, pivot bounds, seeds, triangle counts, UV count and SHA-256 for each delivered FBX.
- `artifacts/terrain/basalt-terrain-contact-sheet.png` is the Blender Cycles render.
- `artifacts/terrain/fbx-roundtrip-validation.json` records the FBX roundtrip results.
- `artifacts/terrain/blender-build.log` records generation and rendering.

## Import contract

Every mesh is exactly 100 × 100 × 100 centimeters. Local X and Y span -50 to 50. Local Z spans 0 to 100. The origin is at the bottom center of the bounds, +X is forward, and +Z is up. All transforms are baked.

Each asset has 594 vertices and 1,128 triangles. The single material slot is named `Basalt`. Each mesh has one packed UV channel and hard, outward-facing authored rock normals. The optional `RockTint` corner-color layer contains restrained desaturated basalt color variation. It is not an ambient-occlusion bake or a normal map. The Blender material reads this color layer; Unreal supplies its own material for the slot.

| Mesh | Seed | Triangles |
| --- | ---: | ---: |
| SM_BasaltCliff_A | 74101 | 1,128 |
| SM_BasaltCliff_B | 74102 | 1,128 |
| SM_BasaltCliff_C | 74103 | 1,128 |
| SM_BasaltCliff_D | 74104 | 1,128 |

FBX uses centimeter units, `global_scale=1`, `apply_unit_scale=True`, `apply_scale_options=FBX_SCALE_UNITS`, `axis_forward=-Y`, `axis_up=Z`, `use_space_transform=True` and `bake_space_transform=False`, matching the existing machinery pipeline. There are no bones, animations, authored collision objects or LOD chains. Import with Nanite and generated collision disabled. The simulation owns movement and fire-line obstacles.

For one mesh fitted to an entire obstacle, use the obstacle center for XY position and scale X/Y by `2 * half_extent / 100`. Scale Z by the chosen presentation height divided by 100. A cluster of smaller, more uniformly scaled instances can preserve natural rock proportions on long obstacle rectangles. In that case, keep every transformed mesh bounding box within the obstacle rectangle. Do not add visible rubble outside the blocked area.

## Verification and limits

The generator rejects geometry above 2,000 triangles per variant or with any triangle below 0.00005 cm². It checks exact source bounds and finite packed UVs. It imports each delivery FBX back into Blender and verifies one mesh, exact triangle count, bounds and bottom pivot within 0.01 cm, one UV channel, the Basalt material slot and preserved RockTint data.

The final 1800 × 1800 Cycles contact sheet was visually inspected. All four outcrops show distinct broken column arrangements, stepped faces and contained rubble. The final labels have clear space between rows. All four FBX hashes match the manifest.

These checks establish the asset delivery contract. They do not prove Unreal import, fog integration, mobile performance or gameplay acceptance. Those checks belong to the runtime integration pass.

To regenerate from the repository root:

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/create_terrain_assets.py
```

The seeds reproduce the geometry. Exported file hashes identify this delivery; exporter metadata can change between runs or Blender versions.
