# Metal performance pass

User priority: use the Mac's Metal capabilities efficiently and keep the codebase efficient while preserving the new visual quality. iOS stays deferred.

## Goal and checklist

Measure the real CPU/GPU bottleneck, remove unnecessary work, compare native-resolution timing and appearance, run relevant regressions, and save a local checkpoint.

- [x] Record a current Metal baseline and identify the limiting stage.
- [x] Review presentation resource/instance churn and HUD submission costs.
- [x] Implement measured or directly verifiable efficiency improvements.
- [x] Validate the native-resolution AA comparison and final scene appearance; exclude foreground-contaminated longer samples.
- [x] Run applicable builds/regressions and preserve evidence.
- [x] Record remaining performance limits and save a local checkpoint.

## Measured renderer decision

Hardware: Apple M1 Max, 32 GPU cores, 32 GB unified RAM. UE 5.8.2 Development game uses Metal SM5. All comparison samples use 2560×1440 framebuffer pixels, 100% primary and secondary screen percentage, no dynamic resolution, VSync off and no FPS cap. This is the local Development game, not a packaged Shipping benchmark.

The original frozen cliff scene was GPU limited: stat unit showed about 20.7 ms GPU, 1.82 ms game thread and 1.91 ms RHI. The 21.76 ms render-thread counter includes waiting and is not CPU utilization. Unreal's GPU capture attributed 13.05 ms to the TSR scope; its nested ClearPrevTextures attribution is not proof that clearing a texture alone consumes that time. A direct anti-aliasing A/B confirms that the TSR path is expensive on this configuration.

Exploratory samples on the old binary, same frozen scene and camera (180 frames after 30 warmups, all foreground):

| Setting | Mean ms | p95 ms | FPS |
| --- | ---: | ---: | ---: |
| Original TSR | 21.517 | 22.677 | 46.48 |
| TSR async compute disabled | 20.761 | 21.377 | 48.17 |
| TSR history update quality 2 | 20.771 | 21.385 | 48.14 |
| Native TAA | 9.333 | 10.028 | 107.14 |
| Original TSR, repeated | 20.869 | 21.465 | 47.92 |
| Native TAA, repeated | 9.354 | 9.943 | 106.91 |

Each experiment restored unrelated changed settings. Async scheduling and history quality gave only small gains, so their defaults are retained. The selected Mac setting is native TAA, quality 2. Textures, anisotropic filtering, models, shadows, ambient occlusion, reflections, bloom and Retina UI stay at the visual-upgrade settings. Resolution is not reduced. TAA and TSR have different temporal reconstruction/edge behavior; this is an AA tradeoff, not a claim of pixel-identical output. Matched stills retain fine terrain detail and crisp UI, and live combat/camera captures were inspected. Longer human motion/ghosting evaluation remains useful.

The Mac profile is tuned on this M1 Max; other Mac generations have not been benchmarked. Use the supported Metal renderer and let Unreal select hardware capabilities. No forced SM6 flags, engine patches or speculative MetalFX plugin were added. Lumen/Nanite/VSM are not enabled by this pass; expensive features need a measured visual benefit in this RTS.

## Code efficiency

- ISM batches keep the last accepted transforms and skip identical poses with zero tolerance. Moving instances submit changed spans through UE 5.8's instance-data update path, avoiding scene-proxy recreation on every 20 Hz update.
- Count changes append or remove tail instances and update retained positions. Normal movement, training, death and visibility changes avoid whole-batch clear/rebuild. Recovery remains for rejected or externally altered component state. Fog filtering and dense visible order remain unchanged.
- Decorative presentation meshes no longer register navigation influence; simulation owns pathfinding.
- The minimap merges consecutive cells of the same fog state within each row. It still samples the current fog and retains visible/explored/unknown precedence, color boundaries and dot visibility. This reduces CPU rectangle submission; Unreal already batched those rectangles, so the old 4096 cells were not 4096 GPU draw calls.
- Canvas labels consume a string view without constructing an FText each draw. Font sizing, glyph rendering and layout are unchanged.
- Development diagnostics support 120/180/600/1200 viewport frames and separate raw CPU/GPU summaries. GPU samples consume fresh completed engine-frame history and explicitly report availability, lag and history gaps. These are not per-frame synchronized CPU/GPU traces.

`cinder.models` logs actor-lifetime accepted instance delta/add/remove/rebuild/skip counters. They demonstrate adapter submissions, not GPU work or frame-time savings. The frozen fixture disables actor ticks, so instance optimization cannot explain its AA speedup.

## Final verification

- UE 5.8.2 Mac Development build passed after adding the missing RenderCore module dependency for the published timing counters.
- Fresh portable tests passed: 26 rule groups and all 3 CTests, including 3,000 offscreen native frames / 150 simulated seconds. Native stress is not an Unreal GPU benchmark.
- All 4 Unreal integration paths passed with zero test errors/warnings, run `20260911T163923Z-27530`.
- The lifecycle path now checks real component counts and transforms through normal movement, addition, ordinary combat death, same-count replacement, shrink and unchanged re-render. These NullRHI checks validate adapter output; rendered captures provide separate visual evidence.
- Two initial 1,200-frame final-build samples lost foreground focus (86 and 549 background frames). They remain in the raw evidence but are excluded from comparable FPS claims.
- Fresh-launch settings confirmed native TAA quality 2 from the Mac device profile, a 2560×1440 render target, DPI 2 and no dynamic resolution. Final menu and live-cliff captures were inspected.
- The accepted timing comparison is the six pre-rebuild A/B samples above. It isolates the AA choice. No final-build aggregate FPS gain is attributed to the separate instance/HUD edits, and longer final-build foreground cadence remains unverified.
- Live final-build instance counters advanced from 118 to 272 render passes with 308 changed-transform submissions, no add/remove operations in that interval, and zero full rebuilds. Unchanged batch skips increased from 5,775 to 13,013. This confirms continued visual updates without resubmitting stationary batches. A separate normal skirmish reached 932 ore at 00:34; its adapter recorded 1,079 changed transforms and zero full rebuilds, and the final worker/minimap scene was inspected.

## Evidence

Runtime artifact snapshots retain Cinderline measurements, Metal/RHI messages and errors; full process logs remain under `Saved/Logs`. Trailing whitespace is normalized.

- `artifacts/metal-performance/before-runtime-evidence.txt`: current-turn old-binary samples, effective settings and complete GPU pass capture.
- `artifacts/metal-performance/before-gpu-passes.txt`: largest captured GPU scopes.
- `artifacts/metal-performance/before-scene.png` and `experiment-native-taa.png`: identical camera and frozen scene with TSR/TAA.
- `artifacts/metal-performance/portable-results.txt` and `portable-details.txt`: fresh portable/native regression results.
- `artifacts/metal-performance/integration-results.json`: four successful engine paths, including the actual instance regression.
- `artifacts/metal-performance/final-build-diagnostics.txt`: both excluded longer samples, their valid engine-counter availability and foreground counts.
- `artifacts/metal-performance/final-default-runtime.txt`: fresh-launch effective settings and live instance counters.
- `artifacts/metal-performance/final-menu.png`, `final-live-cliffs.png` and `final-normal-match.png`: inspected final renderer/HUD output.
- `artifacts/metal-performance/verification.json` and `tested-source-sha256.txt`: evidence index and 132 tested source/config/asset inputs.

## Profiling references

The workflow follows Apple's guidance to measure both CPU and GPU before choosing graphics settings ([Metal performance guidance](https://developer.apple.com/documentation/metal/improving-your-games-graphics-performance-and-settings)) and Unreal's stat/GPU profiling tools ([Epic profiling overview](https://dev.epicgames.com/documentation/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine)). This pass used Unreal timing/GPU captures on its Metal backend; it did not record a separate Xcode Instruments trace. API behavior was checked against this installation's UE 5.8 source.

## Remaining work

Full human skirmish, sustained late-game/thermal profiling, packaged-build timings and other Mac GPUs remain unmeasured. Current short/medium samples cannot guarantee a minimum FPS across all matches. Continue animation and terrain composition work without rebuilding unchanged presentation resources; profile again when unit density, effects or materials change. iOS remains a later, separate performance budget.
