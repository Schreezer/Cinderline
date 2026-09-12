# Visual upgrade asset sources

Acquired 2026-09-11 for Cinderline's terrain presentation pass. These are third-party texture assets, not generated artwork.

## Gravel Floor 04

[Gravel Floor 04](https://polyhaven.com/a/gravel_floor_04) by Charlotte Baglioni, distributed by Poly Haven under **CC0-1.0**. The official asset page identifies its author and license; [Poly Haven's license statement](https://polyhaven.com/license) permits commercial use and redistribution of downloaded assets. The [CC0 deed](https://creativecommons.org/publicdomain/zero/1.0/) describes the public-domain dedication.

The rendered material preview was inspected in the official asset page before selection. The downloaded diffuse image was also inspected: fine gravel and dust, low-contrast broad variation, sparse tiny leaf fragments, and no obvious hard cast shadows or grass tufts. This supports readable units on an industrial battlefield. Its original color is warm sand; tint and desaturation belong in the game material so the downloaded source remains intact.

The [official metadata API](https://api.polyhaven.com/info/gravel_floor_04) reports a 2460 mm square capture, equivalent to **246 cm per tile**; the asset page rounds this to 2.5 m. File URLs and upstream checksums came from the [official file manifest](https://api.polyhaven.com/files/gravel_floor_04). API requests used the identifying user-agent `Cinderline-asset-research/1.0`.

All four source images are original, unmodified **2048 × 2048, 16-bit PNGs** in `RawAssets/Textures/VisualUpgrade/gravel_floor_04/`. No preview renders, third-party page text, or website logos are included in the game assets.

| Original filename | Material input | Interpretation |
| --- | --- | --- |
| `gravel_floor_04_diff_2k.png` | Base Color | RGB, sRGB |
| `gravel_floor_04_nor_dx_2k.png` | Normal | RGB tangent-space DirectX normal; linear, normal-map compression, green flip off |
| `gravel_floor_04_rough_2k.png` | Roughness | Grayscale R, linear |
| `gravel_floor_04_ao_2k.png` | Ambient Occlusion | Grayscale R, linear |

Use one consistent world-space tile scale across all maps. The original scale is 246 cm; an intentional artistic scale change should apply equally to every channel. Ground metallic is zero. Source resolution is 2K; runtime mip residency and appearance still require verification in Unreal.

## Download validation

Each file matched the byte count and MD5 published by Poly Haven. PNG headers independently confirmed dimensions and bit depth. SHA-256 hashes below identify the exact local source files; the accompanying `manifest.json` records download URLs, metadata sources, channel semantics, and acquisition time. `LICENSE.txt` retains the source and license references alongside the assets.

| Filename | Bytes | SHA-256 |
| --- | ---: | --- |
| `gravel_floor_04_diff_2k.png` | 23,242,841 | `1c07ed534bdfc7572e43d8a57c1e00285685c9134862331a23d1b904a0d5da88` |
| `gravel_floor_04_nor_dx_2k.png` | 24,364,042 | `215d17c4bf39273d1fdb1185d3c369b011137182cc88b52b44a5f78ffcffdee4` |
| `gravel_floor_04_rough_2k.png` | 6,518,222 | `9ca48f93f051c12eca19648d5efe8c2f6bfb6bb18475cdf3edab43cd46ab84ba` |
| `gravel_floor_04_ao_2k.png` | 7,812,901 | `51082bff9d1e94114dc79d91f53fcd3df63318c1d95bd7ced393244c0c3de5dd` |

No files were resized, recolored, repacked, or generated. Acquisition and source validation are complete; this document does not claim Unreal import, rendered quality, or device performance.

## Visual target revision — 12 September 2026

The final `M_CinderGroundV4` uses a separate coarse ground source, [Dry Ground Rocks](https://polyhaven.com/a/dry_ground_rocks), with matched 2K PBR maps at the source's 400 cm scale. Fine Gravel Floor 04 remains in the dedicated packed-earth road material. Downloaded sources remain unchanged.

Runtime review showed that fine gravel averaged into a smooth surface at the RTS camera. A first contrast revision still lost too much detail. The final material directly uses the coarse source's photographed color, with mild multiplicative terrain-mask tints. It removes the earlier clipping and broad basalt-cloud path, using six samples including the runtime mask. A second rotated albedo lookup is blended at 35 percent to reduce visible repetition; the primary projection retains 65 percent of color and all normal/roughness/AO detail.

Additional CC0 ground, model-finish and photogrammetry sources are recorded in [VISUAL_TARGET_PASS.md](VISUAL_TARGET_PASS.md), with original metadata and hashes beside their source files.
