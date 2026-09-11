# Original audio cues

Eight original procedural cues are stored in `RawAssets/Audio/`. They use sine/chirp synthesis, seeded filtered noise, damped resonances, and smooth fades. No downloaded audio or source recordings are used.

| File | Duration | Intended role |
| --- | ---: | --- |
| `UI_Click.wav` | 65 ms | Soft mechanical interface tap |
| `Order_Ack.wav` | 180 ms | Two ascending order-confirmation tones |
| `Order_Invalid.wav` | 240 ms | Muted descending rejection cue |
| `Unit_Ready.wav` | 320 ms | Three light rising notes |
| `Building_Ready.wav` | 560 ms | Lower, longer structure-completion cue |
| `Weapon_Pulse.wav` | 110 ms | Compact electronic weapon discharge |
| `Impact.wav` | 170 ms | Low thud with damped metallic resonance |
| `Explosion.wav` | 580 ms | Filtered noise burst with falling bass body |

All files are mono, 48 kHz, signed 16-bit PCM WAV. Individual peaks are below 0.85, endpoints are silent, and measured DC offset is below one PCM quantization step. High-frequency content is restrained through filtered noise and a final low-pass filter. Unit and building readiness use different registers, timing, and durations.

## Regeneration and verification

From the repository root, using Python 3.10 or later and only its standard library:

```sh
python3 scripts/create_audio_assets.py
python3 scripts/create_audio_assets.py --verify-only
```

The generator uses fixed seeds. `RawAssets/Audio/manifest.json` records each cue's duration, format, peak, RMS, DC offset, byte count, and SHA-256 hash, plus the generator hash. Verification reads the WAV files back, checks their signal metrics, and compares them byte-for-byte with newly synthesized data without rewriting files.

## Validation limits

The WAV generation pass established numerical and format validation only. It performed **no playback, listening review, Unreal import, or in-game mixing test**. File metrics do not establish listening quality. Multiple simultaneous cues can exceed the headroom of an individual file; runtime mixing and listening review remain necessary.

## Unreal import

`scripts/unreal_audio_assets.py` has no asset-import side effects when imported as a Python module. Inside Unreal Editor, explicitly call:

```python
import unreal_audio_assets
report = unreal_audio_assets.import_audio(project_root, destination="/Game/Art/Audio")
```

The helper verifies source hashes and WAV metadata before creating `AssetImportTask` objects with `SoundFactory`. It imports eight `SoundWave` assets, disables looping, checks imported channels/sample rates/duration, and saves them. Missing source files or mismatched metadata fail the explicit import. The helper does not play audio.

The import APIs were checked against installed UE 5.8 source: `AssetImportTask.h`, `SoundFactory.h`, `SoundWave.h`, `SoundBase.h`, and PythonScriptPlugin's editor-property access implementation. `num_channels`, `imported_sample_rate`, `sample_rate`, and `duration` are editor-visible properties. Python syntax, module execution outside Unreal, and validation of all eight sources passed. Actual Unreal import and engine compilation subsequently passed; see `artifacts/unreal-asset-import-results.json` and `artifacts/unreal-build-results.txt`.

## Runtime API

The optional `UCinderAudioSubsystem` is a `UGameInstanceSubsystem`. Callers include `Presentation/CinderAudioSubsystem.h` and use:

```cpp
UCinderAudioSubsystem::Play(this, ECinderCue::Order_Ack);
UCinderAudioSubsystem::Play(this, ECinderCue::Impact, 0.7f);
```

The eight enum values match the WAV stems. The subsystem retains loaded `USoundBase` assets through a transient `UPROPERTY` cache, using `/Game/Art/Audio/<Cue>.<Cue>` object paths. `DefaultGame.ini` includes `/Game/Art` in cook inputs, covering these runtime string paths. Missing assets are quiet and checked once per game-instance lifetime; start a fresh game instance after importing previously missing assets.

Playback quietly returns for missing contexts/game instances, disabled audio, commandlets, dedicated servers, unavailable audio devices, and invalid or zero volume. It calls `PlaySound2D` with a conservative cue gain and a caller multiplier clamped to 0–1. It does not change simulation state.

| Cue | Minimum interval | Mix gain |
| --- | ---: | ---: |
| UI_Click | 45 ms | 0.40 |
| Order_Ack | 120 ms | 0.50 |
| Order_Invalid | 250 ms | 0.50 |
| Unit_Ready | 200 ms | 0.60 |
| Building_Ready | 500 ms | 0.65 |
| Weapon_Pulse | 70 ms | 0.16 |
| Impact | 100 ms | 0.20 |
| Explosion | 250 ms | 0.30 |

Throttling applies per cue across the game instance and uses monotonic time. It limits repeated combat submissions but does not replace a final mix/concurrency review. Readiness/combat/UI triggers belong to presentation callers.

Run the explicit `cinder.audio` console command, or call the subsystem's `LogStatus()`, to report loaded cue count and requested, played, throttled, missing, and unavailable counts. The command produces no sound and the subsystem logs nothing automatically. Requests without a game instance cannot contribute instance counters. **Played counts valid `PlaySound2D` submissions; it does not prove audible output or listening quality.**

## Combat dispatch

The battlefield maps new Weapon, Impact and Death events to `Weapon_Pulse`, `Impact`
and `Explosion`. Healing has its own visual treatment and no sound cue yet. Event IDs
are consumed once, including hidden and offscreen events, so panning the camera or
revealing fog later cannot replay them. Match start, menu return and successful load
snapshot the cursor and readiness statistics. Playback requires both event-time and
current endpoint visibility and, when a local viewport exists, an on-screen location.

Each adapter update coalesces repeated events into at most one request per combat
cue: destruction, firing and impact. A large volley cannot crowd out another cue.
Existing per-cue intervals apply across the game instance.
`cinder.combat` reports consumption and filtering; `cinder.audio` additionally reports
requests and actual playback submissions for each cue. This is a bounded first mix;
stereo positioning and a listening review remain later work.
