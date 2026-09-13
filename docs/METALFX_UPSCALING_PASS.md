# MetalFX upscaling

Requested 2026-09-13. Implemented and locally verified. Physical-device performance acceptance remains pending.

## Intended behavior

Render the battlefield at a lower resolution and reconstruct it with Apple's MetalFX spatial scaler before drawing the native-resolution HUD. Keep the existing iPhone frame caps, thermal recovery, menu world-rendering suppression, touch coordinates and approved menu spacing.

Spatial scaling fits the current mobile forward/FXAA renderer. Temporal scaling and frame interpolation are separate features and are outside this pass.

## Work

- [x] Inspect the project renderer and thermal policy.
- [x] Verify the installed engine's supported native Metal integration points.
- [x] Implement runtime capability checks, cached scaler resources and first-use validation with an ordinary-renderer fallback.
- [x] Integrate a conservative render-resolution policy without overriding lower thermal/user limits.
- [x] Verify actual MetalFX encoding, scene/output dimensions, HUD sharpness and menu behavior.
- [x] Build and verify the signed SDK 27 iOS package and its native-on-Mac execution.
- [x] Record exact source/package evidence and remaining physical-device acceptance.

## Validation boundaries

AEON is not part of the local rendering validation. The exact signed package is installed there, but it was left unlaunched when the user deferred further device work and requested visual recommendations instead. Native Designed for iPad execution on Mac is not an Xcode Simulator or physical-iPhone performance result. No FPS or thermal improvement is established until measured on the applicable renderer/device.

## Integration

The project plugin uses UE 5.8.2's `ISpatialUpscaler` after antialiasing and tone mapping. Supported SDR game views render at 80% of output width and height, with lower existing resolution limits taking precedence. Disabling MetalFX restores the prior setting. Canvas and Slate remain at output resolution.

The installed engine does not export its active Metal command buffer. `scripts/prepare-metalfx-engine.py` applies two small, hash-verified patches to the existing sibling engine clone and opts only MetalRHI into recompilation for Cinderline targets. The bridge ends UE's active encoder, passes its fence to MetalFX and continues on the same command buffer. It adds no separate command queue or GPU wait. Stock Launcher files remain unchanged. `scripts/unreal.sh` selects the verified clone for builds and launches.

The plugin weak-links MetalFX, checks OS/device support and checks for the bridge. It caches at most four scaler configurations, including failed creations. A new configuration first encodes into scratch output while the player sees UE's ordinary upscale. Later frames use the MetalFX result after the actual native texture contract passes. Native formats, usage, storage and hazard tracking are checked; scaler mutation is synchronized. The plugin supports SDR game views, with ordinary rendering retained for unsupported view/output configurations.

Development diagnostics are available through `r.CinderMetalFX.Status` and `cinder.quality`. They report registered/encoded frames, validation probes, scaler creation count, input/output dimensions and fallback reasons. `r.CinderMetalFX.Enabled` toggles the feature; `r.CinderMetalFX.ScreenPercentage` requests scene quality, with the game constraining that request to 50–100% while honoring lower external limits.

View-extension registration waits for Unreal's engine-ready event. Auxiliary views leave the main viewport's eligibility unchanged, so scene captures cannot toggle its render percentage.

The iOS profile sets `rhi.Metal.ForceIOSTexturesShared=0` at startup. A native iOS diagnostic proved the initial output failed only Apple's private-storage requirement: formats, usage, dimensions and hazard tracking all passed. The setting selects Unreal's existing private GPU allocation path, including its staging/upload handling; explicit readback, memoryless and PVRTC exceptions remain handled by the engine. It applies to ordinary GPU textures throughout the app, rather than only MetalFX output, and is never toggled during play. Physical-device streaming, memory and sustained thermal acceptance remain required.

## Mac verification

- The isolated MetalRHI bridge and project/plugin compile successfully. The stock engine's patched-file hashes remain unchanged.
- All 24 Unreal integration, tutorial, UI and presentation tests pass. The tutorial victory test reports two existing warnings from the engine's incompatible legacy `idevice_id` helper; no test failed and the fresh test run contains no ensure/assert/fatal error.
- Real Metal API validation: 215 encoded MetalFX frames, one cached scaler, one validation probe, zero fallbacks and no Metal API errors. Input 1536×864, output 1920×1080. Evidence: `artifacts/metalfx/mac-api-validation/`.
- The ordinary renderer matrix passes enabled/disabled at 1920×1080, enabled with the compact HUD at 956×440, and a normal fresh main menu. The menu disables world rendering and records zero new MetalFX encodes over five seconds. Screenshots were inspected and protected player saves/preferences remained unchanged.
- A separate stock-engine run loads the same project plugin, reports `engine_bridge_unavailable`, retains native 100% scene resolution and submits zero MetalFX work. The verifier explicitly prefers stock dylibs because project plugins contain their build engine's runtime search path. No stock engine file is modified.
- Final matched 120-frame Mac samples at 1080p: enabled GPU mean/median/p95 **11.562/11.561/14.147 ms**; disabled **14.491/14.492/17.831 ms**. The preceding run measured 12.587/11.961/18.682 versus 13.279/13.348/15.174 ms, showing tail variability. These short Mac results are not iPhone temperature or battery measurements.

The 80% linear scene setting shades approximately 64% as many scene pixels. This does not reduce all GPU work by 36%: geometry, shadows, native HUD and the MetalFX pass still have costs.

## Signed iOS validation

The SDK 27 ARM64 package at `Saved/Packages/IOS/Cinderline.app` passes strict signature, provisioning, platform, SDK and cooked-container verification. Executable SHA-256: `594f5a626d1e875152355c243cdd25a38511394a78165df8b2742934eee96b0c`.

Four fresh native Designed for iPad-on-Mac processes used the exact signed executable, verified by hash before terminating only the test-owned process. The runs preserved player state, produced complete screenshots and created no crash reports:

| Case | Native result |
| --- | --- |
| MetalFX enabled | 226 encoded frames, one scaler/probe, zero fallbacks; 1642×1229 → 2052×1536 |
| Disabled | No encoded frames; restores automatic native scene resolution |
| Minimum thermal quality | 225 encoded frames, two finite cached configurations, zero fallbacks; 1437×1076 → 2052×1536 |
| Fresh main menu | World rendering disabled; zero encodes during a five-second observation; first-run tutorial offer and full-resolution backdrop render correctly |

Screenshots for enabled, disabled, minimum quality and menu were visually inspected. A separate run of the final signed iOS executable explicitly enabled Metal API validation and completed real MetalFX frames without validation errors. Evidence is in `artifacts/metalfx/native-ios-on-mac/`.

The matched light army fixture on the iOS renderer running on Mac took **4.633/3.984/8.400 ms** GPU mean/median/p95 with MetalFX, versus **3.922/3.530/7.727 ms** disabled. MetalFX costs more in this particular fixture. Reduced scene pixels alone do not establish an energy saving or faster frames. The minimum thermal preset measured 3.498/3.046/6.103 ms, but it also reduces other effects and is not a matched MetalFX-only comparison. Measure sustained scenes on AEON before making performance, battery or temperature claims or choosing a final shipping quality policy.

The exact package installed successfully on AEON as `com.cinderline.game`, version `1.0` (`56702186.0.30`). The install result and follow-up app query report the same bundle URL. The package was not launched, and no physical rendering, touch, gameplay, streaming, memory, temperature, battery or performance result is claimed. The user deferred further device interaction. Prior army/HUD, guided tutorial and difficulty features remain in this package. Evidence: [`aeon/install-verification.json`](../artifacts/metalfx/aeon/install-verification.json).

## Reproduction and evidence

- `python3 scripts/prepare-metalfx-engine.py --check`
- `./scripts/unreal.sh build`
- `python3 scripts/verify-metalfx.py`
- `python3 scripts/verify-metalfx.py --case stock-fallback`

The final evidence index is [verification.json](../artifacts/metalfx/verification.json), with a hash of the complete relevant source snapshot, package identity, native frame counters, individual case reports and physical-device boundaries. Mac compile and the SDK 27 ARM64 compile, assembly and fresh package steps succeeded. Final Unreal regression count: 24 successes, zero failures.

## References

- [Apple: Boost performance with MetalFX Upscaling](https://developer.apple.com/videos/play/wwdc2022/10103/) describes spatial scaling after antialiasing/tone mapping and drawing UI afterward. Scalers should be reused instead of created every frame.
- Installed engine: UE 5.8.2 at `/Users/Shared/Epic Games/UE_5.8`; SDK 27 iOS clone at `/Users/chirag13/Documents/ChatGPT/CinderlineEngineIOS27`.
