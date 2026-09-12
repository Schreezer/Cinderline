# Pause and background GPU use

The player reported high GPU use even with the game paused and while not playing.

## Cause

The Mac profile has no FPS limit. Earlier performance comparisons deliberately used uncapped rendering. `ACinderBattlefield::SetPaused` only gates the simulation, so Unreal continues drawing the complete native-resolution scene and HUD. The adapter also repeated its fog scans and instance comparisons while its simulation was frozen. A stationary picture therefore did not imply idle CPU/GPU work.

## Changes

- A desktop standalone game-engine frame policy limits active gameplay to 120 FPS, foreground menus/paused/results to 30 FPS, and background/minimized windows to 10 FPS.
- The engine's existing lower limit still wins. Frame-state changes do not rewrite `t.MaxFPS` or user settings.
- Rendering quality and resolution stay at the existing Mac settings. The policy does not apply to editor/PIE, headless automation, dedicated servers or the separate iOS configuration.
- Frozen battlefield ticks skip simulation presentation work. Entering pause submits the latest pose/fog once, while repeated pause actions submit nothing. Start/load still populate the scene immediately, and the tick ending a match submits its final state before later ticks become idle.
- An unpaused match still advances when unfocused. Ten frames per second permits the simulation's fixed steps without deliberately pausing gameplay.

## Verification

- [x] Build the Mac Development module in an isolated project copy while preserving the player's current game.
- [x] Pass four Unreal integration paths, including frame-policy transitions and idle presentation counters. All four succeeded with zero test errors/warnings in the isolated copy.
- [x] Apply the new build with the player's authorization. The root build passed in 30.64 seconds with at most two compilation actions in parallel.
- [x] Measure actual foreground menu and paused cadence at 30.00 FPS; observe the live engine selecting gameplay 120 and background/minimized 10 FPS limits.
- [x] Compare short GPU samples on the same native-resolution menu before and after.
- [ ] Measure sustained background cadence and active gameplay after resuming a normal match. Automated app activation did not reliably transfer focus; the final minimized check confirms cap selection only. Further load testing stopped after the player reported loud fans.

## Live results, 2026-09-11 UTC

Both menu runs used the Frontier map at 2560×1440, native resolution, DPI 2 and the existing TAA quality settings. The comparison restarted the game using the old engine policy, then the rebuilt custom engine. The uncapped baseline was stopped as soon as the player reported loud fans.

| Check | Before | Fresh build |
| --- | ---: | ---: |
| Foreground menu measured cadence | 91.66 FPS | 30.00 FPS |
| Menu whole-GPU utilization, sample mean | 99.70% over 10 seconds | 50.75% over 8 seconds |
| Paused foreground measured cadence | Excluded due to a capture hitch | 30.00 FPS |
| Minimized engine limit selected in live log | Not measured | 10 FPS |

Each accepted cadence result covers 120 rendered frames after 30 warmup frames. Menu and paused profiles report foreground throughout. The paused scene is a development visual fixture, not a complete match or normal gameplay performance test. The gameplay transition logs a 120 FPS ceiling; it does not establish that the renderer achieves 120 FPS.

GPU samples use macOS AGX whole-device utilization counters. They include other applications and WindowServer, do not identify individual processes, and do not measure power, fan speed or temperature. These are short observations, not a sustained thermal benchmark. The menu comparison supports lower observed idle load without reducing render quality.

The first paused GPU sample crosses a focus transition, and both files named `new-background-gpu.json` and `new-background-confirmed-gpu.json` remained foreground in the associated cadence profiles. They are retained as excluded attempts. The final minimized sample includes the transition into the 10 FPS policy; there is no accepted measured background cadence result. The old paused cadence is excluded because a screenshot was requested during its profile.

The rebuilt game was closed gracefully at 18:48:50 UTC to let the Mac cool down. No Unreal game, build or shader compiler remained running in the final process check. The fresh module is ready for the next launch. No further heat-producing tests were run.

Build output, four passing integration results, the 91-input source hash manifest, live logs and raw GPU samples are in [artifacts/idle-gpu](../artifacts/idle-gpu/). [verification.json](../artifacts/idle-gpu/verification.json) records accepted results and exclusions separately.

The separate fog-while-panning report remains unconfirmed and is tracked in [TRACKPAD_BUILD_FEEDBACK_PASS.md](TRACKPAD_BUILD_FEEDBACK_PASS.md).
