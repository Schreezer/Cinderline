# Detox investigation, reviewed revision 3

35-second local English draft. Previous versions and their MP4 hashes are preserved. Queue lineage marks them superseded so the daily generator does not repeatedly resume the same old review.

## Watch and evidence

- `renders/detox-investigation.reviewed.en.mp4`
- `renders/verification.json`, `renders/encoded-contact-sheet.jpg`, `renders/encoded-frames/`
- `renders/check.json`, `renders/render.log`, `renders/ffprobe.json`, `renders/decode.log`
- `renders/transcript.json`, `public/audio/word-audit.json`, `renders/subtitles.en.srt`
- `SOURCES.md`, `REVIEW-NOTES.md`, `ASSET-PROVENANCE.md`

Corrected products/brands wording and evidence attribution; removed unsupported senna frequency and scale rebound. The consumer takeaway asks for evidence. Original Flow images and approved Qwen reference retained; newly generated continuous narration at original speed, gain +4 dB, 0.5-second onset.

Full MP4 decode passes. All 72 spoken words match encoded ASR after punctuation/case and numeric normalization. Twelve encoded samples inspected across all seven scenes. Human listening and user editorial approval are still pending; automated transcription does not judge naturalness.

This revision upgraded HyperFrames from 0.8.63 to 0.8.72 and passed the full check with no errors. Three reviewed warnings concern grouped Studio lanes and a numerical tween endpoint. Earlier project pins and videos are unchanged.

## Rebuild

`python3 scripts/build.py`, `npm run check`, `npm run render -- --quality delivery --output renders/detox-investigation.reviewed.en.mp4`.

`python3 scripts/verify.py` rewrites its report; redo encoded ASR and actual image review before restoring their completion fields. Nothing has been published or scheduled externally.
