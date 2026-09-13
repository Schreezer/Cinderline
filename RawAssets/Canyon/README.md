# Cinderline Canyon Kit

This is an original procedural sandstone kit for Cinderline's presentation layer. Gameplay collision continues to come from the simulation's authoritative obstacle rectangles.

## Runtime paths

- `RockA` -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_Rock_A`
- `RockB` -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_Rock_B`
- `RockC` -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_Rock_C`
- `RockD` -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_Rock_D`
- `RockE` -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_Rock_E`
- `RockF` -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_Rock_F`
- low obstacle skirt -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_CliffMass`
- gravel/talus scatter -> `/Game/Art/Canyon/Meshes/SM_CinderCanyon_Debris`

All meshes use `/Game/Art/Canyon/Materials/M_CinderCanyonRock`. Each asset has separately authored LOD0, LOD1, and LOD2 FBXs. The six rocks range from broad and squat to tall and fractured so repeated obstacle rows do not read as clones.

## Geometry contract

The source unit is one centimeter. Every LOD has its origin at the bottom center, its base at `Z=0`, and the same bounds as the other LODs for that asset. The largest horizontal dimension is at most 100 cm. FBXs contain one material section, UV0, vertex-color strata masks, no collision, and no Nanite configuration.

Generate with at most two Blender CPU threads:

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 --python scripts/create_canyon_assets.py
```

The generator validates source geometry and reimports every FBX to verify the portable triangle, LOD, material, UV, bounds, and pivot contract. It writes the exact results and source hashes to `manifest.json`.

Run `scripts/unreal_canyon_assets.py` through `UnrealEditor-Cmd` to create the Unreal assets. Set `CINDER_REIMPORT_CANYON=1` only when intentionally updating assets previously owned by that importer.
