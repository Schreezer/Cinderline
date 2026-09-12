# Model surface-finish source

Downloaded from Poly Haven's official API on 12 September 2026 for the Cinderline visual-target model material.

- Asset: `blue_metal_plate`
- Asset page: https://polyhaven.com/a/blue_metal_plate
- API response: https://api.polyhaven.com/files/blue_metal_plate
- Author: Rob Tuytel
- License: CC0, https://polyhaven.com/license
- API credit: Powered by Poly Haven, https://polyhaven.com
- Resolution and format: 1K JPG

| File | Map | Poly Haven MD5 | SHA-256 |
| --- | --- | --- | --- |
| `blue_metal_plate_diff_1k.jpg` | Diffuse | `a906d0e554596fb76c6969f2f644c5e6` | `a0162bffce47d4a35613a12af22571b28c18412dc5805cbb69eac343554ef750` |
| `blue_metal_plate_nor_dx_1k.jpg` | DirectX tangent normal | `b2857e3ba6a817ae97d4e6e1c8231d56` | `578d18a2f105b78b23b858e638588f39576ff1d0c7a27fea6b71e76385dc08ec` |
| `blue_metal_plate_rough_1k.jpg` | Roughness | `76c3911d8ad2416e4db3104a2ef831c7` | `37168cf57144db28dd0743dab47fbebc09147fac919d5e2b5610b069c0b13a46` |

`polyhaven-blue_metal_plate-files.json` is the saved API response used to resolve the three official download URLs. Its SHA-256 is `eef58641c5e0a42d4e5bb158ba8a665e3a5691f64b6951abc4bbf8f73ab1ba9a`.

The Unreal augmentation uses all three maps at low strength. It preserves Cinderline's material tint and uses the diffuse map only for small value and wear variation rather than its source blue color.
