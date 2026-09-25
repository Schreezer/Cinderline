"""Audition v2: stronger phrase breaks and a deliberate pitch/energy arc."""
from pathlib import Path
import json
import time
import numpy as np
import mlx.core as mx
from mlx_audio.tts.utils import load_model
from mlx_audio.audio_io import write

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'public/audio/fish-pauses-v2'
MODEL = 'mlx-community/fish-audio-s2-pro-8bit'
EN = (
    '[curious] These three labels sound healthy. [pause] '
    '[pitch up] But what do they [emphasis] actually tell you? [pause] '
    '[warm] Let\'s turn the pack around. [pause] '
    '[low voice] Natural isn\'t a nutrition score. [pause] '
    '[conversational] It doesn\'t tell you how much sugar, [short pause] salt, '
    '[short pause] or saturated fat is inside. [pause] '
    '[explaining] Multigrain means more than one grain. [short pause] '
    '[emphasis] It doesn\'t guarantee whole grains. [pause] '
    '[warm] Look for whole wheat or whole oats in the ingredients. [pause] '
    '[surprised] No added sugar [short pause] doesn\'t mean sugar free. [pause] '
    '[calm] Naturally occurring sugars can still be there. [short pause] '
    '[emphasis] Check total sugars, too. [pause] '
    '[conversational] Compare similar foods using the same amount, '
    '[short pause] like per hundred grams. [pause] '
    'Then check how much you [emphasis] actually eat. [pause] '
    '[low voice] The front gets your attention. [pause] '
    '[warm] The back helps you choose. [pause] '
    '[confident] Flip it [short pause] before you pick it.'
)
HI = (
    '[curious] पैकेट पर लिखा है नैचुरल। [pause] '
    '[pitch up] तो क्या यह सेहत के लिए अच्छा है? [pause] '
    '[low voice] ज़रूरी नहीं! [pause] '
    '[warm] पैकेट पलटिए। [pause] '
    '[curious] चीनी कितनी है? [pause] सोडियम कितना है? [pause] '
    '[explaining] मिलती-जुलती चीज़ों की तुलना [short pause] सौ ग्राम के हिसाब से कीजिए। [pause] '
    '[warm] फिर देखिए, [short pause] आप कितना खाते हैं।'
)

if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    model = load_model(MODEL)
    records = {'model': MODEL, 'speed': 1, 'reference': None,
               'notes': 'Inline tags only. No system instructions, time stretch, or inserted silence.', 'takes': []}
    for language, text, seed in [('english', EN, 47), ('hindi', HI, 49)]:
        mx.random.seed(seed)
        print(f'Generating {language}', flush=True)
        start = time.monotonic()
        audio = []
        for result in model.generate(text=text, temperature=.7 if language == 'english' else .65,
                                     top_p=.8, max_tokens=2300, chunk_length=2500, speed=1):
            audio.append(np.array(result.audio))
        samples = np.concatenate(audio)
        rate = result.sample_rate
        peak = float(np.abs(samples).max())
        # Preserve dynamics while avoiding PCM clipping if the model overshoots.
        gain = min(1.0, .98 / max(peak, .000001))
        write(str(OUT / f'{language}-raw.wav'), samples * gain, rate)
        records['takes'].append({'language': language, 'text': text, 'seed': seed,
                                 'duration': len(samples) / rate, 'sample_rate': rate,
                                 'raw_peak': peak, 'gain': gain, 'generation_seconds': time.monotonic()-start})
        (OUT / 'generation.json').write_text(json.dumps(records, ensure_ascii=False, indent=2))
        print(json.dumps(records['takes'][-1], ensure_ascii=False), flush=True)
