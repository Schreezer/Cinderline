# Render and verification

## Commands

Run from the new episode directory after adapting its scripts and package.json:

```bash
python3 scripts/build.py
npm run check
npm run render -- --quality delivery --output renders/episode.en.mp4
```

`episode.en.mp4` is an example filename; use one consistent path throughout your scripts and manifest. Use the selected project's pinned HyperFrames CLI for snapshots, transcription, and preview. For a persistent local preview:

```bash
npx --yes hyperframes@0.8.63 preview --background
npx --yes hyperframes@0.8.63 preview --status
```

This is a local preview. `npm run publish` is an external publication action and is outside the current draft-only scope.

## Required evidence

1. Run full HyperFrames checks. Fix errors; inspect and explain remaining warnings. Sample actual scene boundaries and reveal times, not just the default opening frame.
2. Render the delivery export. Record the framework version and relevant render settings/log.
3. Inspect streams and duration using `ffprobe`. Confirm expected dimensions, frame rate, H.264 video, AAC audio, and requested duration.
4. Decode the entire file using `ffmpeg -v error -i renders/episode.en.mp4 -f null -`. Investigate errors.
5. Extract encoded frames from every scene and around important transitions. Inspect the contact sheet and close-ups for clipping, text accuracy, caption collisions, continuity, blank frames, and readable holds. A generated contact sheet is evidence only after someone actually inspects it.
6. Play the final mixed audio and review the full film for pacing and natural delivery. Transcribe the encoded MP4, compare the full script, and record omissions, repetitions, or ASR-only differences. Confirm the last word and ending are intact.
7. Compute SHA-256 of the final MP4, save `renders/verification.json`, and reference it in the manifest. Changing the MP4 invalidates the old hash and affected checks.

Example read-only inspection commands:

```bash
ffprobe -v error -show_format -show_streams -of json renders/episode.en.mp4
shasum -a 256 renders/episode.en.mp4
npx --yes hyperframes@0.8.63 transcribe renders/episode.en.mp4   --engine whisper --model small.en --language en --json
```

## Existing implementation and a trap

The [detox verifier](../episodes/2026-09-23-detox-job-interview/scripts/verify.py) checks a hardcoded 30-second MP4, decodes it, and extracts nine frames. It requires Pillow; the system Python used for it differs from the MLX voice environment.

Adapt filename, duration, sample timestamps, and expected output for each new episode. **The script rewrites verification.json and does not itself perform the final encoded ASR audit or visual review.** The existing report contains those later review results as additional fields. A rerun can erase them; preserve separate evidence and only mark each audit complete after actually doing it. Never copy a prior episode's “reviewed” flags into a new report.

## Ready means

The deliverable exists and matches its manifest hash; health claims have source support; all narration is present; captions and reveals follow measured speech; mounted and encoded visuals have been inspected; and material quality issues are resolved. Technical readiness and user editorial approval remain distinct. A user-requested rewrite should retain the old artifact and clearly label the revision.
