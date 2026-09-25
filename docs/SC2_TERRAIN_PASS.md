# StarCraft II inspired terrain pass

The terrain now separates worn travel lanes, rough open ground and blocked rock more clearly at the RTS camera distance. All three canonical maps use weathered plateau caps, shallow geological cuts, irregular sediment bands and sparse low outcrops. The ground uses muted olive and dust colors with existing coarse rock texture assets.

## Implementation

- `CinderLandscapeTerrain` keeps the obstacle guard and walkable relief constraints, adds broad cap facets, and exports the existing authored route segments for surface paint. The height profile version is **4**; `Frontier.umap` was rebuilt to match.
- `CinderTerrainSurface` paints broad worn routes into the cached terrain mask, interrupts them at known cliffs, and strengthens macro variation. Routes depend on map, size and player count, never hidden units or resources.
- `CinderScenery` uses 50–120 cm cap outcrops when the Landscape is present. Flat fallback maps retain their full cliff meshes. The terrain mode participates in the scenery cache, and scatter avoids painted routes.
- `unreal_canyon_surface.py` reuses the existing coarse CC0 rock texture, adds irregular strata and weathered rims, and attenuates stretched ground normals on steep faces. It retains seven pixel texture samples and one vertex sample.
- The battlefield sun uses depth bias 0.70 and slope bias 1.0. A shadows-off comparison isolated the dense horizontal cliff stripes as self-shadow artifacts; incremental bias comparisons removed them with shadows enabled. Diagnostic captures are under `artifacts/sc2-terrain/shadow-diagnostic`.

This is a presentation change. Terrain obstacles, navigation, construction rules, scouting and combat elevation rules still come from the existing simulation.

## Verification

The Mac Development editor builds successfully. Seven focused Unreal tests pass with zero test errors, warnings or unfinished results. They cover all 18 map/length/player-count geometry fixtures, route masks, scenery bounds and caching, fallback behavior, fog privacy and world lifecycle. See `artifacts/sc2-terrain/final-automation/index.json` and its exact expected-test manifest.

The material refresh and map authoring validate three 127×127 Landscapes, twelve components and twelve renderable component materials. The material report is `artifacts/sc2-terrain/material-import.json`.

`scripts/capture-terrain.sh` captures each canonical map and the fog boundary in a separate Unreal process at 1440×900. It checks shader and mesh compilation completion, active canonical terrain, screenshot freshness and PNG dimensions before accepting a frame:

```sh
./scripts/capture-terrain.sh map0 map1 map2 fog --output artifacts/terrain-captures
```

Runtime and image verification in this pass use Mac Unreal. Mobile packaging, physical-device performance and platform-specific shadow tuning are separate checks.

Final captures: [map 0](../artifacts/sc2-terrain/map0.png), [map 1](../artifacts/sc2-terrain/map1.png), [map 2](../artifacts/sc2-terrain/map2.png), [fog boundary](../artifacts/sc2-terrain/fog.png), and [gameplay view](../artifacts/sc2-terrain/delivery-base/mac-battle.png). [Verification record](../artifacts/sc2-terrain/verification.json) includes capture hashes, readiness markers and test/build references.
