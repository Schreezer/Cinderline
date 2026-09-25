from pathlib import Path
import json, re, subprocess
P=Path(__file__).resolve().parents[1]
D=P/'public/audio/full-qwen-v2'
words=json.loads((D/'transcript.json').read_text())
lines=json.loads((D/'script-lines.json').read_text())
def norm(s): return re.sub(r'[^a-z0-9]','',s.lower().replace('100','hundred'))
expected=' '.join(lines).split()
assert len(words)==len(expected), (len(words),len(expected))
diff=[(a,b['text']) for a,b in zip(expected,words) if norm(a)!=norm(b['text'])]
assert not diff, diff
for a,b in zip(expected,words): b['text']=a
(D/'transcript.aligned.json').write_text(json.dumps(words,indent=2))
levels=(D/'loudness-analysis.log').read_text()
levels=json.loads(levels[levels.rfind('{'):levels.rfind('}')+1])
gain=min(-18-float(levels['input_i']),-1.5-float(levels['input_tp']))
subprocess.run(['ffmpeg','-y','-v','error','-i',str(D/'narration-raw.wav'),'-af',f'volume={gain}dB','-ar','48000',str(D/'voice.wav')],check=True)
starts=[]; cursor=0
for line in lines:
    starts.append(words[cursor]['start']); cursor+=len(line.split())
starts[0]=0
config={'times':starts+[38.7], 'audio_duration':38.0,'audio_src':'public/audio/full-qwen-v2/voice.wav','lines':'public/audio/full-qwen-v2/script-lines.json','transcript':'public/audio/full-qwen-v2/transcript.aligned.json','srt':'renders/subtitles.qwen-v2.en.srt','click_times':[.18,1.23,2.0,9.84,10.62,11.11,17.71,26.11,29.69,33.83], 'gain_db':gain,'word_audit':'All 110 spoken words match the script after normalizing punctuation and 100/hundred. Caption punctuation restored from source script.'}
(D/'timing.json').write_text(json.dumps(config,indent=2))
print(json.dumps(config,indent=2))
