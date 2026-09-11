# Generated art assets

These two original assets were generated on 2026-09-11 with the built-in `image_gen.imagegen` tool, one generation call per asset. No input images, external artwork, CLI image generation, or pixel edits were used. The workspace PNGs are byte-for-byte copies of the generated originals.

| Workspace source | Actual dimensions | Intended use |
| --- | --- | --- |
| `RawAssets/Textures/T_CinderBasalt.png` | 1254 × 1254 | Repeating diffuse/albedo terrain color beneath units and structures |
| `RawAssets/UI/T_CinderBackdrop.png` | 1536 × 1024 | Menu backdrop, with controls over the quiet left half |

The terrain prompt requested 1024 × 1024. The built-in tool returned 1254 × 1254. Preserve the original here and configure a power-of-two texture size during Unreal import or cooking if required for mobile mipmaps and texture streaming. This source image is not a normal, roughness, height, or ambient-occlusion map.

The backdrop has a 3:2 aspect ratio. Fit it with a deliberate crop or letterboxing for landscape iPhone and iPad screens. Keep the right-side outpost visible and the left-side controls on a dark panel or scrim when needed.

## Inspection

Both images were visually inspected after generation. The terrain uses quiet charcoal and desaturated teal-gray basalt with fine cracks and dust. There are no units, buildings, bright resource-like deposits, text, or logos. The backdrop places the teal outpost and harvesters on the right, leaving dark low-contrast terrain and haze on the left. Its larger industrial shapes remain distinct at reduced preview size. No recognizable franchise design was observed.

Seamlessness was requested for the terrain. A read-only pixel check measured mean absolute RGB differences across wrapped edges at 10.95 horizontally and 8.61 vertically, compared with 8.12 and 8.76 for sampled interior neighbors, on an 8-bit 0–255 scale. These are consistent with modest edge transitions but do not prove invisible tiling. Validate repetition, scale, unit contrast, and mip behavior in the actual battlefield. No Unreal runtime or device acceptance is claimed by this document.

## Provenance

Terrain generated original:

`/Users/chirag13/.codex/generated_images/01a09045-8642-79c1-b9ca-35468130c33c/exec-7a785760-5d93-4a1b-8f5c-4a6e5e1fcfa9.png`

SHA-256:

`2f210f766cf91a3aadfaa21e1c773c8de5d91c07df3353f89ab2206b6663b190`

Backdrop generated original:

`/Users/chirag13/.codex/generated_images/01a09045-8642-79c1-b9ca-35468130c33c/exec-30883ed0-94bc-42c4-9265-7c6d180f1b5d.png`

SHA-256:

`1498f262683e0ebd7b223f9d56e5842d1a2a9e63d8ee3a7884adc21b3a873733`

The default generated originals remain in place. Project references must use the workspace sources or the imported Unreal assets.

## Exact terrain prompt

```text
Use case: stylized-concept
Asset type: seamless tileable game terrain diffuse/albedo texture for Cinderline, an original touch-first sci-fi RTS.
Primary request: Generate exactly one 1024 x 1024 square texture tile of basalt ground, viewed orthographically straight down. Subdued blue-green charcoal rock with small shallow cracks, fine mineral dust, and modest irregular natural grain. Texture should remain quiet beneath colorful small units and buildings.
Style/medium: restrained stylized hand-painted PBR base-color texture, coherent medium-scale basalt flakes with fine dust. Low local contrast and little large-scale variation.
Lighting/mood: perfectly even neutral diffuse illumination across the entire tile. This is albedo only, without lighting direction, ambient-occlusion pockets, specular shine, vignettes, or baked shadows.
Color palette: dark desaturated charcoal with a faint cool teal-gray cast; narrow value range, no bright or saturated patches.
Composition/framing: edge-to-edge ground at uniform physical scale, flat top-down texture. All four edges must match seamlessly when repeated in a grid. No central focal point or recognizable large landmark.
Constraints: no objects, rocks standing up, buildings, machines, characters, people, units, ore clusters, glowing cracks, emblems, text, logos, borders, watermarks, dramatic shadows or horizon. No large distinctive pattern that makes tiling conspicuous. Entire image is usable opaque terrain texture.
```

## Exact backdrop prompt

```text
Use case: stylized-concept
Asset type: landscape menu background key art for Cinderline, an original touch-first sci-fi RTS. Produce exactly one image, 1536 x 1024 landscape or 16:9 landscape.
Primary request: An original Cairn Assembly mineral-harvesting outpost on a basalt planet. Low, broad, angular industrial machines and buildings assembled from clearly separated chunky modules. Cool cyan and teal armor panels, charcoal steel structures, restrained narrow cyan status lights. A mineral processing structure has a squat central core, asymmetric service outriggers and distinct heavy mechanical modules. Nearby compact autonomous harvesters look functional and unlike humanoids. Distant warm amber fissures suggest heat beneath the planet's crust.
Style/medium: restrained stylized painted 3D game key art, clean architectural shapes, selective fine material detail, atmospheric perspective, easy to read at mobile screen size. Original industrial design, not photorealistic military hardware.
Composition/framing: wide cinematic landscape, elevated three-quarter view across the outpost. Concentrate the interesting architecture and all machines on the right half, especially the right two-thirds of that half. Keep the entire left half calm, dark and low contrast with negative space for menu controls, using only distant desaturated haze and quiet basalt foreground. Clear depth and a distant low horizon, no foreground machine intruding into the left menu region.
Lighting/mood: quiet dusk, soft cool ambient illumination with limited warm amber light far away. Readable silhouettes without exaggerated cinematic bloom.
Color palette: blue-green charcoal basalt; cyan and teal industrial armor on the right; small warm amber fissures in the far distance.
Constraints: no text, lettering, numbers, logos, watermarks, UI, borders, people, faces or recognizable franchise designs. Do not use StarCraft, Blizzard, Terran, Protoss, Zerg or other existing game designs. No floating islands, fantasy castles, giant glowing resource crystals or a dominant center object. Make the left half usable behind light menu text.
```
