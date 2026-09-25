# The detox job interview

A 30-second English illustrated health short for FitSnap. Local draft; nothing uploaded.

## Watch

`renders/detox-job-interview.en.mp4` is the delivery export. `renders/encoded-contact-sheet.jpg` shows samples from the encoded video. `renders/subtitles.en.srt` holds phrase captions. `renders/verification.json` records stream/decode checks and narration audit.

## Build

Run `python3 scripts/build.py`, then `npm run check`. After checking mounted frames, render with `npm run render -- --quality delivery --output renders/detox-job-interview.en.mp4`. Run `python3 scripts/verify.py` to decode and inspect output metadata and encoded samples. Use the pinned HyperFrames version in package.json.

## Production choices

Five original Google Flow / Nano Banana 2 illustrations with integrated lettering; the office-team illustration returns as a sixth shot. Scene timing follows the approved Qwen synthetic voice. The raw voice lasts 26.32 seconds at original speed; a 0.4-second entrance and final held payoff make a 30-second export. No cloned human voice, lip sync, or AI speaking person.

Evidence and brew illustrations were regenerated after review to simplify composition and preserve a clear caption band. Original candidates, prompts, downloads, and generation metadata remain in public/flow; final selected artwork is in public/art.

The narrator describes normal organ functions and the lack of convincing evidence for commercial wellness detox cleanses. SOURCES.md documents the claims and their limits. This is not advice for treating poisoning or organ disease.
