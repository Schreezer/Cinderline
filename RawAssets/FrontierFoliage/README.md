# Frontier foliage source pack

Original Cinderline geometry and material. No downloaded assets or alpha textures.
The contact sheet is a Blender asset preview, not Unreal runtime evidence.

## Done

- [x] Author one irregular clump with ten broad bent blades and two muted ochre seed stalks.
- [x] Keep one opaque material section, 92 triangles, one LOD, bottom-center pivot, centimeter units.
- [x] Preserve the vertex channels through an FBX export/reimport: R = per-blade root-to-tip bending, G = olive/dry tone.
- [x] Save the source blend, exported FBX, manifest, and inspect a lit contact sheet.
- [x] Prepare an Unreal importer using the shared material graph helper and the verified legacy FBX route.
- [x] Add opaque two-sided shading, clamped per-pixel fog, and bounded simulation-clock wind to the material generator.
- [x] Compile both Python source files for syntax errors.

## Left before runtime delivery

- [ ] Run `scripts/unreal_frontier_foliage.py` in the root agent's serialized render-capable Unreal session.
- [ ] Confirm the import report succeeds, shader compiles, and both source/render vertex colors survive.
- [ ] Add sparse instances at cliff toes and patchy shoulders after the representative cliff/material scene is accepted.
- [ ] Supply `FogMask`, `CinderWorldSizeInverse`, and simulation seconds in `WindTime` to the runtime material instance.
- [ ] Allow 3 cm of extra horizontal instance bounds for wind and include that movement in fog-safe placement margins.
- [ ] Inspect gameplay camera scale and phone-sized composition; check fog, pause, and replay behavior.

## Assets and runtime contract

- Mesh: `/Game/Art/FrontierFoliage/Meshes/SM_CinderFrontier_Grass_A`
- Material: `/Game/Art/FrontierFoliage/Materials/M_CinderFrontierFoliage`
- Native bounds: approximately 35.17 × 40 × 34.57 cm; intended runtime footprint 15–50 cm.
- Wind uses no engine `Time` node. `WindTime=0` is the authored rest shape. Roots have R=0 and remain stationary. A sine difference, scaled by 1.4 and direction `(1, .35, 0)`, gives at most 2.967 cm horizontal displacement, multiplied by R squared.
- `FoliageRoot`, `FoliageTip`, and `FoliageDry` are muted olive/ochre vector parameters. There are no image textures except the fog mask.
- The importer requires `scripts/unreal_canyon_assets.py` for its scoped legacy FBX import settings and source/render vertex-color verifier. It does not execute that module's main entry point or touch canyon assets.
- To replace this pack's already-imported mesh, set `CINDER_REIMPORT_FRONTIER_FOLIAGE=1`. Unrelated assets at the destination are rejected.

## Rebuild

Run Blender only as an isolated background job. This generator clears its scene.

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 \
  --python scripts/create_frontier_foliage.py
```

Unreal import must run with rendering available, after the existing default fog texture has been imported. The success marker is `unreal-import.success`; detailed verification is written to `unreal-import-results.json` beside this file.
