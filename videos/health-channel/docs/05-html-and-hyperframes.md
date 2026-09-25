# HTML animation and caption synchronization

## Start from framework guidance

For composition work, read `/Users/chirag13/.agents/skills/hyperframes/SKILL.md`, then the applicable core, animation, keyframes, registry, and CLI skills it routes to. If unavailable, check the corresponding `/Users/chirag13/.codex/skills/` directory. Read the episode's `AGENTS.md` too. These documents explain the runtime contract; this guide records our production choices.

The [detox package.json](../episodes/2026-09-23-detox-job-interview/package.json) pins HyperFrames 0.8.63. Use the selected project's pin, not an arbitrary latest release. Verify new CLI options with that version's help. Keep fonts, GSAP, images, and audio local for reproducible rendering.

## Composition structure

- `index.html` owns canvas size, overall duration, scene hosts, narration, captions, sound effects, and brand elements.
- `compositions/frames/NN.html` contains each scene as a framework sub-composition with a unique composition ID and its own duration.
- Hosts use `data-composition-src`, `data-start`, `data-duration`, and canvas dimensions. Timed visuals use `class="clip"`. `data-track-index` organizes Studio lanes; it is not a substitute for timing.
- Register a paused GSAP timeline for each composition on `window.__timelines[compositionId]`. Initialize the dictionary if needed. Arbitrary child timelines added inside a parent must be able to advance when the parent seeks.
- Motion must be deterministic and seekable. Use timeline positions, not wall-clock timers, random values, or network requests during playback.
- Keep the narration in a separate `<audio>` element with measured start and duration. Optional video visuals remain muted.

Read the [root composition](../episodes/2026-09-23-detox-job-interview/index.html), [first scene](../episodes/2026-09-23-detox-job-interview/compositions/frames/01.html), and [builder](../episodes/2026-09-23-detox-job-interview/scripts/build.py). The builder writes HTML, caption groups, and SRT. It hardcodes art names, phrase word ranges, scene boundaries, and narration duration: all must change for a new script. Editing generated HTML alone will be overwritten by a subsequent build.

## Timing pipeline

Transcribe the finished voice using the project's pinned CLI. The example below uses the detox project's version:

```bash
npx --yes hyperframes@0.8.63 transcribe public/audio/voice.wav   --engine whisper --model small.en --language en --json
```

Save raw transcription before correcting alignment. Compare every word with the script. Fix caption spelling only when listening confirms the spoken word. An ASR error is different from a missing spoken word; editing transcript text cannot repair missing narration.

Convert word timestamps to composition time by adding the narration offset exactly once. Use short phrase groups with readable holds, and preserve intentional silent beats. Record aligned word data, phrase groups, and scene boundaries under `public/audio/`. If recognition timestamps run past the physical audio endpoint, verify by listening and clamp the erroneous boundary; do not pretend the audio is longer than it is.

Schedule visual reveals at the relevant spoken idea, not at arbitrary equal intervals. The detox build uses a stationary infographic while highlights move between the two explained regions. Reusing an image is useful when its framing or meaning changes.

## Layout and motion

Build at the intended vertical size, currently 1080×1920 at 30 fps. Keep one dominant focal point per beat. The reference uses roughly 43–45 px phrase captions on a contrasting backing, generally up to two lines. Its lower caption band is a starting point, not a universal guarantee against platform UI overlap; inspect at phone size and keep critical text away from edges.

Use generated art for complete scenes and static lettering. Use HTML/SVG for timed callouts, charts, counters, emphasis, and localization. Keep generated diagram labels readable during camera moves. Prefer purposeful reveals and small reframes to perpetual motion.

After changes, run the full check and inspect the mounted composition rather than only opening an isolated child HTML file. Continue with [render verification](06-render-and-verification.md).
