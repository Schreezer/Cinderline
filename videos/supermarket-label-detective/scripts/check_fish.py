"""Transcribe audition takes and prepare level-matched listening files."""
from pathlib import Path
import subprocess
import json
import re
import difflib
import numpy as np
from mlx_audio.audio_io import read

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'public/audio/fish-audition'
WHISPER = Path.home() / '.cache/hyperframes/whisper/models'
checks = {}
for language, model in [('english', 'ggml-small.en.bin'), ('hindi', 'ggml-small.bin')]:
    raw = OUT / f'{language}-expressive.wav'
    prefix = OUT / f'{language}-check'
    prior = OUT / f'{language}-early-check.txt'
    if prior.exists() and prior.stat().st_mtime >= raw.stat().st_mtime:
        prefix.with_suffix('.txt').write_text(prior.read_text())
    else:
        with (OUT / f'{language}-check.log').open('w') as log:
            subprocess.run(['whisper-cli', '-m', str(WHISPER / model), '-f', str(raw),
                            '-l', 'en' if language == 'english' else 'hi', '-otxt', '-oj',
                            '-of', str(prefix)], stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', str(raw), '-af',
                    'loudnorm=I=-16:TP=-1.5:LRA=9', '-ar', '44100',
                    str(OUT / f'{language}-listen.wav')], check=True)
    arr, sr = read(str(raw))
    checks[language] = {'duration': len(arr) / sr,
                        'max_abs_sample': float(np.max(np.abs(arr))),
                        'transcription': prefix.with_suffix('.txt').read_text().strip()}

expected = ' '.join(json.loads((ROOT / 'script-lines.json').read_text()))
def tokens(s):
    return re.findall(r"[a-z0-9]+", s.lower().replace('multi-grain', 'multigrain'))
actual = checks['english']['transcription']
a, b = tokens(expected), tokens(actual)
sm = difflib.SequenceMatcher(None, a, b)
checks['english']['expected_word_count'] = len(a)
checks['english']['transcribed_word_count'] = len(b)
checks['english']['differences'] = [{'kind': op, 'expected': a[i:j], 'heard_by_asr': b[k:l]}
                                  for op, i, j, k, l in sm.get_opcodes() if op != 'equal']
checks['note'] = 'ASR checks word content, not perceived emotion or voice quality. Hindi needs fluent listening review.'
(OUT / 'verification.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2))
print(json.dumps(checks, ensure_ascii=False, indent=2))
