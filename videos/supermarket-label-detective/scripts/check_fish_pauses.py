from pathlib import Path
import difflib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'public/audio/fish-pauses-v2'
MODELS = Path.home() / '.cache/hyperframes/whisper/models'
records = json.loads((OUT / 'generation.json').read_text())
checks = json.loads((OUT/'verification.json').read_text()) if (OUT/'verification.json').exists() else {}
def run(args):
    return subprocess.run(args, capture_output=True, text=True, check=True)
def words(text):
    text = re.sub(r'\[[^]]*\]', '', text.lower()).replace('multi-grain', 'multigrain')
    return re.findall(r"[a-z0-9]+", text)
def gaps(path):
    r = run(['ffmpeg', '-hide_banner', '-i', str(path), '-af', 'silencedetect=noise=-35dB:d=0.25', '-f', 'null', '-'])
    durations = [float(s) for s in re.findall(r'silence_duration: ([0-9.]+)', r.stderr)]
    return {'count_250ms_or_more': len(durations), 'total_seconds': sum(durations), 'durations': durations}
for take in records['takes']:
    lang = take['language']
    if len(sys.argv) > 1 and lang not in sys.argv[1:]:
        continue
    raw = OUT / f'{lang}-raw.wav'
    result = run(['ffmpeg', '-hide_banner', '-i', str(raw), '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11:print_format=json', '-f', 'null', '-'])
    levels = json.loads(result.stderr[result.stderr.rfind('{'):result.stderr.rfind('}')+1])
    gain = min(-16-float(levels['input_i']), -1.5-float(levels['input_tp']))
    listening = OUT / f'{lang}-listen.wav'
    run(['ffmpeg', '-y', '-v', 'error', '-i', str(raw), '-af', f'volume={gain}dB', '-ar', '44100', str(listening)])
    model = 'ggml-small.en.bin' if lang == 'english' else 'ggml-small.bin'
    prefix = OUT / f'{lang}-check'
    r = run(['whisper-cli', '-m', str(MODELS / model), '-f', str(listening), '-l', 'en' if lang == 'english' else 'hi', '-otxt', '-oj', '-of', str(prefix)])
    (OUT / f'{lang}-check.log').write_text(r.stdout + r.stderr)
    transcript = prefix.with_suffix('.txt').read_text().strip()
    check = {'duration': take['duration'], 'transcript': transcript, 'static_gain_db': gain,
             'pauses_previous': gaps(ROOT/'public/audio/fish-audition'/f'{lang}-listen.wav'),
             'pauses_v2': gaps(listening)}
    if lang == 'english':
        a, b = words(take['text']), words(transcript)
        check['word_differences'] = [{'kind': op, 'expected': a[i:j], 'asr': b[k:l]}
                                     for op, i, j, k, l in difflib.SequenceMatcher(None, a, b).get_opcodes() if op != 'equal']
    checks[lang] = check
checks['limitations'] = 'Silence detection is approximate and includes boundary silence. ASR does not establish naturalness, emotion, or Hindi pronunciation.'
(OUT/'verification.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2))
print(json.dumps(checks, ensure_ascii=False, indent=2))
