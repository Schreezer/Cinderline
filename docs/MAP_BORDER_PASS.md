# Map border and bounded overscroll pass

Status on 2026-09-12: **implemented and verified on Mac and through the signed SDK 27 native iOS-on-Mac renderer; physical touch and AEON validation remain pending**.

## Delivered behavior

- `CinderCamera` now bounds both its target and current interpolated position. The allowed overscroll margin is 15% of the smaller visible ground-footprint span, clamped to 72–360 world units. Maximum zoom remains available at edges and corners.
- A drag that saturates at a boundary no longer stores additional off-map travel. Reversing the drag moves the camera back immediately.
- The terrain ends at the exact 0–4800 world bounds on both axes. Four static, unlit, opaque black planes cover the exterior beyond those edges; they are created with the map and are not rebuilt every frame.
- Existing off-map command taps remain rejected. Ground projection stays unbounded while dragging so a drag started over the black exterior can return to the playable map.
- Fog behavior and the approved mobile menu/HUD geometry are unchanged.

## Mac verification

- The Mac Development build passed eight actions in 34.41 seconds.
- All 17 game tests passed. Border coverage includes geometry creation, material properties and reset behavior.
- The camera automation passed 96 aspect-ratio, zoom and edge/corner cases, including repeated saturated drags, reversal and interpolation.
- Seven rendered Mac captures were inspected across cardinal edges, corners, aspect ratios and zoom levels. Each sampled exterior point was exact black (`RGB 0,0,0`), and the captures showed a straight boundary with the expected fog and HUD still present.

Evidence: [verification manifest](../artifacts/map-border/verification.json), [camera automation](../artifacts/map-border/camera-automation.json), and the associated PNGs and runtime logs in [`artifacts/map-border`](../artifacts/map-border/).

## SDK 27 package and native rendering

- The physical-iOS build passed 44 actions in 157.72 seconds. Packaging passed in 93.72 seconds, and the post-build schema 3 check passed.
- Executable SHA-256 `7c603b0041aa5d268294b8acea96e64d29195864500923b978221d6a9b7c0a31` reports native iOS SDK 27 and passes strict signing. Its signature and provisioning profile authorize sustained execution, and `LSSupportsGameMode` is enabled.
- Three fresh native iOS-on-Mac runs captured the southwest edge at distance 1650, west edge at 650 and northeast edge at 3200. All three 2052x1536 captures were visually inspected; each shows a straight black exterior with retained fog and HUD, and each exterior probe returned exact black (`RGB 0,0,0`). No new crash reports appeared, and all owned Cinderline processes were stopped.
- An earlier northeast attempt entered the background before its two-second world timer fired. It produced no capture or crash report and was stopped after 90 seconds. That run is inconclusive; the fresh foreground northeast retry passed.

Evidence: [native verification](../artifacts/map-border/native-verification.json) and [package policy](../artifacts/map-border/package-policy.json).

## Acceptance boundary

This evidence covers controlled Mac rendering, direct camera automation and the native iOS mobile renderer running through Designed for iPad on the Mac. It does not prove physical touch behavior, iPhone rendering or sustained phone thermal performance. Pixel probes cover known exterior sample points rather than every exterior pixel, and the fog checks do not substitute for a complete human multiplayer match.

The replacement package and its three native captures pass, but it has not been installed on AEON. The phone remains on executable `1870dcf9...` and was untouched during this pass. The earlier thermal package `4ae4305a...` retains its separate five-launch evidence at [thermal-package-verification.json](../artifacts/ios-thermal/thermal-package-verification.json).

When AEON returns, the next physical acceptance run should combine boundary dragging and black-border inspection with the cooled-menu and sustained-match thermal checks. Persistent public online play, the dedicated HUD redesign and authored Unreal Landscape follow that device work.
