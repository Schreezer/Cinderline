from pathlib import Path
import json,time,numpy as np
import mlx.core as mx
from mlx_audio.tts.utils import load_model
from mlx_audio.audio_io import write
P=Path(__file__).resolve().parents[1];O=P/'public/audio'
lines=json.loads((O/'script-lines.json').read_text())
ref=Path('/Users/chirag13/Documents/ChatGPT/starCraft/videos/supermarket-label-detective/public/audio/voice-examples-v3/qwen-designed-raw.wav')
reftext='No added sugar. Sounds like sugar-free... right? Well... not quite. Fruit juice can still contain sugar from the fruit. So turn the pack over. Check total sugars, then compare similar drinks per hundred millilitres. Small flip. Better information.'
model_id='mlx-community/Qwen3-TTS-12Hz-1.7B-Base-bf16'
model=load_model(model_id);mx.random.seed(2026);start=time.monotonic();parts=[]
for r in model.generate(text=' '.join(lines),ref_audio=str(ref),ref_text=reftext,lang_code='English',temperature=.9,top_p=1,max_tokens=1700,speed=1):parts.append(np.array(r.audio))
a=np.concatenate(parts);write(str(O/'narration-raw.wav'),a,r.sample_rate)
meta=dict(model=model_id,reference=str(ref),reference_origin='User-approved original synthetic Qwen VoiceDesign',reference_text=reftext,script=lines,seed=2026,temperature=.9,speed=1,duration=len(a)/r.sample_rate,sample_rate=r.sample_rate,generation_seconds=time.monotonic()-start)
(O/'generation.json').write_text(json.dumps(meta,indent=2));print(json.dumps(meta),flush=True)
