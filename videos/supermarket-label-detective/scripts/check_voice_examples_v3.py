from pathlib import Path
import difflib
import json
import re
import subprocess
import sys
from voice_examples_v3 import TEXT

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'public/audio/voice-examples-v3'
def run(args):
    return subprocess.run(args, capture_output=True, text=True, check=True)
def tokens(s):
    return re.findall(r'[a-z0-9]+', s.lower().replace('milliliters','millilitres').replace('100','hundred'))

for name in sys.argv[1:]:
    raw = OUT/f'{name}-raw.wav'
    result = run(['ffmpeg','-hide_banner','-i',str(raw),'-af','loudnorm=I=-18:TP=-1.5:LRA=11:print_format=json','-f','null','-'])
    levels = json.loads(result.stderr[result.stderr.rfind('{'):result.stderr.rfind('}')+1])
    gain = min(-18-float(levels['input_i']),-1.5-float(levels['input_tp']))
    dst = OUT/f'{name}-listen.wav'
    run(['ffmpeg','-y','-v','error','-i',str(raw),'-af',f'volume={gain}dB','-ar','44100',str(dst)])
    prefix = OUT/f'{name}-check'
    result = run(['whisper-cli','-m',str(Path.home()/'.cache/hyperframes/whisper/models/ggml-medium.en.bin'),
                  '-f',str(dst),'-l','en','-otxt','-oj','-of',str(prefix)])
    (OUT/f'{name}-check.log').write_text(result.stdout+result.stderr)
    transcript = prefix.with_suffix('.txt').read_text().strip()
    a,b = tokens(TEXT),tokens(transcript)
    difference = [{'kind':op,'expected':a[i:j],'asr':b[k:l]}
                  for op,i,j,k,l in difflib.SequenceMatcher(None,a,b).get_opcodes() if op!='equal']
    result = {'name':name,'transcript':transcript,'differences':difference,'gain_db':gain,
              'processing':'Constant gain only; no time stretch, inserted gaps, or pitch processing.',
              'limitation':'ASR validates words, not naturalness or acting quality.'}
    (OUT/f'{name}-verification.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
