# Terrain geometry pass

The four `SM_BasaltCliff_A` through `SM_BasaltCliff_D` source meshes now use long,
offset fault faces, localized ledges, narrow fractured crowns, visible gaps between
major plates, and purpose-built low scree wedges. The scattered pieces remain
inside each mesh's normalized 100 x 100 cm blocked footprint.

Run the complete export, FBX roundtrip validation, source `.blend` save, and Eevee
contact-sheet build with:

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 \
  --python scripts/create_terrain_assets.py
```

Use `-- --no-render` after the script path for export and validation without the
presentation scene or contact-sheet render.

| Asset | Vertices | Triangles | Minimum triangle area |
| --- | ---: | ---: | ---: |
| `SM_BasaltCliff_A` | 423 | 770 | 1.9425 cm2 |
| `SM_BasaltCliff_B` | 443 | 802 | 2.2763 cm2 |
| `SM_BasaltCliff_C` | 414 | 748 | 2.2672 cm2 |
| `SM_BasaltCliff_D` | 399 | 722 | 1.5006 cm2 |

`RawAssets/Terrain/manifest.json` and
`artifacts/terrain/fbx-roundtrip-validation.json` are the canonical machine-readable
records for the Blender generation pass. At generation time, this pass created
source and FBX assets only and did not import into or launch Unreal Engine.

## Subsequent Unreal evidence

A later unattended UE 5.8.2 integration imported and saved all four static meshes.
`artifacts/animation-terrain/terrain-import.json` records `success: true`, matching
source SHA-256 values and triangle counts, one `Basalt` slot per mesh, dimensions of
100 x 100 x 100 cm within floating-point tolerance, and the assigned
`/Game/Art/Materials/M_CinderBasaltV2` material. The accompanying
`artifacts/animation-terrain/terrain-import-engine.log` records each FBX payload,
package save, asset validation, `CINDERLINE_TERRAIN_SURFACE_OK`, and clean editor
shutdown.

The inspected Mac Development `SF_METAL_SM5` screenshot at
`artifacts/animation-terrain/after-terrain-first.png` shows the revised crowns,
cluster gaps and low scree in the battlefield composition. This proves the assets
were visually present in that Mac runtime capture. It does not prove iOS behavior,
collision acceptance, gameplay acceptance or performance.
