from pathlib import Path
import json
import numpy as np
import mlx.core as mx
from mlx_audio.tts.utils import load_model
from mlx_audio.audio_io import write

ROOT = Path(__file__).resolve().parents[1]
lines = [
    "These three labels sound healthy. But what do they actually tell you? Let's turn the pack around.",
    "Natural isn't a nutrition score. It doesn't tell you how much sugar, salt, or saturated fat is inside.",
    "Multigrain means more than one grain. It doesn't guarantee whole grains. Look for whole wheat or whole oats in the ingredients.",
    "No added sugar doesn't mean sugar free. Naturally occurring sugars can still be there. Check total sugars, too.",
    "Compare similar foods using the same amount, like per hundred grams. Then check how much you actually eat.",
    "The front gets your attention. The back helps you choose. Flip it before you pick it."
]
(ROOT/'script-lines.json').write_text(json.dumps(lines, indent=2))
model = load_model('/Users/chirag13/.cache/huggingface/hub/models--mlx-community--Qwen3-TTS-12Hz-1.7B-Base-bf16/snapshots/a6eb4f68e4b056f1215157bb696209bc82a6db48')
mx.random.seed(42)
text = ' '.join(lines)
audio=[]
for result in model.generate(text=text, lang_code='English', temperature=0.65, max_tokens=1800, verbose=True):
    audio.append(np.array(result.audio))
    sr=result.sample_rate
arr=np.concatenate(audio)
write(str(ROOT/'public/audio/narration.wav'), arr, sr)
print(json.dumps({'sample_rate':sr,'duration':len(arr)/sr}), flush=True)
