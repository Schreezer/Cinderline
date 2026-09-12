# Animation and terrain pass

12 September 2026. Requested while iOS installation/signing remain blocked.

## Implemented

- Drudge four-leg gait, Ember/Needle alternating steps, Anvil/Cinderthrow suspension, weapon recoil and active Drudge tools.
- Scout, Mender and Kite hover/bank. Cosmetic poses preserve authoritative body-root XY; no simulation movement or combat rules changed in this pass.
- Eighteen original articulated mesh parts, partitioned from the existing five unit models. Combined triangle count stays 10,756. All parts are required per unit kind; an incomplete pack falls back to the original full model.
- Four fractured basalt cliff variants with broken crowns, fault faces and low rubble. Their 100 cm source footprints and bottom-centered origins are preserved. Combined triangles fell from 4,512 to 3,042.
- Layered basalt, ash, dark cliff weathering and copper mineral staining. The ground material reuses existing PBR textures and adds one packed-mask sampler, for six samples total.
- A 256×256 linear terrain mask rebuilt only when observed features change. Explored cliffs and remembered mineral positions drive local transitions; current hidden ore depletion does not. Raised rocks remain inside simulation obstacle rectangles.
- Pause freezes animation; hidden/dead entities are evicted from the pose cache. Starting/loading/resetting scenes clears resource and motion presentation history. Moving parts remain instanced by mesh/team, without per-unit actors or skeletal components.

## Build and regression evidence

The Mac Development target built with at most two compile jobs. All four Unreal integration paths passed with zero errors, warnings or unfinished tests. The report is copied to `artifacts/animation-terrain/integration-results.json`; build and runner output sit beside it.

The focused motion assertions cover real displacement versus blocked movement, active mining/construction, separate cooldown-driven recoil for nearby identical units, hover, hinge positions, cache eviction, fog filtering and reset behavior. World integration also checks observed terrain layers, asset availability/fallback and existing lifecycle/idle behavior. These checks establish code behavior; rendered captures are separate evidence.

Blender roundtrips and actual Unreal imports passed for all 18 motion parts and all four revised cliffs. Imports checked source hashes, exact triangle counts, material names, dimensions and absent collision. The ground graph has no shader errors. Reports: `artifacts/motion-assets/unreal-import-results.json` and `artifacts/animation-terrain/terrain-import.json`.

Runtime `cinder.models` reported all 15 original model kinds loaded, 55 model batches including articulated parts, and zero fallback entities in the fixture.

## Rendered evidence

The selected captures are listed in `artifacts/animation-terrain/capture-manifest.json`; `animation-terrain-preview.mp4` combines them. Captures come from the actual Mac Development `SF_METAL_SM5` game window at a 2560×1440 framebuffer. Window captures include the macOS title bar and are 1280×752 logical pixels. Development fixtures use ordinary move/work/attack commands, replace an unsaved match and never write a save. Their seeded armies and resources are not normal skirmish balance evidence.

`terrain-delivery.png` and `weapons-delivery/` show the revised cliffs, weathered ground, local copper stains, attacks and health changes. `walk-verified/` shows the five-unit patrol moving and reversing, with aircraft motion elsewhere in the scene. `work-delivery/`, framed using the normal Home control, shows the miner working and returning ore (4,750 to 4,768), the builder working while construction rises through 2%, 5% and 8%, and the moving patrol. `paused-delivery.png` records the pause screen. Source assertions establish the pose transforms; these window captures establish that the imported parts and changing poses render in the actual game. Small parts remain subtle at wide zoom, so physical readability feedback is still useful.

Camera fitting needed a correction confined to the development fixture: it now stops at camera bounds and retains the best view instead of accumulating unreachable focus offsets. The mining fixture chooses a deposit that can be framed beside the builder. Earlier empty/offscreen framing attempts are excluded from accepted captures. The walking clip predates only that final mining-deposit selection; the same animation runtime and terrain assets are used throughout the selected video.

The video preserves the full captured images and their recorded wall-clock timing, encoded at 30 FPS with repeated frames as needed. It is silent and is not itself a game-frame-rate measurement. No generated imagery or retouching substitutes for runtime evidence.

## Short performance observation

`artifacts/animation-terrain/accepted-runtime.log`, 20:14:27 UTC:

- Foreground viewport, 2560×1440, 120 sampled frames after 30 warmup frames, four seconds.
- Capped at 30 FPS: measured 30.00 FPS, mean 33.334 ms and p95 33.948 ms.
- Completed engine GPU frames: mean 11.778 ms, p95 12.492 ms.
- Game/render/RHI thread means: 1.500 / 1.911 / 1.752 ms.
- Zero background frames, GPU disjoint events or polling-limit hits.

This was a small, largely settled combat fixture while screenshots were being requested. It does not measure uncapped throughput, GPU utilization, sustained thermals, a late-game army, synchronized per-frame CPU/GPU attribution or iOS performance. An earlier startup sample had 120 background frames and is excluded from foreground conclusions. The additional parts increase batches from 29 to 55; large-army draw cost remains a profiling checkpoint.

## Remaining acceptance

- Physical full-skirmish review of animation readability, especially at wider zoom levels.
- Large-army and sustained thermal profiling; iPhone rendering/touch acceptance after setup.
- Further art work can add damage/death motion, animated production mechanisms and richer environmental detail.

## Delivery state

The game was closed after the final captures. No commit or push was made in this pass. `source-hashes.json` records 89 source/config and delivery-asset hashes; unrelated local changes from earlier work are retained. The task board and Unreal/model/terrain guides are updated.
