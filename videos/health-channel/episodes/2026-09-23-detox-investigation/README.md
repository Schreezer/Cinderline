# The Detox Investigation (detox episode, revision 2)

A 27-second English illustrated short. Local draft only; nothing uploaded. The earlier job-interview episode is untouched.

- Video: renders/detox-investigation.en.mp4 (SHA-256 61853ff7715b7033ecda7635415b033ee8b5feaae6fdae23326b3d8118f458bb)
- Evidence: renders/verification.json, renders/encoded-contact-sheet.jpg, renders/encoded-frames/, renders/encoded-frames-ocr.txt, renders/check.log, renders/render.log
- Subtitles: renders/subtitles.en.srt

Build: `python3 scripts/build.py`, `npm run check`, `npx --yes hyperframes@0.8.63 render --quality delivery --output renders/detox-investigation.en.mp4`, `python3 scripts/verify.py`.

Production: six new Nano Banana 2 stills (public/flow/*-v1, selected copies in public/art) plus the reviewed infographic reused from the job-interview episode as 07-organs. Narration: approved Qwen Base reference, seed 42, gain +4.5 dB only, 0.5 s offset. Scene cuts and captions follow measured word timings (public/audio/transcript.aligned.json).

Review status: automated checks, OCR of generated lettering and encoded frames, and a full encoded-audio word audit all pass. **Composition and character consistency have not been visually reviewed by a person yet.** Known minor art oddities: small "Pill Jar"/"OintmenT" labels on the 2009 corkboard, stray keypad-like digits in the straightener shot, a few garbled letters on background tea boxes in the final shot.
