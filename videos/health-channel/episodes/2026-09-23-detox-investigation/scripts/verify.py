from pathlib import Path
import json,subprocess,hashlib,re,os,shutil
from PIL import Image,ImageDraw,ImageFont
P=Path(__file__).resolve().parents[1];R=P/'renders';v=R/'detox-investigation.en.mp4'
env=dict(os.environ,PATH='/opt/homebrew/bin:/usr/local/bin:'+os.environ.get('PATH',''))
def run(a,**k):return subprocess.run(a,check=True,capture_output=True,text=True,env=env,**k)
m=json.loads(run(['ffprobe','-v','error','-show_format','-show_streams','-of','json',str(v)]).stdout)
vid=next(s for s in m['streams'] if s['codec_type']=='video');aud=next(s for s in m['streams'] if s['codec_type']=='audio')
dur=float(m['format']['duration'])
checks={'size_1080x1920':vid['width']==1080 and vid['height']==1920,'fps_30':vid['r_frame_rate']=='30/1','h264':vid['codec_name']=='h264','aac':aud['codec_name']=='aac','duration_27':abs(dur-27)<.1}
dec=subprocess.run(['ffmpeg','-v','error','-i',str(v),'-f','null','-'],capture_output=True,text=True,env=env)
times=[1.0,3.5,6.5,10.0,12.8,14.6,16.5,18.0,19.6,20.3,22.2,22.9,24.5,26.5]
fr=R/'encoded-frames';shutil.rmtree(fr,ignore_errors=True);fr.mkdir()
sheet=Image.new('RGB',(5*216,3*412),'#182026');dr=ImageDraw.Draw(sheet);font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
paths=[]
for i,t in enumerate(times):
    f=fr/f'{t:05.2f}.jpg';run(['ffmpeg','-y','-v','error','-ss',str(t),'-i',str(v),'-frames:v','1','-q:v','2',str(f)]);paths.append(str(f))
    x=i%5*216;y=i//5*412;sheet.paste(Image.open(f).resize((216,384)),(x,y+26));dr.text((x+6,y+5),f'{t:.2f}s',fill='white',font=font)
sheet.save(R/'encoded-contact-sheet.jpg',quality=90)
ocr=run(['/tmp/ocrbin',*paths]).stdout if Path('/tmp/ocrbin').exists() else 'ocr unavailable'
(R/'encoded-frames-ocr.txt').write_text(ocr)
# encoded ASR audit
tx=R/'asr';tx.mkdir(exist_ok=True);shutil.copy(v,tx/'final.mp4')
subprocess.run(['npx','--yes','hyperframes@0.8.63','transcribe',str(tx/'final.mp4'),'--engine','whisper','--model','small.en','--language','en','--json'],capture_output=True,text=True,env=env,cwd=str(P))
tj=next((p for p in tx.glob('*.json')),None)
asr=json.loads(tj.read_text()) if tj else []
asr=asr if isinstance(asr,list) else asr.get('words',[])
norm=lambda s:re.sub(r"[^a-z0-9']","",s.lower())
exp=[norm(w['text']) for w in json.loads((P/'public/audio/transcript.aligned.json').read_text())]
got=[norm(w['text']) for w in asr]
num={'2009':'2009','15':'fifteen'}
got=[num.get(g,g) for g in got]
diffs=[(i,e,g) for i,(e,g) in enumerate(zip(exp,got)) if e!=g]
rep={'video':str(v),'duration_seconds':dur,'width':vid['width'],'height':vid['height'],'fps':vid['r_frame_rate'],'video_codec':vid['codec_name'],'audio_codec':aud['codec_name'],'audio_rate':aud['sample_rate'],'checks':checks,'decode_errors':dec.stderr.strip()[:2000],'sample_times':times,
 'encoded_asr':{'expected_words':len(exp),'asr_words':len(got),'differences':diffs,'last_word':asr[-1]['text'] if asr else None,'last_word_end':asr[-1]['end'] if asr else None},
 'visual_review':'Automated only: frames extracted and OCR-checked. Human visual review of composition still required (Claude could not view images this session).',
 'sha256':hashlib.sha256(v.read_bytes()).hexdigest(),'voice':json.loads((P/'public/audio/timing.json').read_text()),'publishing':'local draft only'}
(R/'verification.json').write_text(json.dumps(rep,indent=2));print(json.dumps({k:rep[k] for k in ['duration_seconds','checks','decode_errors','encoded_asr','sha256']},indent=1));print(ocr)
