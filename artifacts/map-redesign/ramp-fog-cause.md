# Ramp fog diagnosis — before visibility correction

Capture: `verified-motion/run-20260922T153147Z-OGKEN9`, frame 0/tick 44 through frame 89/tick 104.

The standalone `ramp-fog-repro.cpp` reconstructs the authored `StageCanyonSector` and `StartSectorOrders` fixture. Its tick-44 counts match the recorded capture exactly: 3150 unknown, 0 explored, 946 visible. By tick 104 these become 3149 unknown, 13 explored, 934 visible. All 13 darkened cells transition from visible (2) to explored (1), never to unknown (0). Terrain geometry is unchanged.

At tick 83/frame 58, cells x=26, y=14..18 darken as Ember descends below their conservative height requirement of 32.119 cm. At tick 94/frame 74, the ramp-foot cells x=27, y=14..18 also become explored. These 75 cm cells extend past the actual foot at x=2050. Nearby Anchor(2260,950), Skim(2240,1350), and Anvil(2140,1200) are all on zero-height ground; the previous rule prevents each from revealing the sloped surface despite being within normal sight range.

The same conservative sampling temporarily hides Ember's own cell at ticks 83–92 and 94–103. Own-unit rendering still draws the unit; by tick 104 all own unit cells are visible. This is a gameplay visibility limitation, not only a material issue. A later task explicitly authorizes correcting ordinary vision of gradual ramps while retaining the gate for actual upper plateaus, including mixed plateau-edge cells. Keep this baseline as evidence; future precision work may still be needed at a cell that contains both the crest and a real upper plateau.

The prior material multiplies paving diffuse by `1 - fog opacity`, so a fully explored cell loses its paving texture and appears flat navy. Preserving some already-explored static surface detail addresses that separate presentation effect; it does not replace the gameplay visibility correction.

Build the standalone reproduction with the current portable simulation sources, for example:

```sh
clang++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -I Source/Cinderline/Public Source/Cinderline/Private/Sim/*.cpp artifacts/map-redesign/ramp-fog-repro.cpp -o /tmp/cinder-ramp-fog-repro
/tmp/cinder-ramp-fog-repro
```

`ramp-fog-repro.log` records the pre-correction behavior. In the historical log, `requiredHeight` is the old nine-sample maximum surface height. The current reproduction source reports the shared active `terrainVisibilityHeight` gate as `requiredHeight` and also prints the old sampled maximum as `maxSurfaceHeight` for comparison.
