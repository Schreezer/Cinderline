# Cinderline visual target plan

Current package note, 2026-09-12: navigation package `2bf6039a...` passed SDK 27 build, package, signing and native iOS-on-Mac checks, then installed successfully on AEON, an iPhone 17 Pro Max running iOS 27. The physical menu is visually verified and the app remains running. The reported disappearance most likely came from the agent forcing a post-install relaunch after the user had started playing; no new crash or memory-termination report was found. Physical routing, touch, boundary and sustained thermal tests remain pending. Historical `1870dcf9...` phone thermal evidence, `7c603b00...` native edge captures and `4ae4305a...` native thermal runs remain tied to their original packages. See [NAVIGATION_PASS.md](NAVIGATION_PASS.md).

12 September 2026. The visual kit is built, imported and tested. Final Mac stills exist, and the final SDK 27 safe-area package installed and rendered on AEON. The player confirmed that the main-menu spacing is perfect. The phone then became very hot, so iPhone heat and performance work is now first. Persistent public online service and a dedicated HUD redesign follow. Further terrain expansion is deferred; an authored Unreal Landscape is the proposed later direction. Delivery and actual evidence are tracked in [VISUAL_TARGET_PASS.md](VISUAL_TARGET_PASS.md).

The reference labels itself a mobile RTS concept, not actual gameplay. It establishes a target for composition, detail and atmosphere; it does not establish achievable phone performance.

## Historical starting point

The pass began with working RTS rules, construction/production, combat events, fog-filtered presentation, instanced units, articulated motion and the compact mobile HUD. Those foundations were preserved.

The following limitations described the baseline before this pass. The original model and LDR statements are historical and no longer describe the current build:

- Ground presentation was a flat play surface with simple noise and material stains. The pass improved its material and scenery treatment, but it remains flat. No authored Unreal Landscape exists yet.
- The original models had clear silhouettes and five shared material roles, but no three-level LOD chain or finished surface treatment. The replacement set now covers all seven building/mineral meshes, eight units and eighteen motion parts, with three validated LODs each.
- Much combat feedback used HUD lines, rings and crosses. The completed world-effects work now drives bounded flashes, projectiles, impacts, dust, healing and destruction from authoritative events.
- iOS used a conservative LDR configuration with shadows and mobile anti-aliasing disabled. The current path uses the tested mobile HDR, FXAA and shadow configuration.
- The reference packed buildings, formations, roads and props into a composed view. The pass increased model and scenery density, but its achieved art should be judged from the actual final captures rather than as reference parity.

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

The Mac portion now has actual final base and battle stills. A six-second real in-engine battle recording also exists, but it predates the final ground anti-tiling and ambient-occlusion polish. The AEON main-menu spacing has passed in the captured orientation. The next milestone is a bounded heat and performance investigation, followed by physical gameplay: visually verify the other landscape orientation, then test multitouch, training, construction, production, a complete match and thermal behavior. Phone timing must come from the phone; the mixed-state Mac sample is not a phone-performance result.

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
- [x] Import final v1.6.1 materials; validate all 33 custom meshes at three LODs and all twelve scenery batches.
- [x] Pass Mac and SDK 27 iOS builds, the 636-package iOS cook/package, 15/15 focused Unreal tests and 2/2 portable tests.
- [x] Capture actual final Mac base and battle stills.
- [ ] Record a final-polish battle video. The available six-second recording predates the last ground anti-tiling and ambient-occlusion changes.
- [x] Install the menu-only safe-area package on AEON and obtain player approval of the revised main-menu spacing.
- [ ] Visually verify the captured landscape-right result; both-orientation acceptance remains open.
- [x] Build and validate the schema 3 thermal package on Mac while preserving the accepted menu and HUD geometry.
- [x] Build, sign and validate map-border package `7c603b00...` through three rendered native iOS-on-Mac edge captures.
- [x] Install navigation replacement `2bf6039a...` on AEON. Installation and physical menu verification passed; the reported disappearance most likely came from the agent's forced relaunch.
- [ ] Resolve the current physical launch failure, then test routing, physical boundaries and a sustained ordinary match on a cooled phone.
- [ ] Complete the AEON gameplay, multitouch, full-match and thermal playtest.

## Implementation notes, 12 September final verification

- Final `cinematic-visual-target-materials-v1.6.1` import passed. All 33 custom meshes have validated pivots, five material slots where required and three LODs. The original tier, cost and placement rules remain in force.
- All twelve scenery batches imported. Six use UV-preserving decimations of a CC0 photogrammetry scan. Road and pad heights were corrected after rendered fog-plane intersections were found.
- Final ground and model materials are integrated. The shared finish uses low-strength CC0 metal maps, vertex-baked ambient occlusion and restrained wear. The ground adds a rotated secondary albedo sample to reduce visible tiling.
- World effects follow gameplay and use fixed instance caps. The hidden-target direction guard and its regression test are complete.
- Mac and SDK 27 iOS builds passed. The full physical-iOS cook/package passed with 636 packages, as did 15/15 focused Unreal tests and 2/2 portable tests.
- The packaged app installed and launched on AEON, an iPhone 17 Pro Max running iOS 27. A captured screen and the player's observation confirm normal menu rendering. The launch then exposed a Dynamic Island overlap on the left menu buttons.
- The final menu-only repair reads UIKit insets on the main thread without broadcasting a global Unreal safe-area change. It applies and restores the adjusted Canvas region only while drawing the menu, leaving the full-bleed background and gameplay behavior intact. The SDK 27 package installed and launched, and the player confirmed that the revised menu spacing is perfect.
- The captured runtime retained a 1912x880 full-bleed Canvas with UIKit insets of 124, 0, 124 and 40 render pixels. The 2868x1320 screenshot has a 1.5 scale, so those values must not be restated as 186 native pixels. The raw landscape-right screenshot has not been visually accepted.
- Final Mac base and battle stills are complete. The measured 14.79 FPS wall-clock mean spans mixed foreground/background capture states, including 85 background frames and the 10 FPS background cap. GPU history reported a 16.492 ms mean. These figures are not a steady gameplay benchmark and do not measure iPhone performance.
- The player reported that AEON became very hot during the historical `1870dcf9...` run, so further play paused for cooldown. Historical thermal package `4ae4305a...` passed schema 3 build/package/signing, tests and five fresh-process iOS-on-Mac launches; its evidence is preserved separately and it was not installed. Historical map-border replacement `7c603b00...` passed its 44-action SDK 27 build in 157.72 seconds, packaging in 93.72 seconds, post-build check, strict signing, sustained entitlement/profile and Game Mode checks. Three fresh 2052 x 1536 native iOS-on-Mac edge captures passed visual inspection and exact-black probes with no new crash reports. These runs do not prove physical gestures or phone performance. Navigation package `2bf6039a...` is now installed on AEON, and its physical menu is verified. The reported disappearance most likely came from the agent's forced relaunch. Current priority is combined physical routing, boundary and sustained thermal acceptance, followed by persistent public online service, dedicated HUD redesign and authored Unreal Landscape.
- No Unreal Landscape was added. The flat playing surface remains.
