# Flip it. Then pick it.

38.7-second English vertical health explainer. 1080×1920, 30fps.

- Final with approved Qwen voice: `renders/flip-it-then-pick-it.qwen-v2.en.mp4`
- Captions: `renders/subtitles.qwen-v2.en.srt`
- Original export retained: `renders/flip-it-then-pick-it.en.mp4`
- Evidence and media provenance: `SOURCES.md`
- Editable six-scene composition: `index.html` and `compositions/frames/`
- Creative brief and narration: `BRIEF.md`, `STORYBOARD.md`, `SCRIPT.md`

## Rebuild

`python3 scripts/build.py` regenerates the compositions from the approved assets and measured `public/audio/full-qwen-v2/timing.json`. `npm run check` runs the visual checks. `npm run render -- --quality delivery --fps 30 --output renders/flip-it-then-pick-it.qwen-v2.en.mp4` exports.

`scripts/narrate_full_v2.py` uses Qwen3-TTS-12Hz-1.7B-Base BF16 with the user-approved synthetic VoiceDesign BF16 reference from `public/audio/voice-examples-v3/qwen-designed-raw.wav`. The full script is one continuous 38-second generation. Constant +3.29 dB gain keeps the measured true peak at approximately -1.5 dBTP; no speed, pitch, compression, or inserted-pause processing. Python dependencies are frozen in `requirements.txt`.

`scripts/prepare_full_v2.py` audits the 110 spoken words against the script, restores caption punctuation, normalizes gain, and derives scene boundaries from measured word timestamps. Regenerating speech requires fresh transcription and updating semantic reveal timing before rebuilding.

HyperFrames was upgraded from 0.8.60 to 0.8.61. The final check passed runtime, layout, motion, and all 59 contrast checks, with one existing caption-track structure warning. Six mounted scenes were inspected at their midpoints. `scripts/verify_full_v2.py` checks the encoded MP4 dimensions, duration, frame rate, audio duration, full decode, and extracts encoded frames for visual review. Evidence: `renders/qwen-v2-verification.json` and `renders/qwen-v2-encoded-contact-sheet.jpg`.

Flow generation used `GFLOW_CHROME_PATH=/Applications/Helium.app/Contents/MacOS/Helium` with the existing authenticated profile in the parent starCraft workspace. Generation metadata and original download retained under `public/flow`.

No publishing or recurring automation was activated. Music lookup was unavailable; the export uses narration and restrained bundled sound effects.
