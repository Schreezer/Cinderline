# Google Flow CLI

## Local setup and verified command

Checkout: `/Users/chirag13/src/gflow-cli`. The working browser override is Helium. Run from the StarCraft workspace so the CLI finds the established local session/profile. Do not copy authentication files into episode assets or documentation.

```bash
cd /Users/chirag13/Documents/ChatGPT/starCraft
export GFLOW_CHROME_PATH='/Applications/Helium.app/Contents/MacOS/Helium'
node /Users/chirag13/src/gflow-cli/dist/src/index.js doctor
node /Users/chirag13/src/gflow-cli/dist/src/index.js image --help
```

Generation example: replace the episode directory and prompt before execution. Use a new job ID/output directory for each candidate.

```bash
node /Users/chirag13/src/gflow-cli/dist/src/index.js image   --id scene-01-v1   --prompt 'Original vertical editorial illustration, warm cream paper, forest green ink and coral accents. One clear focal subject in the upper two-thirds. Exact heading: "YOUR APPROVED TEXT". Generous empty lower area for subtitles. Clear bold lettering, consistent prop design.'   --model 'Nano Banana 2' --ratio '9:16' --outputs 1 --timeout 300   --out '/absolute/path/to/new-episode/public/flow/scene-01-v1'
```

See the [working generation script](../episodes/2026-09-23-detox-job-interview/scripts/generate_art.py). It loads prompt jobs from JSON and invokes the CLI with a subprocess argument array, avoiding shell interpolation of prompt text. It may regenerate all listed jobs: adapt it to resume missing jobs instead of blindly rerunning it.

## Prompt design and continuity

Include subject, action, composition, palette/style, exact lettering, and caption clearance. Use short, specific instructions. Earlier approximately 1,800-character prompts hit a `pressSequentially` 20-second typing timeout; roughly 700–830-character prompts worked. This is an observed UI issue, not a documented model token limit.

Define recurring characters and props once, then retain their distinctive features across prompts. `image --help` currently exposes `--character <name...>` for saved characters and `--ingredient <ref...>` for existing project assets. Inspect the actual saved assets and relevant CLI help before using these; do not assume those flags accept arbitrary local image paths or that every listed ingredient type works in the Flow UI.

Nano Banana 2 can produce complete infographics with labels. Supply exact approved words and values. Inspect the image itself: readable-looking text can still contain wrong numbers, misleading arrows, or duplicated objects. Regenerate actual defects. Keep all meaningful text and provider marks visible when cropping or animating.

## Execution and output handling

- Run Flow jobs serially; they share a browser session and UI state.
- Save command output, exact prompt, generation metadata, and original download in `public/flow/<job>/`.
- Downloads have arrived as either a ZIP containing images or a direct JPEG plus JSON. Inspect the returned files; do not assume a fixed filename or extension.
- Copy the selected image into `public/art/`; retain rejected candidates separately for provenance.
- If authentication is missing, report the sign-in needed. If credits are unavailable, use existing authorized assets or HTML diagrams; do not purchase credits automatically.

Optional silent footage uses the CLI's `video` command. Read `video --help` first for the current model, frame, duration, and download options. This guide does not claim that optional video generation was exercised for the detox draft; its visuals are five generated stills across six HTML shots. Keep narration outside Flow.
