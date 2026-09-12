# iOS readiness

Latest UI package, 2026-09-12: the mobile HUD now uses corner controls and contextual drawers. The freshly signed SDK 27 package passed nine native Designed for iPad on Mac layout states, keyboard pause/resume and five Unreal tests. See [MOBILE_HUD_PASS.md](MOBILE_HUD_PASS.md) and [current UI build evidence](../artifacts/mobile-hud/verification.json). Aeon touch acceptance remains open.


Updated **2026-09-12**. The player considers the desktop game playable and has moved iOS ahead of further desktop polish. The goal is an iOS build with touch controls and verified gameplay. The first installed iPhone build failed before engine startup; no successful phone rendering or gameplay is claimed yet.

## Designed for iPad on Mac: native launch verified

The player chose this route on 2026-09-12, with physical Aeon testing deferred until they return home. Xcode 27 exposes My Mac with the Designed for iPad/iPhone variant. Development signing includes this Mac, and Xcode assembly for that destination passed. Epic's `MakeMacNative.sh` wraps the real IOS/arm64, SDK 27 app without modifying its signed inner bundle. This is native iOS-on-Mac execution, not an iOS Simulator or a desktop Mac executable. See [Epic's guide](https://dev.epicgames.com/documentation/unreal-engine/test-on-mac-with-designed-for-ipad-in-unreal-engine).

The final package launches with the tracked configuration and no temporary graphics overrides. Screenshots verify the menu, skirmish terrain, Anchor, Drudges, ore, fog, field guide and pause overlay. Native keyboard input starts both guided training and a skirmish; arrow input advances the training camera objective. Mining increases ore during gameplay. Pause and field-guide return preserve the paused match. Resume advances the timer and resources again; screenshots show 00:31 / 978 ore while paused and 01:25 / 1724 ore after resuming. The test app was closed afterward. The final runtime has no assertion, handled ensure or fatal error in the captured launch/smoke window.

Automated coordinate clicks and drags remain unverified. The native automation tool reads this UIKit window and sends keyboard input, but coordinate clicks fail with `noWindowsAvailable`, even after raising the actual translocated app window. No successful native tap, hold-drag selection, construction/production button interaction or multitouch is claimed. These remain manual/physical-device acceptance tasks. The mobile control layout and instructions are rendered, but that is not touch-delivery proof.

The test used a 30 FPS cap. A 180-frame viewport sample at 2052x1536 measured 29.27 FPS with a 38.35 ms p95 frame interval. This is a short Mac run, not an iPhone performance or thermal measurement. Evidence: [verification](../artifacts/ios-on-mac/verification.json), [menu](../artifacts/ios-on-mac/menu.png), [skirmish](../artifacts/ios-on-mac/skirmish.png), [training](../artifacts/ios-on-mac/training.png), [field guide](../artifacts/ios-on-mac/field-guide.png).

Four issues were fixed during this pass:

1. **Engine staging paths.** UAT checks the project root before the engine root. Nesting the isolated engine in `Saved` staged its engine data at `Cinderline/Saved/EngineIOS27/Engine/...`, leaving the runtime without valid iOS shader-platform data. The isolated engine now lives at sibling `../CinderlineEngineIOS27`; both helpers reject nested engine paths. The final pak contains `Engine/Config/IOS/DataDrivenPlatformInfo.ini` and no former nested-engine entries. The original Epic installation remains unchanged.
2. **Unused Apple online service.** The stock iOS profile starts OnlineSubsystemIOS, whose MarketplaceKit distributor lookup calls a missing weak-linked symbol on iOS-on-Mac. Cinderline uses its own WebSocket subsystem. Project `Config/IOS/IOSEngine.ini` selects Null and disables OnlineSubsystemIOS; the runtime confirms Null initialization. Game Center and store purchases are not part of the current game.
3. **Mobile rendering.** UE 5.8's occlusion-feedback draw inherits the translucent fog pass's uniform buffer, whose feedback UAV is uninitialized. Disabling `r.OcclusionFeedback.Enable` in the iOS profile uses the standard occlusion path and stops the fatal shader-resource assertion. A separate LDR MSAA texture-type mismatch left world geometry black. The verified iOS baseline uses one sample and mobile anti-aliasing disabled. Native resolution is retained; restoring edge smoothing needs a separately verified rendering path, including Aeon testing.
4. **Incremental signatures.** The generated Xcode copy phase does not model individual cooked-data changes as signing inputs. A config-only package could report success while leaving a stale signature. Both physical iOS packaging helpers now quarantine the previous generated app before UAT, so Xcode recreates and signs the entire bundle. The raw executable and intermediates remain intact; simulator packaging is unchanged. The final physical package and native Mac inner bundle pass strict signature verification.

Older phone archives predate these runtime repairs. Use the current `Saved/Packages/IOS/Cinderline.app`, which matches the verified native Mac app's executable and cooked data. It is signed for Aeon but has not yet been installed or launched on that phone.

## Physical iPhone pass in progress

The player authorized the physical-device route after the simulator link diagnosis. The iPhone 17 Pro Max runs iOS 27.0, has Developer Mode enabled and previously exposed development services over the local network for the first installation. That connection has since dropped and must return before the replacement can be installed. A fresh signing bootstrap build with the existing Apple development team and automatic provisioning **succeeded**. Its unexpired development profile covers `com.cinderline.game`, matches a usable local signing identity, includes this phone and passes strict codesign verification. This supersedes the earlier signing blocker; no account login request is needed now.

The bootstrap was a signing check only and was not installed. The current game compiles and links for physical iOS, produces its target receipt and passes Xcode app assembly. The first cook completed all 565 packages and compiled 2,325 shaders. Final packaging and archive passed, and the 461 MB development app passed signature, profile/device coverage, native IOS platform and cooked-IoStore checks. CoreDevice successfully installed `com.cinderline.game` on the paired iPhone.

The first actual phone launch failed on thread 0 with `EXC_BREAKPOINT` in `___UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption_block_invoke`. The SDK 27-linked app was rejected by UIKit for using the legacy application lifecycle before Unreal reached engine initialization or rendered a frame. Apple documents the required migration in [TN3187: Migrating to the UIKit scene-based life cycle](https://developer.apple.com/documentation/technotes/tn3187-migrating-to-the-uikit-scene-based-life-cycle).

UE 5.8 source includes `bUseSceneBasedLifecycle`, an `IOSSceneDelegate` implementation and matching manifest generation. That setting is compile-time, however, and the installed precompiled `ApplicationCore` object contains `IOSAppDelegate` but no `IOSSceneDelegate` symbols. Enabling the flag or adding the manifest alone would create a lifecycle mismatch and is not a valid repair for this installed engine.

The native SDK 27 repair now uses an APFS clone at the sibling `../CinderlineEngineIOS27`; the stock Epic installation remains unchanged. For the Cinderline iOS target only, the clone rebuilds `ApplicationCore` and `Launch` from their shipped source with the scene-lifecycle setting enabled while retaining the other installed precompiled modules. The build passed all 33 actions, the resulting executable reports SDK 27 in `LC_BUILD_VERSION`, `IOSSceneDelegate` and its scene callbacks are present in the linked symbols, the generated iOS plist has the scene manifest, and direct Xcode 27 app assembly passed. UAT staging, IoStore packaging and the package helper all exited successfully. The replacement archive passed strict signature verification. The later native Mac pass above then exposed and fixed engine-data staging and mobile-rendering errors; only the current archive should be used for Aeon. There is still no successful replacement launch or gameplay result on the physical phone.

Rules regeneration initially failed because four cloned Microsoft online plugins reference GDK rule symbols whose SDK source is absent from the installed engine. All four plugin descriptors and every declared module were verified as Win64-only before that tree was moved to `CinderlineExcludedPlugins/Microsoft` inside the clone. [prepare-ios27-engine.py](../scripts/prepare-ios27-engine.py) reproduces the copy-on-write clone, validates the UE 5.8 scene source, applies only the two target-scoped module-rule changes, performs that guarded clone-local exclusion and records source hashes. `--check` verifies the prepared state without changing it. A separate Xcode 26.6 / iPhoneOS 26.5 fallback compiled all 26 actions and produced an executable whose `LC_BUILD_VERSION` reports SDK 26.5, but that route was paused before installation or launch when the native SDK 27 repair was selected. No engine binary is being relabelled. Evidence: [signing verification](../artifacts/ios-device/signing-verification.json).

UE 5.8 modern Xcode generation reads the bundle identifier from `[/Script/MacTargetPlatform.XcodeProjectSettings]` even for iOS. The tracked configuration now sets `com.cinderline.game` and automatic signing there. This Mac's signing team lives only in ignored `Config/UserEngine.ini` under the same section as `CodeSigningTeam`; no account identifier or certificate is committed.

The nested `ApplePostBuildSync` Xcode invocation failed its generated pre-action, while the same workspace built successfully when invoked directly. `-NoUBA` did not fix this: UE 5.8 always retains the UBA scheduler and its temporary-directory environment. The verified route scopes `UE_BUILD_FROM_XCODE=1` to standalone iOS compilation, then runs Xcode assembly and UAT cooking/packaging separately. This is Epic's own Xcode-to-UBT handoff flag; it is not exported into packaging. No engine code or platform markers were changed. The precise nested-process failure remains undiagnosed.

The SDK 27 UAT run completed successfully, but its existing `Saved/Packages/IOS/Cinderline.app` archive retained stale signed content: the fresh `Binaries/IOS/Cinderline.app` passed strict signature verification while the archived copy did not. UAT success alone therefore does not establish that a reused archive directory contains the fresh signed bundle. The physical `package-ios` helper now verifies the fresh app after UAT, copies it to a temporary archive with APFS copy-on-write, verifies that copy, quarantines any previous archive under `Saved/IOSDevice/Archives`, atomically replaces the destination and verifies it again. This archive step has run successfully on the SDK 27 output, and the resulting destination passed strict verification. It does not print certificate or account details. A caller-supplied archive directory is preserved, while `-clientarchitecture=iossimulator` uses the simulator archive path and skips this physical-device replacement. Designed-for-iPad packaging remains separate.

Mobile UAT commands now pass an allowlisted build environment because Xcode's error retry can print inherited variables. Saved diagnostics were scrubbed after an earlier retry exposed a credential. Do not copy raw signing or build-environment logs into published artifacts.

## ARM64 simulator target

The installed UE **5.8.2** build tools do define an ARM64 iOS Simulator target. The canonical architecture argument is `-Architecture=iossimulator`; it selects the iPhoneSimulator SDK, the `arm64-apple-ios…-simulator` compiler target and `WITH_IOS_SIMULATOR=1`. The earlier statement that UE 5.8 categorically replaced simulator support was too broad and has been removed. Target availability, installed engine objects, packaging, shaders and an actual launch must each be checked.

The selected test destination is **iPhone 18 Pro / iOS 27.0**, UDID `DAE4CD1A-DF2D-4086-A01A-9CB6FAEEB9F3`. Xcode **27.0 (27A266a)** and the **iPhoneSimulator 27.0 SDK** are installed. The first build probe did not boot it; the later installation retry below booted it successfully and shut it down afterward.

`./scripts/unreal.sh build-ios-simulator` invokes the simulator architecture with two compile jobs and skips deployment. The first direct UBT attempt is recorded in [ubt-preflight.log](../artifacts/ios-simulator/ubt-preflight.log): it exited 6 in 0.88 seconds with “Missing files required to build IOS targets.” No C++ compilation or launch occurred in that attempt.

## iOS 27 simulator installation retry

The player requested a simulator test again after the SDK 27 phone rebuild. The available **iPhone 18 Pro / iOS 27.0** simulator booted successfully on 2026-09-12. Installing the strictly verified scene-enabled phone app failed with CoreSimulator exit 4: its executable contains **IOS / arm64**, while this destination requires **IOSSIMULATOR / arm64**. The game has no installed simulator app container and was not launched.

A fresh simulator-target build with the current game sources and Xcode 27 passed all **22 compilation actions** but failed at linking, exit 6 after **86.34 seconds**, on the missing `PLCrashReporter` simulator archive. The current link response still contains **785 device-path engine/plugin objects** and 21 simulator project objects. The installed Core object remains **IOS / SDK 26.1**; it cannot be used in a simulator executable. The missing simulator archives are PLCrashReporter, libPNG 1.6.44, FreeType 2.14.1 and OodleNetwork 2.9.16. Their installed trees do not include the implementation sources needed to rebuild them. OodleNetwork could potentially be disabled, but that would not replace the device-only engine/plugin objects or the other required libraries.

The next simulator route is a matching complete UE source/dependency checkout and a build of the linked engine/plugin modules for `iossimulator`. The current GitHub CLI cannot access `EpicGames/UnrealEngine` (`Could not resolve to a Repository`). [Epic's source access instructions](https://dev.epicgames.com/documentation/unreal-engine/downloading-source-code-in-unreal-engine) require associating GitHub with the Epic account; account/repository access must be resolved before that checkout. No public mirror or platform relabelling was used.

The simulator was shut down after this blocked test; other simulator states were preserved. The verified physical-phone package remains unchanged. See [retry verification](../artifacts/ios-simulator/sdk27-retry/verification.json), [installation rejection](../artifacts/ios-simulator/sdk27-retry/install-rejection.txt) and [fresh build summary](../artifacts/ios-simulator/sdk27-retry/build-summary.log). No simulator gameplay or touch acceptance is claimed.

## Optional component recovery

The user selected iOS and pressed Apply. Launcher logs confirm an actual `TagChange` adding `platform_IOS` and a queue length of one, but no UE installer started. Repeating Apply skipped the existing queue entry. A graceful restart produced `FInstallPatcherItem::Install: was unable to create Install Task` with existing-install-modification reported as zero, then emptied the queue. The Downloads panel also logged an invalid-route error. This is an observed Launcher failure; the precise internal cause is not established.

Recovery uses Epic's bundled **BuildPatchTool** and the exact UE 5.8.2 local installation manifest. A one-file probe downloaded and verified the real iOS target receipt into a staging directory. The selected iOS component contains **15,223 files / 4,202,991,029 bytes**. An iOS-only derived manifest limits the transfer to those records; every staged and installed file is checked against the original manifest's size and SHA-1 before success is recorded. The installer completed successfully. All **15,223 files** passed size and SHA-1 verification against the original manifest before installation and again afterward; all were missing, so no existing engine file was overwritten. After verification, the Launcher item was backed up under `Saved/IOSLauncherReceiptBackup.item`, and only `InstallTags` was updated to include the user-selected `platform_IOS`. The original Epic manifest was not modified. See [payload verification](../artifacts/ios-simulator/payload-verification.json) and [component record reconciliation](../artifacts/ios-simulator/launcher-component-reconciliation.json).

Evidence: [Launcher queue diagnosis](../artifacts/ios-simulator/launcher-queue-diagnosis.txt), [probe](../artifacts/ios-simulator/payload-probe.log), [component transfer](../artifacts/ios-simulator/payload-ios-only.log), [recovery](../artifacts/ios-simulator/payload-recovery.log).

## Actual simulator build result

After component recovery, `./scripts/unreal.sh build-ios-simulator` reached real compilation using Xcode 27's iPhoneSimulator SDK and `-target arm64-apple-ios15.0-simulator`. **All 22 compile actions, including the PCH, passed.** The build then failed at linking after **62.84 seconds**, exit **6**:

```text
ld: library 'ThirdParty/PLCrashReporter/lib/lib-Xcode-16.2/iOS/Simulator/libCrashReporter.a' not found
```

The exact installation manifest also lacks the referenced OodleNetwork 2.9.16 simulator archive, FreeType 2.14.1 simulator archive and libPNG 1.6.44 simulator archive. More fundamentally, the link response file points at `IOS/arm64` precompiled engine objects. `vtool` reports the game's fresh object as **IOSSIMULATOR / SDK 27.0**, while the installed Core object is **IOS / SDK 26.1**. An isolated object-only linker diagnostic confirms:

```text
ld: building for 'iOS-simulator', but linking in object file (...) built for 'iOS'
```

This is an engine-distribution blocker, not a game C++ error or a development-signing failure. Downloading the optional component fixed the first missing-payload error, but this distribution has no simulator-built engine object set. Continuing requires a simulator engine build with its missing dependencies; adding the four libraries alone cannot fix the device-only Core objects. No simulator app was produced, installed or launched, and no simulator gameplay/touch result is claimed. The simulator was left shut down.

Evidence: [build log](../artifacts/ios-simulator/ubt-after-payload.log), [Mach-O platforms](../artifacts/ios-simulator/macho-platforms.txt), [object-link diagnostic](../artifacts/ios-simulator/core-object-link-probe.log), [verification and source hashes](../artifacts/ios-simulator/verification.json).

## Packaging and device signing are separate

The simulator route must not inherit a physical-device signing blocker. The modern IOS run-only Xcode project currently defaults to `SDKROOT=iphoneos` and `SUPPORTED_PLATFORMS=iphoneos`; a simulator package needs verified iPhoneSimulator overrides and signing disabled. UAT accepts `-platform=IOS -clientarchitecture=iossimulator`; deployment has a `simctl install` branch, while the normal RunClient path is device-oriented. Use simulator tools for the final install/launch and verify the executable's Mach-O platform before claiming simulator proof.

The installed manifest lacks several simulator archives referenced by the link command, and the engine's architecture-specific precompiled objects are under `IOS/arm64`, with none under `IOS/iossimulator`. Also, `IOSTargetPlatformSettings.cpp` declares `SF_METAL_SIM` without adding it to targeted shader formats. These are source/manifest findings to evaluate with the actual build; a target declaration alone does not establish a working package.

Designed for iPad on Mac and installation on the paired iPhone remain separate routes. The previous signing audit in [artifacts/ios-readiness](../artifacts/ios-readiness/) found a profile/certificate mismatch, and a disposable automatic-provisioning attempt failed with an unavailable Xcode team account. Those historical findings apply to development-signed device/iOS-on-Mac packages, not to an unsigned simulator build. The subsequent physical-device signing retry above resolved this blocker.

## Touch and lifecycle preparation

Controls retain tap-to-select/command, immediate drag-to-pan, hold-then-drag box selection, two-finger pan/pinch and touch placement. This pass fixes stationary holds issuing terrain actions, placement holds arming selection, stale multitouch state and secondary HUD touches moving the camera.

Backgrounding pauses an active match and clears pending gestures. Returning leaves the pause overlay in place until the player chooses Resume. A manual pause stays paused. Native safe-area metrics augment compact HUD margins for cutouts and the home indicator, including placement-status labels near screen edges. Touch hints explain panning and hold-drag selection. Desktop Option-drag and the 20-entry queues are preserved.

Focused lifecycle/gesture tests use the real Unreal controller and delegate callbacks in a transient world. Those tests establish controller state behavior; they do not prove native UIKit input delivery or physical touch usability.

## Build and test sequence

The current request is to test the native iOS app in Designed for iPad mode on this Apple Silicon Mac, then test on Aeon when the player returns home. The simulator still requires a complete simulator engine build. On this Mac:

1. Keep the Mac editor module current with `./scripts/unreal.sh build -MaxParallelActions=2`; it performs the cook.
2. Run `./scripts/prepare-ios27-engine.py`, then `./scripts/prepare-ios27-engine.py --check`, before an SDK 27 device build. Compile/link physical ARM64 with the prepared clone, `UE_BUILD_FROM_XCODE=1`, `-SkipRulesCompile -ForceRulesCompile` and two compile jobs. This rebuilds the two lifecycle entry modules and writes the Cinderline target receipt without modifying the stock engine.
3. Run `./scripts/unreal.sh package-ios -skipbuild -target=Cinderline -clientarchitecture=arm64 -unattended`. The prepared engine must remain outside the project directory so UAT stages engine data under `Engine/` rather than game content paths. UAT stages and assembles/signs the final app through Xcode outside UBA, then the helper replaces the archive from the strictly verified fresh `Binaries/IOS` app and quarantines the prior archive. Modern Xcode signing reads the ignored local `Config/UserEngine.ini` team. Verify the final app bundle identifier, profile, code signature, cooked content and **IOS** Mach-O platform before installation.
4. First test the native iOS wrapper on this Mac with `./scripts/unreal.sh package-ios-on-mac -skipbuild -skipcook -iostore`. When the player returns home, install and launch the current physical app on Aeon. Capture the menu and battlefield, then exercise guided training, selection, commands, production, construction, panning, pause/resume and background/foreground behavior. Check safe areas, both landscape orientations and queue pages. Physical multitouch quality and thermal performance need physical evidence.
5. The simulator remains a separate follow-up: obtain simulator-compatible engine objects and missing dependencies, rerun `build-ios-simulator`, cook with the simulator architecture and verify an **IOSSIMULATOR** executable before installation. Never relabel physical-iOS objects or count the device launch as simulator proof.

The first cook showed that `-CoreLimit=2` did **not** restrict shader workers on this Mac: Unreal started seven. That run was stopped. A direct `DevOptions.Shaders.NumUnusedShaderCompilingThreads=9` override on this 10-core host was verified by both `Using 1 local workers` in the log and a single live ShaderCompileWorker. Keep one cook process and bounded shader compilation for subsequent packaging.

## Evidence tracker

| Stage | Status |
| --- | --- |
| Mac compilation of shared touch/HUD changes | Passed; final placement-label correction rebuilt in 11.61 seconds, two compilation jobs |
| Headless gesture and lifecycle regression | All four Unreal integration paths passed, zero errors/warnings |
| iOS platform payload | Installed and verified: 15,223 files / 4,202,991,029 bytes; Launcher component record reconciled |
| Simulator game C++ compilation | Passed all 22 compile actions, including PCH, with the iPhoneSimulator SDK |
| Simulator linking | Failed: missing simulator dependencies and physical-iOS engine objects |
| Cook/package/archive | Passed: 565 cooked packages; packaging and archive succeeded, final helper exit 0 |
| Development signing | Full game signature, profile/device coverage and native IOS executable verified |
| Designed for iPad launch | Passed with the final tracked settings; rendered menu/skirmish, keyboard camera input, training, guide and pause verified; tap/drag/multitouch still unverified |
| ARM64 iOS Simulator | Real target/build verified; no executable, installation, launch or touch test because engine linking fails |
| Physical iPhone installation and launch | First SDK 27 installation passed, then launch failed in UIKit's no-scene-lifecycle runtime check before engine startup. The scene-enabled SDK 27 replacement builds, packages and passes strict archive verification with both required modules rebuilt; the development connection dropped before replacement installation, so no replacement launch exists yet |
| Physical multitouch, safe areas and a skirmish | Not demonstrated |

Keep these stages separate. A Mac build or headless gesture test cannot establish iOS gameplay.
