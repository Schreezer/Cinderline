# Cinderline visual target plan

12 September 2026. The current visual kit is built and tested. The player has since prioritized an AEON playtest, persistent online service and HUD refinement. Further terrain expansion is deferred; Unreal Landscape is the proposed next terrain approach. Delivery and actual evidence are tracked in [VISUAL_TARGET_PASS.md](VISUAL_TARGET_PASS.md).

The reference labels itself a mobile RTS concept, not actual gameplay. It establishes a target for composition, detail and atmosphere; it does not establish achievable phone performance.

## Starting point

We have working RTS rules, construction/production, combat events, fog-filtered presentation, instanced units, articulated motion and the compact mobile HUD. Preserve those foundations.

Current limitations verified in source and recent runtime captures:

- Ground presentation remains a flat play surface with noise/material stains. Rectangular simulation obstacles drive stretched cliff segments, rather than an authored geological landscape.
- The original models have clear silhouettes and five shared material roles, but lack a finished texture atlas, a LOD chain, fine surface wear and the density of mechanical detail seen in the reference.
- Much combat feedback is drawn as HUD lines, rings and crosses. Existing authoritative events can drive richer world effects.
- iOS runs a conservative LDR configuration with shadows and mobile anti-aliasing disabled. The last two settings reflect unresolved rendering compatibility problems, not a desired final quality target.
- The reference packs buildings, formations, roads and props into a deliberately composed view. Our starter-base screenshot contains a few entities and large empty areas. A camera or quality preset alone cannot close that gap.

## Implementation order

1. Build one finished test sector. Include an Anchor, Kiln, ore cluster, a small mixed army and an enemy position. Establish a camera, scale, warm light and a terrain composition that still make selection and commands clear. Capture it in the actual Unreal runtime at the gameplay camera.
2. Establish a richer, stable mobile lighting path. Test HDR, edge smoothing and affordable shadows in isolation, preserve the known-working build, and compare forward/deferred only with relevant device evidence. Profile on Aeon before setting a quality/performance promise.
3. Author the environment. Use modular cliff faces, broken shelves, a ravine, rock scatter, packed soil, roads, foundation pads, cables and localized mineral deposits. Keep background relief cosmetic initially. Traversable height changes, ramps and high-ground combat rules need explicit navigation, placement, visibility and network-rule work.
4. Finish the benchmark buildings and units. Use Blender for meshes, mechanical articulation and baking; use suitably licensed asset packs where they fit the chosen style. Add plate seams, machinery, roughness variation, restrained wear, readable weapons and emissive accents. Produce LODs and reduce material sections where practical. Image generation can supply concept sheets, portraits and texture source ideas; runtime proof must use actual game assets.
5. Add activity through existing game events. Production bay movement, mining tools, exhaust, muzzle flashes, projectile meshes, impact dust, smoke and destruction should follow gameplay. Keep fog filtering intact and control how much of the screen translucent effects cover.
6. Apply the successful asset/material kit to the other maps and roster. Polish icons and selection presentation while retaining the newly cleared mobile battlefield. Do not reproduce the reference's large advertising panel or permanently dense HUD.

## Visual direction

My recommendation is to borrow the reference's layered terrain, blue-versus-warm color contrast, readable silhouettes and busy industrial life while retaining Cinderline's charcoal/teal machinery and amber minerals. Palette and biome remain a player preference; the reference does not require copying every design.

## Mobile performance approach

- Keep instanced rendering and incremental updates. Share textures/materials, create sensible LODs, and cull small props at distance.
- Use baked surface shading where useful, plus a limited tested dynamic-shadow setup for runtime-built structures and moving units.
- Pool effects and cap smoke/particle overdraw and local lights. Treat near-camera showcase shots and a large army at normal zoom as different performance cases.
- Benchmark sustained combat, thermal behavior and fog safety on Aeon. Native iOS-on-Mac is useful for correctness and visual iteration, but does not establish iPhone FPS.
- A 60 FPS play target and an optional richer 30 FPS mode are possible product targets, not measured promises at this stage.

## First acceptance milestone

A short, real in-engine sequence of the test sector showing mining, production and a small fight. Review terrain depth, readable silhouettes, surface detail, stable edges/shadows, effects visibility and unobstructed touch controls. Record on-device frame time before expanding the art pass.

## References

- [Epic UE 5.8 mobile rendering modes](https://dev.epicgames.com/documentation/en-us/unreal-engine/mobile-rendering-and-shading-modes-for-unreal-engine)
- [Epic mobile rendering optimization](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-and-development-best-practices-for-mobile-projects-in-unreal-engine)
- [Current models](MODEL_ASSETS.md), [animation and terrain](ANIMATION_TERRAIN_PASS.md), [iOS rendering baseline](IOS_READINESS.md), [mobile HUD](MOBILE_HUD_PASS.md).

## Active implementation checklist

- [x] Detailed building and amber-crystal meshes with validated import/LODs.
- [x] Improved unit meshes preserving working articulated animation.
- [x] Modular geological terrain and industrial base scenery.
- [x] Finished model/terrain materials and affordable world effects.
- [x] Stable iOS HDR, edge smoothing and shadows; Mac lighting/camera pass.
- [x] Integrate into ordinary matches with fog, placement and multiplayer rules intact.
- [ ] Inspect real base, mining, production and battle footage; iterate on visual defects.
- [ ] Mac/iOS builds, focused regressions and bounded runtime performance evidence.
- [ ] Update final evidence and retain physical Aeon testing as a separate checkpoint if unavailable.

## Implementation notes — 12 September, final verification

- Seven building/mineral meshes, eight unit meshes and eighteen articulated parts are imported with validated pivots, five material slots and three LODs. The original tier/cost/placement rules remain in force.
- All twelve scenery batches are imported. Six use UV-preserving decimations of a CC0 photogrammetry scan. Road and pad heights were corrected after rendered fog-plane intersections were found.
- Ground and model material revisions are being judged at the gameplay camera. The shared finish includes three low-strength CC0 metal maps. The final terrain and HUD polish is in progress.
- World effects follow gameplay and use fixed instance caps. Review found a directional muzzle effect that could expose an unseen target's bearing; its final guard and regression are in progress.
- The iOS HDR/FXAA/shadow probe rendered successfully in the native Designed for iPad window. The complete SDK 27 C++ build also passes. The finished asset cook and new-roster runtime capture remain pending.
- Portable simulation/network CTests pass (2/2), and the engine suite passes (14/14) before final polish. Final builds, automation, video and frame timing will be recorded in [VISUAL_TARGET_PASS.md](VISUAL_TARGET_PASS.md).
