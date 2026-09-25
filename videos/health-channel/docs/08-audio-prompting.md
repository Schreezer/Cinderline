# Prompting for natural, expressive narration

The user initially found both voices unnatural and lacking pauses. After example-informed prompting, the user accepted both Qwen and Fish and said the prompt had been the issue. The approved audition files document what worked; they do not guarantee that every new script will sound good.

Use this guide with [local voice setup](04-local-voice.md). These examples come from the saved local scripts and generation metadata, not an assertion that all Qwen/Fish interfaces support the same controls.

## Choose the correct control surface

| Route | What to supply | What controls delivery |
| --- | --- | --- |
| Qwen Base BF16, normal episode route | Spoken text, approved reference WAV, exact reference transcript | Reference performance plus phrasing and punctuation in the new text |
| Qwen VoiceDesign BF16, deliberate voice audition | Spoken text plus a separate `instruct` string | Natural-language voice and performance description through `generate_voice_design` |
| Fish S2 Pro 8-bit, approved alternative | Spoken text with sparse supported tags, reference waveform and transcript | Reference voice plus emotion/pause tags in the locally tested implementation |

Do not put `[curious]`, “pause here,” or the VoiceDesign instruction into Qwen Base's spoken text. Our Base narrator does not use the VoiceDesign API. Do not assume SSML `<break>` is supported. A cue appearing in a prompt does not prove the model performed it; listen to the result.

## Write for the ear first

Use one thought per sentence or short clause. Mix short reactions with longer explanations. Give the voice a reason to change: curiosity about a claim, a moment of realization, then calm advice. Keep the performance proportionate to the topic.

Example using a neutral prop:

```text
Stiff:
The printed serving quantity on this package should be examined prior to comparing the values.

Spoken:
Hang on. How big is one serving? Turn the pack over... there it is.
```

The revision gives a question room to hang, then ties the answer to a visible action. It is a performance example, not a complete scientific script. Preserve all necessary factual qualifications when rewriting real claims.

Periods encourage completed thoughts; question marks can invite rising intonation; an occasional ellipsis can suggest hesitation or a held beat. These are soft cues, not exact pause durations. Repeated ellipses and very short fragments can make delivery halting. Avoid capitals and exclamation marks as substitutes for a clear performance intention.

For a 30-second short, start with roughly 55–65 words and measure the take. If it sounds rushed, cut or rewrite before changing the audio's speed. Some sentences need more silence than their word count suggests.

## Qwen: the exact approved VoiceDesign instruction

This instruction created the reference voice we now reuse:

```text
Male, early thirties, a warm resonant midrange voice with light breathiness and clear English. An easy conversational rhythm, as if talking to a friend in a shop. The opening question is curious and slightly incredulous; the correction is gently amused; the advice is warm and reassuring. Let the question hang for a beat. Use natural rises and falls in pitch, brief breaths between ideas, and relaxed sentence endings.
```

It was passed separately as `instruct` with the approved label-reading audition text, `language='English'`, temperature 0.9, top_p 1, and max_tokens 900 to `generate_voice_design`. See [the saved generation record](../../supermarket-label-detective/public/audio/voice-examples-v3/qwen-designed-generation.json).

For ordinary episodes, retain the approved reference and use Qwen Base as described in the setup guide. Generating a new VoiceDesign take can change voice identity. Treat that as a separate audition, save it under a new name, and never silently replace the approved reference.

When a new voice audition is requested, adapt the instruction to the actual script's emotional progression. “Curious opening, gently amused realization, relaxed advice” gives more direction than “make it emotional.” Limit instructions to compatible choices; avoid simultaneously demanding high excitement, soothing calm, fast pace, and long pauses throughout.

## Fish: the exact successful tagged audition

```text
[curious] No added sugar. Sounds like sugar-free... right? [long-break]
[doubtful] Well... not quite. [break] Fruit juice can still contain sugar from the fruit.
[confident] So turn the pack over. Check total sugars, then compare similar drinks per hundred millilitres.
[warm] Small flip. Better information.
```

See [the saved Fish generation record](../../supermarket-label-detective/public/audio/voice-examples-v3/fish-reference-generation.json) for the exact single-string input and parameters. This is an existing audition example, not a requirement to reuse its script in new episodes.

Use tags at a few meaningful transitions rather than before every sentence. `[long-break]` after the opening question and `[break]` before the explanation were used successfully in this audition. Their realized lengths still need measurement. Start with the same voice conditioning and sampling settings; change one aspect at a time so comparisons are useful.

Build caption text from the clean spoken script, with tags removed. If a model speaks a cue aloud, reject the take and check the installed implementation instead of hiding the cue in subtitles.

## Prompt for Claude to prepare TTS input

```text
Adapt the script below for our approved off-screen narrator without changing its factual meaning.

Target: conversational, interested, lightly amused where appropriate, and reassuring. Speak to one person. Give important questions and reveals breathing room. Avoid an announcer voice and forced excitement.

Return:
1. Clean spoken text suitable for Qwen Base with our existing approved reference. Use natural phrasing and restrained punctuation. No bracket tags or stage directions in this version.
2. A Fish S2 Pro version of the same spoken words, adding only sparse tags from our tested set: [curious], [doubtful], [confident], [warm], [break], [long-break].
3. A separate performance map naming the few words or moments needing emphasis, a held beat, or a softer ending.
4. Any wording that needs cutting to fit the target duration naturally. Preserve scientific qualifications; flag any proposed factual change.

Target duration: [seconds]
Script: [paste supported script]
```

The performance map guides audition review and visual timing. It is not automatically a supported TTS control format.

## Audition and correction loop

1. Use a short passage containing the opening, one explanatory sentence, and a payoff. Keep the reference voice fixed.
2. Save input, model, parameters, seed, and output with a distinct take name. Compare a baseline with one targeted revision.
3. Listen for whether the question hangs naturally, stressed words carry meaning, explanations remain clear, and sentence endings avoid a repetitive melody. Check audible breaths, awkward silences, pronunciation, and clipped endings.
4. Diagnose before regenerating. Flat delivery may need a clearer emotional turn in the writing; rushed delivery may need fewer words; voice drift may mean reference conditioning changed. Temperature is a sampling parameter, not an emotion dial.
5. Generate the complete short continuously once the approach works. Audit the full take again; a good short audition does not guarantee a good full generation.
6. Measure the final speech, align captions and visual reveals, then listen to the encoded mix. Preserve original speed and pitch. Do not create an illusion of verification by relying only on ASR.

For Hindi, write conversational Hindi and explicitly review pronunciation, numbers, borrowed English words, and pauses. Test the model's actual language controls through its installed interface. No Hindi audition has been established by the English examples above.
