# Basalt terrain assets

This pack contains four original fractured basalt outcrops for Cinderline. Blender 5.2.1 LTS generated the geometry from the authored arrangements and recorded random seeds in `scripts/create_terrain_assets.py`. No third-party mesh or texture was used.

The outcrops combine offset fault faces, localized asymmetric ledges, narrow sloped crowns, visible fissures between major plates, and low peripheral scree. Their different mass arrangements provide a split crown, descending fan, blade pair and broken crown with lower shoulders. The models remain inside a common normalized box so presentation can use the existing simulation obstacle boundaries.

## Delivery files

- `RawAssets/Terrain/CinderBasaltTerrain.blend` contains the four source meshes and the shared Basalt material.
- `RawAssets/Terrain/CinderBasaltTerrain_ContactSheet.blend` contains the actual contact-sheet scene, lights, camera and labels.
- `RawAssets/Terrain/FBX/SM_BasaltCliff_A.fbx` through `SM_BasaltCliff_D.fbx` contain one triangulated static mesh each.
- `RawAssets/Terrain/manifest.json` records dimensions, pivot bounds, seeds, triangle counts, UV count and SHA-256 for each delivered FBX.
- `artifacts/terrain/basalt-terrain-contact-sheet.png` is the Blender Eevee render.
- `artifacts/terrain/fbx-roundtrip-validation.json` records the FBX roundtrip results.
- `artifacts/terrain/blender-build.log` records generation and rendering.
- `artifacts/animation-terrain/terrain-import.json` records the later Unreal import and asset inspection.
- `artifacts/animation-terrain/terrain-import-engine.log` records the UE 5.8.2 import, package saves and asset validation run.
- `artifacts/animation-terrain/after-terrain-first.png` is the inspected Mac Development `SF_METAL_SM5` runtime screenshot.

## Import contract

Every mesh is exactly 100 × 100 × 100 centimeters. Local X and Y span -50 to 50. Local Z spans 0 to 100. The origin is at the bottom center of the bounds, +X is forward, and +Z is up. All transforms are baked.

The single material slot is named `Basalt`. Each mesh has one packed UV channel and hard, outward-facing authored rock normals. The optional `RockTint` corner-color layer contains restrained desaturated basalt color variation. It is not an ambient-occlusion bake or a normal map. The Blender material reads this color layer; Unreal supplies its own material for the slot.

| Mesh | Seed | Vertices | Triangles |
| --- | ---: | ---: | ---: |
| SM_BasaltCliff_A | 74101 | 423 | 770 |
| SM_BasaltCliff_B | 74102 | 443 | 802 |
| SM_BasaltCliff_C | 74103 | 414 | 748 |
| SM_BasaltCliff_D | 74104 | 399 | 722 |

FBX uses centimeter units, `global_scale=1`, `apply_unit_scale=True`, `apply_scale_options=FBX_SCALE_UNITS`, `axis_forward=-Y`, `axis_up=Z`, `use_space_transform=True` and `bake_space_transform=False`, matching the existing machinery pipeline. There are no bones, animations, authored collision objects or LOD chains. Import with Nanite and generated collision disabled. The simulation owns movement and fire-line obstacles.

For one mesh fitted to an entire obstacle, use the obstacle center for XY position and scale X/Y by `2 * half_extent / 100`. Scale Z by the chosen presentation height divided by 100. A cluster of smaller, more uniformly scaled instances can preserve natural rock proportions on long obstacle rectangles. In that case, keep every transformed mesh bounding box within the obstacle rectangle. Do not add visible rubble outside the blocked area.

## Verification and limits

The generator rejects geometry above 2,000 triangles per variant, geometry with open or non-manifold edges, or any triangle below 0.00005 cm². It checks exact source bounds and finite packed UVs. It imports each delivery FBX back into Blender and verifies one mesh, exact triangle count, bounds and bottom pivot within 0.01 cm, one UV channel, the Basalt material slot and preserved RockTint data.

The final 1800 × 1800 Eevee contact sheet was visually inspected. All four outcrops show distinct fractured crowns, long fault faces, asymmetric ledges, readable cluster fissures and contained low scree. All four FBX hashes match the manifest.

These Blender source, export, roundtrip and visual checks establish the asset delivery contract. A subsequent unattended UE 5.8.2 import matched all four FBX SHA-256 values, triangle counts and material-slot names; measured each imported static mesh at 100 × 100 × 100 cm within floating-point tolerance; assigned `/Game/Art/Materials/M_CinderBasaltV2`; saved the four `/Game/Art/Environment/SM_BasaltCliff_*` packages; and completed Unreal asset validation. The engine log records `CINDERLINE_TERRAIN_SURFACE_OK` followed by a clean editor shutdown.

The inspected `after-terrain-first.png` Mac Development `SF_METAL_SM5` runtime screenshot shows the revised fractured crowns, split clusters and contained scree in the battlefield composition. This is visual runtime evidence on Mac. It does not establish iOS behavior, collision acceptance, gameplay acceptance or performance.

To regenerate from the repository root:

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 \
  --python scripts/create_terrain_assets.py
```

Add `-- --no-render` after the script path to export and run the Blender roundtrip checks without rebuilding the contact sheet. The seeds reproduce the geometry. Exported file hashes identify this delivery; exporter metadata can change between runs or Blender versions.
