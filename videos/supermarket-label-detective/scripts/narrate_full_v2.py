"""Full six-scene narration, conditioned on the user-approved synthetic Qwen voice."""
from pathlib import Path
import json
import time
import numpy as np
import mlx.core as mx
from mlx_audio.tts.utils import load_model
from mlx_audio.audio_io import write
from voice_examples_v3 import TEXT as REFERENCE_TEXT

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'public/audio/full-qwen-v2'
LINES = [
    'Natural. Multigrain. No added sugar. They sound healthy... right? Let\'s turn the pack around.',
    'Natural sounds reassuring. But it doesn\'t tell you how much sugar, salt, or saturated fat you\'re getting.',
    'Multigrain just means more than one grain. Whole grains? Not necessarily. Look for whole wheat or whole oats in the ingredients.',
    'No added sugar sounds like sugar-free... but not quite. Naturally occurring sugars can still be there. Check total sugars, too.',
    'Comparing two packs? Use the same amount... like per hundred grams. Then check how much you actually eat.',
    'The front gets your attention. The back helps you choose. Small habit. Better information. Flip it before you pick it.',
]

if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'script-lines.json').write_text(json.dumps(LINES,indent=2))
    model_id = 'mlx-community/Qwen3-TTS-12Hz-1.7B-Base-bf16'
    reference = ROOT/'public/audio/voice-examples-v3/qwen-designed-raw.wav'
    model = load_model(model_id)
    mx.random.seed(2026)
    start = time.monotonic()
    chunks=[]
    for result in model.generate(text=' '.join(LINES), ref_audio=str(reference),
                                 ref_text=REFERENCE_TEXT, lang_code='English',
                                 temperature=.9, top_p=1, max_tokens=2000, speed=1):
        chunks.append(np.array(result.audio))
    audio=np.concatenate(chunks)
    sr=result.sample_rate
    peak=float(np.abs(audio).max())
    gain=min(1,.98/max(peak,.000001))
    write(str(OUT/'narration-raw.wav'),audio*gain,sr)
    meta={'model':model_id,'reference':str(reference),'reference_origin':'Qwen3-TTS 1.7B VoiceDesign BF16; user-approved audition',
          'reference_text':REFERENCE_TEXT,'script':LINES,'seed':2026,'temperature':.9,'top_p':1,
          'speed':1,'duration':len(audio)/sr,'sample_rate':sr,'raw_peak':peak,'gain':gain,
          'generation_seconds':time.monotonic()-start}
    (OUT/'generation.json').write_text(json.dumps(meta,indent=2))
    print(json.dumps(meta),flush=True)
