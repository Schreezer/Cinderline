"""Short, example-informed English auditions with model-specific conditioning."""
from pathlib import Path
import gc
import json
import time
import subprocess
import sys
import numpy as np
import mlx.core as mx
from mlx_audio.tts.utils import load_model
from mlx_audio.audio_io import write, read

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'public/audio/voice-examples-v3'
TEXT = (
    'No added sugar. Sounds like sugar-free... right? '
    'Well... not quite. Fruit juice can still contain sugar from the fruit. '
    'So turn the pack over. Check total sugars, then compare similar drinks per hundred millilitres. '
    'Small flip. Better information.'
)
FISH_TEXT = (
    '[curious] No added sugar. Sounds like sugar-free... right? [long-break] '
    '[doubtful] Well... not quite. [break] Fruit juice can still contain sugar from the fruit. '
    '[confident] So turn the pack over. Check total sugars, then compare similar drinks per hundred millilitres. '
    '[warm] Small flip. Better information.'
)
INSTRUCTION = (
    'Male, early thirties, a warm resonant midrange voice with light breathiness and clear English. '
    'An easy conversational rhythm, as if talking to a friend in a shop. '
    'The opening question is curious and slightly incredulous; the correction is gently amused; '
    'the advice is warm and reassuring. Let the question hang for a beat. '
    'Use natural rises and falls in pitch, brief breaths between ideas, and relaxed sentence endings.'
)
QWEN = 'mlx-community/Qwen3-TTS-12Hz-1.7B-VoiceDesign-bf16'
FISH = 'mlx-community/fish-audio-s2-pro-8bit'

def save(name, model_id, params, results, start):
    parts = []
    for result in results:
        parts.append(np.array(result.audio))
    samples = np.concatenate(parts)
    sr = result.sample_rate
    peak = float(np.max(np.abs(samples)))
    gain = min(1, .98 / max(peak, .00001))
    write(str(OUT / f'{name}-raw.wav'), samples * gain, sr)
    record = {'name': name, 'model': model_id, 'parameters': params,
              'duration': len(samples)/sr, 'sample_rate': sr, 'raw_peak': peak,
              'gain': gain, 'elapsed_seconds': time.monotonic()-start}
    (OUT / f'{name}-generation.json').write_text(json.dumps(record, indent=2, ensure_ascii=False))
    print(json.dumps(record, ensure_ascii=False), flush=True)

if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    phase = sys.argv[1] if len(sys.argv) > 1 else 'all'
    if phase in ('qwen', 'all'):
        print('Loading Qwen VoiceDesign BF16', flush=True)
        model = load_model(QWEN)
        mx.random.seed(2026)
        params = dict(text=TEXT, instruct=INSTRUCTION, language='English',
                      temperature=.9, top_p=1, max_tokens=900)
        start = time.monotonic()
        save('qwen-designed', QWEN, params, model.generate_voice_design(**params), start)
        del model
        gc.collect()
        mx.clear_cache()
    if phase in ('fish', 'all', 'fish-control'):
        print('Loading Fish S2 Pro', flush=True)
        model = load_model(FISH)
        mx.random.seed(2026)
        params = dict(text=FISH_TEXT, temperature=.7, top_p=.8,
                      max_tokens=1000, chunk_length=1800, speed=1)
        ref = None
        if phase != 'fish-control':
            # A clean, original synthetic reference. No internet speaker or human clone.
            subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', str(OUT/'qwen-designed-raw.wav'),
                            '-ar', '44100', '-ac', '1', str(OUT/'fish-reference.wav')], check=True)
            ref, sr = read(str(OUT/'fish-reference.wav'))
            assert sr == 44100
        start = time.monotonic()
        save('fish-reference' if ref is not None else 'fish-control', FISH,
             {**params, 'reference': 'qwen-designed-raw.wav' if ref is not None else None,
              'reference_text': TEXT if ref is not None else None},
             model.generate(**params, ref_audio=mx.array(ref) if ref is not None else None,
                            ref_text=TEXT if ref is not None else None), start)
