# Local Qwen and Fish narration

For exact successful performance prompts, model-specific tags, and an audition loop, read [08 Audio prompting](08-audio-prompting.md).

## Approved Qwen route

The established Python environment is:

```text
/Users/chirag13/Documents/ChatGPT/starCraft/videos/supermarket-label-detective/.venv/bin/python
```

Read the [working narrator](../episodes/2026-09-23-detox-job-interview/scripts/narrate.py) before adapting it. It uses `mlx_audio`, model `mlx-community/Qwen3-TTS-12Hz-1.7B-Base-bf16`, and an original synthetic voice reference:

```text
/Users/chirag13/Documents/ChatGPT/starCraft/videos/supermarket-label-detective/public/audio/voice-examples-v3/qwen-designed-raw.wav
```

The reference transcript must match that audio exactly:

> No added sugar. Sounds like sugar-free... right? Well... not quite. Fruit juice can still contain sugar from the fruit. So turn the pack over. Check total sugars, then compare similar drinks per hundred millilitres. Small flip. Better information.

The reference was produced using Qwen VoiceDesign; full episodes use Base with `ref_audio` and `ref_text` to retain that voice. These are different model interfaces. Do not remove conditioning or replace the reference with an unrelated speaker.

Working full-script parameters: seed 2026, `lang_code='English'`, temperature 0.9, top_p 1, max_tokens 1700, speed 1. These are a starting point for the installed MLX implementation, not universal settings for every Qwen package. Increase generation capacity when a longer script would otherwise truncate.

## Procedure

1. Save the spoken sentences in `public/audio/script-lines.json` in the new episode. Keep performance directions outside this spoken input unless the specific model supports them.
2. Adapt the narrator in the new episode, preserving reference audio and transcript. Generate the entire short continuously. Do not rerun the original episode script and overwrite its files.
3. Save `narration-raw.wav` and `generation.json` with model, parameters, reference provenance, input text, duration, and sample rate.
4. Listen to the complete take. Evaluate pronunciation, pauses, sentence endings, emotional progression, missing words, repetitions, and artifacts. An ASR match alone does not establish natural delivery.
5. Adjust the writing or model-supported controls and regenerate if needed. Preserve natural pitch and speed. Gain adjustment and sample-rate conversion are acceptable; speeding/slowing a weak take is not the approved solution.
6. Save the delivery narration as `voice.wav`, measure it, then build caption and reveal timings.

The detox take was 26.32 seconds with a 0.4-second timeline offset in a 30-second export. Those timings and its +1.48 dB gain belong to that take only; measure every new one.

## Fish alternative

The user accepted both voices after better prompting. The verified Fish audition used `mlx-community/fish-audio-s2-pro-8bit`, not BF16, with the same approved synthetic voice converted to mono 44.1 kHz. See [voice_examples_v3.py](../../supermarket-label-detective/scripts/voice_examples_v3.py) for exact conditioning and implementation.

The successful audition used sparse tags such as `[curious]`, `[long-break]`, `[doubtful]`, `[break]`, `[confident]`, and `[warm]`; temperature 0.7, top_p 0.8, max_tokens 1000, chunk_length 1800, speed 1. Keep tags out of captions. Do not transplant Fish tags into Qwen Base and assume they control emotion. Avoid repeatedly generating a new Qwen reference when auditioning Fish; reuse the approved file.

Hindi needs a separate short audition and native-language review of pronunciation and phrasing. The English reference and historical approval do not prove Hindi quality.
