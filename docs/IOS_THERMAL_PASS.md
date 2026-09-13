# iOS thermal and performance pass

Current package note, 2026-09-12: navigation package `2bf6039a...` passed SDK 27 build, package, signing and native iOS-on-Mac checks, then installed successfully on AEON, an iPhone 17 Pro Max running iOS 27. The physical menu is visually verified and the app remains running. The reported disappearance most likely came from the agent forcing a post-install relaunch after the user had started playing; no new crash or memory-termination report was found. Physical routing, touch, boundary and sustained thermal tests remain pending. Historical `1870dcf9...` phone thermal evidence, `7c603b00...` native edge captures and `4ae4305a...` native thermal runs remain tied to their original packages. See [NAVIGATION_PASS.md](NAVIGATION_PASS.md).

12 September 2026. AEON, an iPhone 17 Pro Max on iOS 27, reported a real `serious` thermal state during the physical run. The installed optimized package has executable SHA-256 `1870dcf9ef090b8d0125bd3265ebe5b59d82dff1723f3c800f9e330ef20b12ec`. It is now stopped, and the player has left with the phone. All remaining work in this session is Mac-only and cannot add physical-device proof.

## Delivered physical behavior

- The package applies a thermal policy of 15 FPS in menus and 20 FPS in gameplay when iOS reports serious pressure. It selects minimum quality in that state.
- The menu-only safe-area implementation and the player-approved full-bleed geometry are preserved.
- The signed app includes `com.apple.developer.sustained-execution=true`, and its plist opts into Game Mode. The observed Apple HUD reported Game Mode on.
- The final package installed and launched successfully. The game was stopped after the bounded run.

## Captured evidence

The original 600-frame physical baseline completed at 1912x880. It measured 55.29 FPS over 10.852 seconds, with 18.086 ms mean and 25.854 ms p95 viewport wall-clock cadence. Asynchronous engine counters reported 3.687 ms mean game-thread time, 10.003 ms mean render-thread time and 11.601 ms mean GPU time. These counters are not CPU utilization, GPU utilization or synchronized per-pass attribution.

The optimized quick run detected `thermal=serious` and selected 15 FPS/menu, 20 FPS/gameplay and minimum quality. A later gameplay sample reported 6.12 percent process CPU, 36.70 percent of one core, 506.88 MB process memory and 6.295 ms for the latest completed engine GPU frame. This is one sampled state under existing thermal pressure, not sustained cooldown proof.

The [`optimized-quick-scene.png`](../Saved/IOSThermal/optimized-quick-scene.png) Apple Metal HUD capture showed 19.56 FPS, 9.23 ms GPU time and Game Mode on. Apple defines the standard HUD GPU value as command-buffer duration, which can include gaps between encoders. Encoder timing was unavailable here, so neither this value nor the engine's 6.295 ms sample establishes GPU utilization or attributes cost to a rendering pass. The earlier parsed menu capture in [`baseline-menu.json`](../artifacts/ios-thermal/baseline-menu.json) covers a different original-menu window with DeviceHub mirroring active and no sampled physical thermal state; it must not be used as the optimized result.

The optimized command requested `cinder.profile 300`, but the profiler only accepts 120, 180, 600, 1200, 9000 and 18000 frames. It printed usage and produced no aggregate result. There is no optimized aggregate profile to compare with the successful 600-frame baseline.

## Controlled Mac comparisons

Five 600-frame Mac runs at 1440x810 used the 30 FPS cap. Their diagnostics reported `foreground=0` and `background_frames=600` while Slate classified the screen as Menu or Gameplay. Treat them as controlled render-cadence comparisons only. They are not sustained foreground results and do not predict phone temperature or performance.

The menu baseline reported 8.243 ms mean GPU time and 13.055 ms p95. After gating world rendering on non-gameplay screens, the first comparison reported 1.919 ms mean and 4.069 ms p95. A repeat reported 1.774 ms mean and 4.064 ms p95. The verified final screenshot at [`mac-menu-final/scene.png`](../Saved/IOSThermal/mac-menu-final/scene.png) retains the approved full-bleed menu. The earlier `mac-menu-after/scene.png` was captured during startup before map load and is rejected as visual evidence.

A single GPU-pass capture before the gate attributed 6.291 ms to scene rendering, including 1.148 ms lighting and 1.101 ms postprocessing. After the gate, the capture contained no scene-render pass; Canvas used 1.693 ms within a 1.990 ms root. These single captures explain the menu-only change but are not aggregate per-pass measurements.

The battle comparison changed from 11.553 ms mean and 17.968 ms p95 GPU time to 11.470 ms mean and 17.622 ms p95 after disabling the translucency lighting volume. This does not support a significant aggregate saving claim. The current fog, dust and glow materials are unlit, so the volume added no needed GPU lighting work; an approximately 0.9 ms pass appeared in the single capture before the change and was absent after it. Live-combat visuals were otherwise unchanged. `r.TranslucencyLightingVolume=0` is now the default Mac profile only; the iOS profile is unchanged.

The world-rendering gate covers Mac and iOS and runs during `RedrawViewports` before drawing, so input-driven state transitions take effect in the same tick. It also keeps world rendering disabled when a menu is backgrounded. This behavior has Mac evidence. The current phone package is installed, but its fresh launch has not reached a menu test.

## Prepared after the historical 1870 phone build

The installed-package snapshot is [`device-installed-package.json`](../artifacts/ios-thermal/device-installed-package.json) and identifies executable SHA-256 `1870dcf9ef090b8d0125bd3265ebe5b59d82dff1723f3c800f9e330ef20b12ec` as the package installed during that historical run. Later package-verification output must not be used as proof that AEON received a package unless a matching installation record exists.

The sustained-execution entitlement source moved from the ignored build area to tracked `Config/IOS/Cinderline.entitlements`, with the project configuration pointing to that file. The prepared SDK 27 package has executable SHA-256 `d991e6462df26129beddb6293f00bb66efd3d9a0e83e2b6d9dfb4822bef3e12e`. Its earlier build, tests, packaging and signing passed, but a native startup race later crashed that package during module diagnostics. It was never installed on AEON and is no longer an installation candidate. At that stage, the installed physical package was `1870dcf9...`.

Schema 3 of the engine preparer addresses that race while preserving the working engine pieces. Its copy-on-write clone rebuilds `ApplicationCore`, `Core` and `Launch`, but the `Core` rule is limited to Cinderline native iOS ARM64 Development builds. `ApplicationCore` and `Launch` retain their existing Cinderline/iOS coverage. Other configurations continue using Epic's precompiled `Core`; this matters for Shipping, whose generated `Modules_Initialize` is empty. The `Core` change makes `IOSModuleDiagnostics` register through dyld's public add-image callback, uses a thread-local gate while registration is active and protects initialization with an atomic once. The stock engine remains untouched.

The preceding schema 2 build compiled all patched Core C++ and failed only when Epic's modified `MiMalloc.c` could not find its private `static.c`. Schema 3 preserves the exact stock native-iOS ARM64 Development `MiMalloc.c.o` in a stable clone-local `CinderlinePrecompiled` path, verifies SHA-256 `7d32cd0001b5daf4bd16a02ca75746d5d4b3972dbd3c77592861a266bc5cd94c`, and uses a compile skip macro so the rebuilt Core consumes that object instead of recompiling `MiMalloc.c`. The preserved object remains an SDK 26.1, minimum iOS 15 object and contains `mi_ext_set_os_tracking`.

Eight preparer tests pass. The schema 3 build then passed 29 actions in 112.85 seconds, packaging passed in 88.84 seconds, and the post-build preparer `--check` passed. All 16 game tests also passed.

The resulting thermal package has executable SHA-256 `4ae4305ad7318bb25c35f0d65dfe1a6687a3ae42fe7f70d3b21415d3e82bd573`, reports native iOS SDK 27 with minimum iOS 15, and passes strict signature verification. Its signature and provisioning profile authorize sustained execution, and its plist enables Game Mode. It was prepared, not installed on AEON, and is now historical evidence preserved in [thermal-package-verification.json](../artifacts/ios-thermal/thermal-package-verification.json). The historical bounded map-edge replacement is SDK 27 executable `7c603b00...`; its build, packaging, post-build check, strict signing, sustained entitlement/profile, Game Mode and three native iOS-on-Mac edge captures pass. Those captures are renderer evidence, not phone or thermal proof. At the time of those comparisons, the phone still had `1870dcf9...` installed.

## Acceptance boundary

The evidence proves that serious thermal pressure occurred and that the installed policy responded with lower frame caps and minimum quality. It does not prove that the phone cooled, that temperature stabilized, that the policy reduced power by a measured amount, or that a sustained match is thermally acceptable. It also provides no GPU utilization percentage and no per-pass cause.

After the current launch failure is resolved, the next physical run should begin from a cooled phone, record `ProcessInfo.thermalState`, use an allowed profile length, separate menu and ordinary gameplay windows, and capture a sustained trace without DeviceHub mirroring where possible.

## Apple references

- [ProcessInfo thermalState](https://developer.apple.com/documentation/foundation/processinfo/thermalstate-swift.property) supplies the system thermal-pressure state used by the policy.
- [Monitoring your Metal app's graphics performance](https://developer.apple.com/documentation/xcode/monitoring-your-metal-apps-graphics-performance) defines the Metal HUD metrics and explains the distinction between standard command-buffer GPU time and encoder timing.
- [Analyzing the performance of your Metal app](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app/) describes the Game Performance Instruments workflow needed for causal CPU/GPU analysis.

Primary evidence: [`quick-baseline-runtime.log`](../Saved/IOSThermal/quick-baseline-runtime.log), [`optimized-quick-runtime.log`](../Saved/IOSThermal/optimized-quick-runtime.log), [`optimized-quick-scene.png`](../Saved/IOSThermal/optimized-quick-scene.png), [`baseline-menu.json`](../artifacts/ios-thermal/baseline-menu.json), [`mac-comparison.json`](../artifacts/ios-thermal/mac-comparison.json), and [`device-installed-package.json`](../artifacts/ios-thermal/device-installed-package.json).

## Repeatable development capture

`cinder.gpuprofile 10` schedules a whole-frame GPU pass capture after ten seconds of scene warmup. Set `r.ProfileGPU.ShowUI 0` to write the report without opening a profiler window. Menu captures intentionally include Canvas rendering and no 3D scene; this is useful to verify the suppression gate. `cinder.profile 600` collects cadence and asynchronous engine timings after 30 warmups. These commands are Development-only and do not change the match save.

The final rendered Mac [transition check](../artifacts/ios-thermal/menu-transitions.json) recorded `disabled=1 state=menu`, `disabled=0 state=gameplay`, then `disabled=1 state=menu`. Base, battle and returned-menu screenshots were inspected. The run used an unsaved development fixture; it is not a physical touch test.

## Separate follow-up

The prepared `d991e646...` Development package exposed a startup race in Unreal's iOS module diagnostics while dyld was loading an accessibility bundle. Its symbolicated crash is `Saved/IOSThermal/native-module-trace-crash.ips`. The older `Saved/IOSThermal/profile-launch-crash.ips` belongs to an earlier physical package with SHA prefix `8defa1a25` and must not be attributed to `d991e646...`. The `d991...` evidence is historical and served as the precursor to the schema 3 repair.

## Native mobile renderer on Mac

The signed `4ae4305a...` thermal iOS binary completed five fresh-process launches through Epic's Designed for iPad wrapper on the Mac. All five launched without a new crash report and were stopped afterward. This validates the exercised startup path for that historical package, but five launches do not prove exhaustive race freedom. This is an iOS binary using the mobile Metal renderer on a Mac, not Simulator or phone evidence.

Three fresh menu processes held the requested 15 FPS at 14.80, 14.76 and 14.82 FPS. No SceneRender pass appeared; mean GPU times were 1.204, 1.312 and 1.043 ms. Nominal gameplay measured 29.70 FPS and 4.619 ms mean GPU time with a 2052×1536 scene. The forced-serious run measured 19.81 FPS and 2.493 ms mean GPU time, rendering the scene at 1437×1076 and spatially upscaling to 2052×1536 while keeping the HUD at full resolution. The menu and both battle screenshots were inspected. Geometry and text remain readable; the serious preset reduces only 3D scene resolution, shadows and detail. These short forced-policy runs do not establish physical temperature or power improvement. [Final native verification](../artifacts/ios-thermal/native-final/verification.json).

The startup repair, thermal-package snapshot and policy records are [`engine-startup-repair.json`](../artifacts/ios-thermal/engine-startup-repair.json), [`thermal-package-verification.json`](../artifacts/ios-thermal/thermal-package-verification.json) and [`prepared-package-policy.json`](../artifacts/ios-thermal/prepared-package-policy.json). Current package verification refers to the later map-border replacement and must not be used as proof that AEON received it.
