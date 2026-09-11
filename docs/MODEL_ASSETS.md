# Cairn Assembly model assets

Codex created fifteen original industrial sci-fi meshes with the repository's Blender Python generator on 11 September 2026. These use layered armor, split hulls, exposed cores, industrial supports, tracks, tools and distinct role silhouettes. The geometry is procedural Blender output; no third-party meshes or texture maps were downloaded.

Blender 5.2.1 LTS ran through its installed command-line executable. A Blender MCP connection was not exposed in this session. The source is editable and reproducible.

## Files

- `scripts/create_blender_assets.py` builds, exports, validates and renders the pack.
- `RawAssets/Models/Cinderline_Cairn_Assembly.blend` contains the 15 editable game meshes arranged in a grid at their real game dimensions.
- `RawAssets/Models/Cinderline_Cairn_Contact_Sheet.blend` contains the complete orthographic presentation scene, preview copies, labels, materials, lighting and camera.
- `RawAssets/Models/FBX/SM_*.fbx` contains one merged static mesh per file.
- `RawAssets/Models/manifest.json` records dimensions, triangle and vertex counts, simulation kind, material slot names, export settings and each FBX SHA-256.
- `artifacts/models/cairn-assembly-contact-sheet.png` is the actual 2400 × 1640 Blender Cycles render. Preview sizes vary to make small units readable; this image does not compare their physical scale.
- `artifacts/models/fbx-roundtrip-validation.json` records the delivery-file roundtrip checks.
- `artifacts/models/blender-build.log` records the generation run.

## Mesh contract

Local +X points forward and +Z points up. The pivot is at the bottom center of the mesh bounds. Mesh transforms are baked before FBX export. The maximum horizontal dimension equals twice the simulation definition radius. All models have packed UV coordinates and chamfered edges with weighted normals. Each mesh uses the same five material slots, in exactly this order, and every slot has actual geometry assigned:

| Index | Material | Runtime use |
| --- | --- | --- |
| 0 | HullDark | Dark navy industrial hull |
| 1 | HullLight | Lighter gray armor plates |
| 2 | Metal | Mechanical joints, tracks, barrels and supports |
| 3 | TeamPanel | Teal player plates, overridden for opponent team |
| 4 | CoreGlow | Teal emissive cores, overridden for opponent team |

The ore FBX retains this same contract. Its rendered preview overrides slots 3 and 4 with copper and amber. The Unreal adapter applies copper and amber overrides to neutral deposits, with warmer rock-face overrides for compact-screen readability.

The low Skim and Veil use 14 cm and 18 cm heights to keep their dart and swept-wing silhouettes. Drudge is 25 cm tall; Anvil and Cinderthrow are 38 cm tall. Forcing all small vehicles above 35 cm made them look like towers in the first render, so the revised pack preserves their roles.

| Mesh | Simulation kind | Dimensions X × Y × Z, cm | Triangles |
| --- | --- | --- | ---: |
| SM_Drudge | Worker | 32.0 × 19.907 × 25.0 | 1,720 |
| SM_Ember | Striker | 40.0 × 30.854 × 48.0 | 1,404 |
| SM_Needle | Lancer | 42.0 × 23.807 × 47.0 | 1,624 |
| SM_Skim | Scout | 36.0 × 17.294 × 14.0 | 1,180 |
| SM_Anvil | Bastion | 68.0 × 59.691 × 38.0 | 2,984 |
| SM_Cinderthrow | Mortar | 60.0 × 41.846 × 38.0 | 3,024 |
| SM_Mend | Mender | 38.0 × 34.244 × 39.0 | 2,532 |
| SM_Veil | Kite | 49.695 × 52.0 × 18.0 | 1,328 |
| SM_Anchor | Headquarters | 250.0 × 250.0 × 185.0 | 2,304 |
| SM_Siphon | Processor | 152.0 × 152.0 × 112.0 | 1,952 |
| SM_Kiln | Foundry | 180.0 × 180.0 × 120.0 | 2,348 |
| SM_Crucible | MotorPool | 208.0 × 208.0 × 155.0 | 2,000 |
| SM_Resonator | Laboratory | 170.0 × 170.0 × 160.0 | 2,512 |
| SM_Ward | Turret | 96.0 × 83.17 × 120.0 | 1,796 |
| SM_Ore | Resource | 88.729 × 90.0 × 65.0 | 1,108 |

## Export and validation

The Blender scene uses metric units with `scale_length = 0.01`, meaning one model coordinate is one centimeter. FBX exports use `global_scale = 1`, `apply_unit_scale = True`, `apply_scale_options = FBX_SCALE_UNITS`, `axis_forward = -Y`, `axis_up = Z`, `use_space_transform = True`, and `bake_space_transform = False`. The files contain triangulated static geometry, material assignments and normals. They contain no bones or animation.

The generator rejects triangles below Unreal's 0.00005 cm² import threshold and triangle corners within its 0.00002 cm componentwise welding threshold before export. It imports all 15 exported FBXs into a fresh part of its centimeter scene. It checks that each file produces one mesh, restores the recorded dimensions within 0.01 cm, keeps the XY center and bottom pivot within 0.01 cm, preserves all five slots in order, and restores every imported vertex within 0.001 cm of the authored geometry. The last check also catches an axis flip or rotation.

The final contact sheet was rendered and visually inspected after lowering the vehicle silhouettes. The rendered assets have separate worker tools, infantry weapons, scout pods, tank tracks, siege barrel, repair ring, aircraft wings, production bays, research ring and defense tower.

The Blender contact sheet is an authoring preview. A separate UE 5.8.2 bootstrap with Metal rendering enabled passed all 15 exact triangle counts, XYZ dimensions, pivots and material-slot checks on 2026-09-11. Five material palettes passed parameter read-back validation, and all 29,816 triangles were retained. Evidence is in `artifacts/unreal-asset-import-results.json`. The live starting Anchor, Drudges and ore deposits were then inspected at desktop and compact window sizes. Runtime diagnostics confirmed 15 loaded models, 29 batches and zero fallback entities. The full roster, enemy palette and device performance still need gameplay checks.

## Skim exhaust correction

The first Unreal import removed 64 degenerate triangles from Skim. Both rear emissive exhaust caps were 0.03 authoring units thick with a 0.015 bevel, so their chamfers met and collapsed. Blender source and FBX inspection found 56 zero-area triangles and eight below 0.000001 cm², exactly accounting for the import difference. No other model had triangles below 0.0001 cm².

The generator now uses a 0.006 bevel on those two caps and rejects geometry that Unreal would discard. The Skim FBX, editable packs, contact sheet and manifest were regenerated. The other 14 FBX files were retained unchanged with `--export-only Skim`. `artifacts/models/geometry-before-skim-fix.json` records the original problem; `artifacts/models/geometry-diagnostics.json` records the corrected source and delivery FBX checks. The corrected Skim FBX was reimported into Unreal and passed its exact 1,180-triangle check.

## Remaining work

These are static assets. Wheels, legs, barrels and production mechanisms are merged into each mesh and do not animate independently. No damage meshes, authored collision hulls, LOD chain, baked texture atlas or skeletal rig is included. The shared simulation owns movement and collision rules. The five material sections support team coloring, but their runtime draw cost still needs profiling with large armies. Triangle counts stay below 5,000 for units and below 10,000 for buildings; that alone does not prove a mobile frame budget.

To regenerate from the repository root:

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/create_blender_assets.py
```
