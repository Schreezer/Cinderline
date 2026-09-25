"""Local, non-commercial Fish S2 Pro performance audition; preserves original edit."""
from pathlib import Path
import json
import time
import numpy as np
import mlx.core as mx
from mlx_audio.tts.utils import load_model
from mlx_audio.audio_io import write

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'public/audio/fish-audition'
OUT.mkdir(parents=True, exist_ok=True)
MODEL = 'mlx-community/fish-audio-s2-pro-8bit'
EN = (
    '[curious] These three labels sound healthy. [short pause] But what do they actually tell you? '
    '[inviting] Let\'s turn the pack around. '
    '[skeptical] Natural isn\'t a nutrition score. '
    '[explaining] It doesn\'t tell you how much sugar, salt, or saturated fat is inside. '
    'Multigrain means more than one grain. [emphasis] It doesn\'t guarantee whole grains. '
    '[warm] Look for whole wheat or whole oats in the ingredients. '
    '[surprised] No added sugar doesn\'t mean sugar free. '
    '[explaining] Naturally occurring sugars can still be there. Check total sugars, too. '
    'Compare similar foods using the same amount, like per hundred grams. '
    'Then check how much you actually eat. '
    '[confident] The front gets your attention. [short pause] The back helps you choose. '
    '[upbeat] Flip it before you pick it.'
)
HI = (
    '[curious] पैकेट पर लिखा है नैचुरल। तो क्या यह सेहत के लिए अच्छा है? '
    '[skeptical] ज़रूरी नहीं! [explaining] पैकेट पलटिए। चीनी कितनी है? सोडियम कितना है? '
    'मिलती-जुलती चीज़ों की तुलना सौ ग्राम के हिसाब से कीजिए। '
    '[warm] फिर देखिए, आप कितना खाते हैं।'
)
INSTRUCT = (
    'One warm, engaging young adult narrator speaking directly to a friend. '
    'Conversational and expressive, with clear diction, varied pitch, and purposeful short pauses. '
    'Curiosity in questions, light skepticism at marketing claims, reassuring confidence in practical advice. '
    'Natural brisk explainer pacing. No shouting, caricature, background music, or sound effects.'
)

if __name__ == '__main__':
    print('Loading Fish S2 Pro', flush=True)
    model = load_model(MODEL)
    metadata = {'model': MODEL, 'performance_brief': INSTRUCT, 'conditioning': 'Inline tags only. System style instruction rejected because model spoke it aloud.', 'license': 'Fish Audio Research License; local non-commercial audition', 'takes': []}
    for name, text in [('english-expressive', EN), ('hindi-expressive', HI)]:
        seed = 47 if name == 'english-expressive' else 49
        mx.random.seed(seed)
        started = time.monotonic()
        segments = []
        print('Generating ' + name, flush=True)
        for result in model.generate(text=text,
                                     temperature=0.7 if name == 'english-expressive' else 0.65,
                                     top_p=0.8, max_tokens=1500 if name == 'english-expressive' else 1100,
                                     chunk_length=1800, speed=1.0):
            arr = np.array(result.audio)
            segments.append(arr)
            print(json.dumps({'take': name, 'segment_duration': len(arr) / result.sample_rate,
                              'seconds_elapsed': time.monotonic() - started}), flush=True)
        audio = np.concatenate(segments)
        rate = result.sample_rate
        path = OUT / (name + '.wav')
        write(str(path), audio, rate)
        metadata['takes'].append({'name': name, 'text': text, 'duration': len(audio) / rate,
                                  'sample_rate': rate, 'elapsed': time.monotonic() - started,
                                  'path': str(path), 'reference': None, 'seed': seed})
        (OUT / 'generation.json').write_text(json.dumps(metadata, indent=2, ensure_ascii=False))
    print('Done', flush=True)
