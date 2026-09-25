from pathlib import Path
import json,subprocess,hashlib
from PIL import Image,ImageDraw,ImageFont
P=Path(__file__).resolve().parents[1];R=P/'renders';v=R/'detox-job-interview.en.mp4'
def run(a):return subprocess.run(a,check=True,capture_output=True,text=True)
m=json.loads(run(['ffprobe','-v','error','-show_format','-show_streams','-of','json',str(v)]).stdout)
vid=next(s for s in m['streams'] if s['codec_type']=='video');aud=next(s for s in m['streams'] if s['codec_type']=='audio')
assert vid['width']==1080 and vid['height']==1920 and vid['r_frame_rate']=='30/1'
assert abs(float(m['format']['duration'])-30)<.05
run(['ffmpeg','-v','error','-i',str(v),'-f','null','-'])
times=[.8,3.8,6.4,8.8,11.5,16.6,21.5,25.8,29.5]
frames=R/'encoded-frames';frames.mkdir(exist_ok=True)
sheet=Image.new('RGB',(3*324,3*604),'#182026');draw=ImageDraw.Draw(sheet);font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18)
for i,t in enumerate(times):
 f=frames/f'{t:05.2f}.jpg';run(['ffmpeg','-y','-v','error','-ss',str(t),'-i',str(v),'-frames:v','1','-q:v','2',str(f)])
 x=i%3*324;y=i//3*604;sheet.paste(Image.open(f).resize((324,576)),(x,y+28));draw.text((x+8,y+5),f'{t:.2f}s',fill='white',font=font)
sheet.save(R/'encoded-contact-sheet.jpg',quality=92)
report={'video':str(v),'duration_seconds':float(m['format']['duration']),'width':vid['width'],'height':vid['height'],'fps':vid['r_frame_rate'],'video_codec':vid['codec_name'],'audio_codec':aud['codec_name'],'audio_rate':aud['sample_rate'],'audio_channels':aud['channels'],'decode_errors':0,'sample_times':times,'sha256':hashlib.sha256(v.read_bytes()).hexdigest(),'voice_audit':json.loads((P/'public/audio/timing.json').read_text()),'publishing':'local draft only'}
(R/'verification.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
