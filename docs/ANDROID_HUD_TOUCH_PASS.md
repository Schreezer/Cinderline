# Android HUD touch correction

Updated 2026-09-15. Status: **source correction and Android package locally verified; physical runtime pending**. This is focused HUD maintenance after the P1 stop point. It does not reopen P1 or authorize P2 work.

## Report, cause and correction

The player reported that tapping the highlighted Train control during tutorial lesson 4 moved the selected Drudge on Android instead of opening training.

The confirmed root cause was a coordinate-space mismatch. Unreal translated and clipped the HUD canvas for the viewport safe zone before `DrawHUD`, while Cinderline registered hit regions against raw viewport input coordinates. Android gameplay retained that engine transform even though the existing compact layout already applied the platform insets explicitly. A drawn control and its raw touch hitbox could therefore disagree.

`DrawHUD` now temporarily removes the engine canvas translation so drawing and registered hit regions use the same full-viewport coordinates, then restores it on return. The existing explicit platform-inset layout policy is unchanged. The removal follows Unreal's guard: only nonzero left/top canvas padding is popped, so right/bottom-only padding is never passed to an engine pop that cannot remove it.

## Local evidence

- The valid pre-fix report [`20260915T065443Z-89543`](../Saved/Automation/Integration/20260915T065443Z-89543/index.json) passed 14 of 15 tests. `HUDTouchSafeZone` failed five independent full-viewport geometry assertions: the transformed canvas reported 884 by 406 instead of the 956 by 440 viewport, and the explicit inset-policy check failed. This proves the coordinate mismatch; it did not reproduce the player's exact worker movement.
- The after report [`20260915T065727Z-90165`](../Saved/Automation/Integration/20260915T065727Z-90165/index.json) passed all 41 tests, including `HUDTouchSafeZone` through an actual Unreal canvas safe-zone transform and raw-coordinate input coverage.
- Two Mac mobile/debug-safe-area captures were individually reviewed: highlighted Train and the following training catalogue/Drudge portrait prompt are legible and unclipped. [`preview-verification.json`](../artifacts/android-hud-touch/preview-verification.json) records their hashes, matching module and unchanged protected player state. These scripted Mac renders are visual evidence, not Android touch proof.

## Android package and remaining verification

- Android **0.1.1**, version code **2**, package `com.cinderline.game` built successfully at [Cinderline-Android-arm64.apk](</Volumes/Codex Storage/Cinderline/Android/packages/previous/543762f5d40f-Cinderline-Android-arm64.apk>). APK Signature Scheme v2 verifies, its signing-certificate SHA-256 remains `1430ffabfd20b590dc334486f80f8423a402369e60db88a4142734ce128d1541`, matching the previous package, and `zipalign -c -P 16 -v 4` passes. The [combined verification receipt](../artifacts/android-hud-touch/verification.json) records APK SHA-256 `543762f5d40f26ca2abe467493ebecc162562d2ad9bfd533bccc900d221fa9c5`, 274,457,578 bytes, and matching focused source hashes in the packaged workspace.
- This development APK includes the current local P1 source (save 13/protocol 10); no public server deployment or P0/P1 physical acceptance is implied.
- Physical Android launch and the original tutorial Train tap, plus ordinary tap, drag and pinch behavior: **pending**.
