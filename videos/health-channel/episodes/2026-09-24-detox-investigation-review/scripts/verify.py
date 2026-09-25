from pathlib import Path
import json, subprocess, hashlib
from PIL import Image,ImageDraw
P=Path(__file__).resolve().parents[1];R=P/'renders';v=R/'detox-investigation.reviewed.en.mp4'
def run(args):return subprocess.run(args,capture_output=True,text=True,check=True)
m=json.loads(run(['ffprobe','-v','error','-show_format','-show_streams','-of','json',str(v)]).stdout)
vs=next(x for x in m['streams'] if x['codec_type']=='video');au=next(x for x in m['streams'] if x['codec_type']=='audio')
assert (vs['width'],vs['height'],vs['r_frame_rate'],vs['codec_name'],au['codec_name'])==(1080,1920,'30/1','h264','aac')
assert abs(float(m['format']['duration'])-35)<.05
result=run(['ffmpeg','-v','error','-i',str(v),'-f','null','-']);assert not result.stderr.strip(),result.stderr
(R/'ffprobe.json').write_text(json.dumps(m,indent=2));(R/'decode.log').write_text(result.stderr or 'Full decode completed without errors.\n')
times=[.8,3.5,7,12,17.5,21,24.8,27.2,29.4,31,33.5,34.9]
f=R/'encoded-frames';f.mkdir(exist_ok=True);sheet=Image.new('RGB',(4*270,3*505),'#182026');d=ImageDraw.Draw(sheet)
for i,t in enumerate(times):
 out=f/f'{t:05.2f}.jpg';run(['ffmpeg','-y','-v','error','-ss',str(t),'-i',str(v),'-frames:v','1','-q:v','2',str(out)])
 x=i%4*270;y=i//4*505;sheet.paste(Image.open(out).resize((270,480)),(x,y+25));d.text((x+8,y+6),f'{t:.2f}s',fill='white')
sheet.save(R/'encoded-contact-sheet.jpg',quality=94)
report={'video':str(v),'sha256':hashlib.sha256(v.read_bytes()).hexdigest(),'duration_seconds':float(m['format']['duration']),'width':vs['width'],'height':vs['height'],'fps':vs['r_frame_rate'],'video_codec':vs['codec_name'],'audio_codec':au['codec_name'],'audio_sample_rate':au['sample_rate'],'decode_errors':0,'sample_times':times,'encoded_frames_visually_reviewed':False,'encoded_audio_audit':None,'human_listening_review':False,'framework_version':'0.8.72','raw_narration_audit':json.loads((P/'public/audio/word-audit.json').read_text()),'publishing':'local draft only'}
(R/'verification.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
