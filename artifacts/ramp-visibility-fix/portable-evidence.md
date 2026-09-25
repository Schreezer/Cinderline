# Portable ramp visibility verification

All MapTerrainSimulationTests pass after the gameplay fix. The new Anchor-only, moving-own-cell, and repeated-save/load regressions each failed against the preserved previous implementation. Existing upper-ground combat, mixed plateau-edge privacy, air vision, compatibility, replay, placement, movement, and AI checks remain passing.

The exact sector-live fixture retains 946 visible cells at tick 44. At tick 104 it has 947 visible, zero explored-only, and 3149 unknown cells; the old implementation instead had 934 visible and 13 explored-only cells. No initially visible cell darkens during this reproduction, and no own unit's cell becomes hidden at ticks 44–104. The one additional visible cell is ordinary new exploration by the moving units.

Anchor(2260,950) at zero terrain height supports ramp cell(26,16), center(1987.5,1237.5), after both patrol units descend. Its normal radius covers that cell; the ramp's required observer height is now zero rather than the old sampled 32.119 cm. Separate isolated-Anchor tests prove sight across the upper ramp interior, middle and foot in both orientations while the actual upper-plateau point(1490,950), within sight radius, stays hidden.

The remaining conservative boundary is a cell containing a real upper plateau together with the ramp crest; that cell retains its upper-height gate. The safe ramp interior no longer has the previously observed own-cell flicker.

Sources: `map-terrain-simulation-tests.log`, `ramp-fog-repro-after.log`, and machine-checked `portable-evidence.json`. Historical source/log/cause are in `../map-redesign/ramp-fog-*`; baseline new-regression failures are in `../map-redesign/ramp-visibility-before-tests.log`. No Unreal process was launched for this verification.
