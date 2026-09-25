# Voice auditions based on provider examples

The earlier outputs did not meet the user's naturalness standard. Correct transcripts and longer silent gaps do not establish a convincing performance.

## What the examples change

- [Fish emotion examples](https://docs.fish.audio/developer-guide/core-features/emotions) advise one primary emotion per sentence, spacing out changes, matching emotion to the words, and avoiding excessive tags. Their S2 examples use square brackets, with `[break]` and `[long-break]` listed for pauses. This does not imply the README's `[pause]` syntax is invalid.
- [Fish inference instructions](https://speech.fish.audio/inference/) explain that omitting the reference lets the model choose a random voice timbre. Our previous English takes did this. [Reference guidance](https://docs.fish.audio/developer-guide/best-practices/voice-cloning) emphasizes a clean single voice, consistent delivery, and natural sentence pauses.
- [Qwen's official examples](https://github.com/QwenLM/Qwen3-TTS#voice-design) distinguish VoiceDesign, CustomVoice, and Base. The Base model used in the pilot is intended for reference-conditioned cloning and does not provide the same instruction control. VoiceDesign accepts a separate natural-language description of voice and performance. The examples combine emotionally meaningful wording and punctuation with that description.
- [Qwen's reusable-voice example](https://github.com/QwenLM/Qwen3-TTS#voice-design-then-clone) generates a synthetic voice first and uses the result as a reusable reference. Here we adapt the reference idea to Fish after checking the Qwen output. We use no real person's recording.

## This audition

The script narrows the topic to one label so a short listening comparison is useful. It sounds like a spoken question followed by a correction and a takeaway. It preserves the earlier factual point: a no-added-sugar claim does not mean a product has no total sugars.

```text
No added sugar. Sounds like sugar-free... right?
Well... not quite. Fruit juice can still contain sugar from the fruit.
So turn the pack over. Check total sugars, then compare similar drinks per hundred millilitres.
Small flip. Better information.
```

Qwen uses `mlx-community/Qwen3-TTS-12Hz-1.7B-VoiceDesign-bf16` and a separate voice/performance instruction. Fish uses `mlx-community/fish-audio-s2-pro-8bit`, a small number of sentence-level cues, and the original synthetic Qwen reference. An unconditioned Fish control is retained for diagnosis. These are workflow auditions, not a precision-controlled model benchmark.

Reproduction: run `scripts/voice_examples_v3.py qwen`, verify the output, then run `scripts/voice_examples_v3.py fish`. `fish-control` generates the unconditioned control. Settings, raw audio and checks live in `public/audio/voice-examples-v3/`. The existing video is unchanged.

The comparison is in English. Hindi is not in Qwen3-TTS's officially listed supported languages. Fish's local research licence still requires a separate commercial agreement for production use.

## Delivered takes

- `public/audio/voice-examples-v3/qwen-designed-listen.wav`: 19.44 seconds. Qwen VoiceDesign BF16, separate instruction, original synthetic narrator.
- `public/audio/voice-examples-v3/fish-reference-listen.wav`: 19.09 seconds. Fish S2 Pro 8-bit, sparse inline cues, conditioned on the above Qwen synthetic narration and its exact transcript.
- `public/audio/voice-examples-v3/fish-control-listen.wav`: 19.97 seconds. Same Fish script and sampling settings without reference, retained as a diagnostic control.

All three pass a Whisper medium English word comparison after normalizing numeric spelling and milliliters/millilitres. No instructions are spoken. The Qwen model's safetensors header contains 404 BF16 tensors and its configuration identifies `voice_design`; it has no quantization configuration. Its model revision is `7d3824abff87e49756bb0f83fb5411de75d160c4`.

Listening files use constant gain to approach -18 LUFS without exceeding a -1.5 dBTP ceiling. No time stretch, artificial pauses, or pitch processing was applied. Word accuracy checks do not establish whether a voice sounds natural; these files remain listening auditions.
