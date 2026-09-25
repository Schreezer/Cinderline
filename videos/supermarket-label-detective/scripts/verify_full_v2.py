from pathlib import Path
import json, subprocess
from PIL import Image, ImageDraw
P=Path(__file__).resolve().parents[1]; R=P/'renders'; video=R/'flip-it-then-pick-it.qwen-v2.en.mp4'
def run(args): return subprocess.run(args,check=True,capture_output=True,text=True)
meta=json.loads(run(['ffprobe','-v','error','-show_format','-show_streams','-of','json',str(video)]).stdout)
v=next(s for s in meta['streams'] if s['codec_type']=='video'); a=next(s for s in meta['streams'] if s['codec_type']=='audio')
assert v['width']==1080 and v['height']==1920 and v['r_frame_rate']=='30/1'
assert abs(float(meta['format']['duration'])-38.7)<.1
assert float(a['duration'])>=38.0
run(['ffmpeg','-v','error','-i',str(video),'-f','null','-'])
folder=R/'qwen-v2-frames'; folder.mkdir(exist_ok=True)
times=[.5,6.1,12.2,19.6,26.65,31.65,37.6,38.6]
sheet=Image.new('RGB',(4*324,2*600),'#181818'); d=ImageDraw.Draw(sheet)
for i,t in enumerate(times):
    dst=folder/f'{i:02d}-{t}.jpg'
    run(['ffmpeg','-y','-v','error','-ss',str(t),'-i',str(video),'-frames:v','1','-q:v','2',str(dst)])
    im=Image.open(dst).resize((324,576)); x=(i%4)*324;y=(i//4)*600
    sheet.paste(im,(x,y+24));d.text((x+8,y+5),f'{t:.2f}s',fill='white')
sheet.save(R/'qwen-v2-encoded-contact-sheet.jpg',quality=92)
report={'file':str(video),'duration':meta['format']['duration'],'video':{k:v[k] for k in ['codec_name','width','height','r_frame_rate','nb_frames']},'audio':{k:a[k] for k in ['codec_name','sample_rate','channels','duration']},'decode_errors':0,'script_word_audit':json.loads((P/'public/audio/full-qwen-v2/timing.json').read_text())['word_audit'],'sample_times':times,'renderer':'HyperFrames 0.8.61; updated from 0.8.60; check passed with existing caption-track structure warning.'}
(R/'qwen-v2-verification.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
