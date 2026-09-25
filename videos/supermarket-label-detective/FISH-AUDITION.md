# Fish S2 Pro voice audition

This is a private evaluation of a more expressive narrator. The original Qwen video and its timing remain available unchanged.

Run from this project:

```sh
.venv/bin/python -u scripts/fish_audition.py
```

The script uses `mlx-community/fish-audio-s2-pro-8bit`, an MLX conversion of `fishaudio/s2-pro`, on Apple Silicon. The existing `mlx-audio==0.5.5` environment supports it.

The English take keeps the original spoken words, with local expression tags. The Hindi take is a shorter adaptation about the "natural" label, not a translation of the entire episode. Final auditions are independently generated voices, without a real person's voice recording. Speed stays at 1.0; video edits should follow approved narration timing.

The first experiment passed the performance brief via the MLX `instruct` parameter. Fish spoke that brief aloud, then hit its token limit before finishing the script. That take was rejected and retained only under `rejected-system-instruction/` for diagnosis. The revised script uses inline tags only and does not send the prose performance brief to the model.

An intermediate Hindi take used the generated English voice as its reference. Its ASR transcript omitted the final sentence and showed pronunciation ambiguities. It is retained under `rejected-english-conditioned-hindi/`. The final Hindi attempt uses a simpler script with no English voice reference. Cross-language narrator identity is not established by these auditions.

Intended performance:

- Opening: interested, curious, with an audible question.
- Marketing claim: light skepticism, without alarm or ridicule.
- Explanation: clear and reassuring.
- Takeaway: confident, warm, and brisk.

Generated files and settings are saved in `public/audio/fish-audition/`. Review word accuracy, pronunciation, pauses, emotional range, and continuity before adopting the voice. Voice conditioning does not guarantee identical accent or timbre between languages.

Sources: [original model](https://huggingface.co/fishaudio/s2-pro), [MLX conversion](https://huggingface.co/mlx-community/fish-audio-s2-pro-8bit), [license](https://huggingface.co/fishaudio/s2-pro/blob/main/LICENSE.md).

## Listening files and checks

- `public/audio/fish-audition/english-listen.wav`: 44.95 seconds. Same spoken English script as the Qwen pilot. Normalized ASR word comparison has no differences.
- `public/audio/fish-audition/hindi-listen.wav`: 14.30 seconds. Independent synthetic voice, shorter native Hindi script. Multilingual ASR captures the ending and the intended comparison advice, but includes spelling/phonetic ambiguities. A fluent listening review is still required for accent and pronunciation.
- Both files are loudness-normalized to a -16 LUFS target with a -1.5 dBTP ceiling, at 44.1 kHz. No speed change was applied.
- Raw PCM sample peaks remain below full scale. Full text and generation timing are in `generation.json`; transcription results are in `verification.json` in the same audio directory.
- These are audio auditions. The existing MP4 still uses Qwen narration. An approved Fish performance needs a new visual/caption timing pass before it can replace that soundtrack.

The Fish Audio Research License permits non-commercial evaluation/testing; commercial use requires a separate agreement. These audition files are not a licensed production rollout. No paid service, publication, or recurring automation was enabled.

## Second audition: pauses and pitch contrast

Run `scripts/fish_pauses.py` followed by `scripts/check_fish_pauses.py` in the project environment. Outputs are in `public/audio/fish-pauses-v2/`.

This version preserves both scripts' spoken wording and adds explicit `[pause]`, `[short pause]`, `[pitch up]`, `[low voice]`, and `[emphasis]` controls, with calmer explanatory phrases between claims. It keeps the same 8-bit model, seed per language, and speed 1.0. There is no inserted silence or time stretching. Playback files use a constant gain adjustment to preserve volume variation.

The English take is 45.79 seconds and the Hindi take 18.39 seconds. Silence detection at -35 dB, counting gaps of at least 250 ms, finds approximately 4.33 seconds in English versus 3.40 previously, and 6.09 seconds in Hindi versus 3.20 previously. These are approximate acoustic measurements, including boundary silence, not proof of perceived naturalness.

English small-model ASR misrecognized "Naturally" as "Only". A stronger medium English ASR pass transcribes it correctly and matches every word after normalizing "100" to "hundred". Hindi ASR captures the complete intended sequence with orthographic/phonetic ambiguities; pronunciation and naturalness remain listening judgments. Full settings and checks are saved alongside the WAV files. The MP4 soundtrack has not been replaced.
