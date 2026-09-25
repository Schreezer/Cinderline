from pathlib import Path
import json,html
P=Path(__file__).resolve().parents[1]
timing=json.loads((P/'public/audio/timing.json').read_text())
times=timing['times'];words=json.loads((P/'public/audio/transcript.aligned.json').read_text())
assets=['01-applicant','02-team','03-infographic','04-evidence','05-brew','02-team']
# Timed phrase captions use measured word starts and preserve deliberate spoken pauses.
phrases=[(0,5),(5,9),(9,13),(13,16),(16,19),(19,24),(24,28),(28,31),(31,34),(34,38),(38,42),(42,45),(45,47),(47,51),(51,54),(54,58),(58,61),(61,65)]
groups=[]
for j,(a,b) in enumerate(phrases):
 s=words[a]['start'];e=min(words[b-1]['end']+.12,words[phrases[j+1][0]]['start']-.035) if j+1<len(phrases) else 27.2
 groups.append({'start':round(s,3),'end':round(max(s+.12,e),3),'text':' '.join(w['text'] for w in words[a:b])})
(P/'public/audio/caption-groups.json').write_text(json.dumps(groups,indent=2))
for i,(a,b,asset) in enumerate(zip(times,times[1:],assets),1):
 d=round(b-a,3);cid=f'frame-{i:02d}';special='';js=''
 # Camera moves adapted from the installed push-in component: eased entrance, linear continuation.
 if i in [1,2,4,5,6]:
  endscale={1:1.035,2:1.025,4:1.025,5:1.035,6:1.02}[i]
  js+=f"tl.fromTo('#{cid}-camera',{{scale:1}},{{scale:{endscale},duration:{d},ease:'sine.inOut'}},0);"
 else:
  # Infographic remains still for legibility; emphasis follows the two organ roles.
  special=f'''<svg id="{cid}-emphasis" width="1080" height="1920" viewBox="0 0 1080 1920" style="position:absolute;inset:0;pointer-events:none" data-layout-ignore="true"><rect id="{cid}-liver-focus" x="70" y="370" width="940" height="430" rx="24" fill="none" stroke="#b76548" stroke-width="6"/><rect id="{cid}-kidney-focus" x="70" y="825" width="940" height="715" rx="24" fill="none" stroke="#b76548" stroke-width="6"/></svg>'''
  js+=f"tl.fromTo('#{cid}-liver-focus',{{opacity:0}},{{opacity:.8,duration:.35}},.1).to('#{cid}-liver-focus',{{opacity:0,duration:.25}},2.65);tl.fromTo('#{cid}-kidney-focus',{{opacity:0}},{{opacity:.8,duration:.35}},3).to('#{cid}-kidney-focus',{{opacity:0,duration:.25}},{d-.3});"
 if i==4:
  special+=f'''<div id="{cid}-evidence-accent" style="position:absolute;left:175px;top:1410px;width:730px;height:7px;background:#ba6949;transform-origin:left;border-radius:5px" data-layout-ignore="true"></div>'''
  js+=f"tl.fromTo('#{cid}-evidence-accent',{{scaleX:0}},{{scaleX:1,duration:.35,ease:'power2.out'}},2.3);"
 if i==6:
  special+=f'''<div id="{cid}-end" style="position:absolute;left:80px;right:80px;top:1500px;text-align:center;background:#243a21;color:#efe7d4;padding:28px 20px;font:700 61px/1.1 'Source Serif 4',serif;border-radius:8px">Already on the job.</div>'''
  js+=f"tl.fromTo('#{cid}-end',{{y:25,opacity:0}},{{y:0,opacity:1,duration:.5,ease:'power3.out'}},3.0);"
 frame=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"></head><body><template><style>#root{{position:absolute;inset:0;width:100%;height:100%;overflow:hidden}}#{cid}-camera{{position:absolute;inset:0;transform-origin:50% 45%;will-change:transform}}#{cid}-art{{width:100%;height:100%;object-fit:cover;display:block}}</style><div id="root" data-composition-id="{cid}" data-width="1080" data-height="1920" data-duration="{d}"><div class="clip" id="{cid}-back" data-start="0" data-duration="{d}" data-track-index="0" style="position:absolute;inset:0;background:#efe7d4"><div id="{cid}-camera"><img id="{cid}-art" src="public/art/{asset}.jpeg" alt="Original illustrated scene {i}"/></div>{special}</div></div><script>(function(){{const tl=gsap.timeline({{paused:true}});{js}window.__timelines['{cid}']=tl;}})();</script></template></body></html>'''
 (P/f'compositions/frames/{i:02d}.html').write_text(frame)
head='''<!doctype html><html lang="en"><head><meta charset="UTF-8"><script src="public/gsap.min.js"></script><style>*{box-sizing:border-box}html,body{margin:0;width:1080px;height:1920px;overflow:hidden;background:#efe7d4}@font-face{font-family:'Source Serif 4';src:url('public/fonts/SourceSerif4.ttf')}@font-face{font-family:'JetBrains Mono';src:url('public/fonts/JetBrainsMono.ttf')}#root{width:100%;height:100%;position:relative;overflow:hidden;background:#efe7d4;color:#243a21}.clip{position:absolute;inset:0}.cap{position:absolute;left:72px;right:72px;top:1585px;min-height:105px;display:flex;align-items:center;justify-content:center;text-align:center;opacity:0}.cap span{font:43px/1.3 'JetBrains Mono',monospace;padding:18px 24px;background:#243a21;color:#fff8e8;border-radius:9px;max-width:936px}.brand{position:absolute;left:62px;top:25px;font:22px/1.3 'JetBrains Mono',monospace;letter-spacing:3px}.source{position:absolute;left:62px;right:205px;top:1770px;font:22px/1.5 'JetBrains Mono',monospace;color:#243a21}.progress{position:absolute;left:62px;right:62px;top:1860px;height:4px;background:#b9bd9f}.progress-inner{width:100%;height:4px;transform-origin:left;background:#243a21}</style></head><body><div id="root" data-composition-id="main" data-width="1080" data-height="1920" data-duration="30">'''
hosts=''.join(f'<div id="host-{i:02}" class="clip" data-composition-id="frame-{i:02}" data-composition-src="compositions/frames/{i:02}.html" data-start="{a}" data-duration="{round(b-a,3)}" data-track-index="1" data-width="1080" data-height="1920"></div>' for i,(a,b) in enumerate(zip(times,times[1:]),1))
caps='<div id="caption-layer" class="clip" data-start="0" data-duration="30" data-track-index="3" style="z-index:30">'+''.join(f'<div class="cap" id="cap-{i}"><span>{html.escape(g["text"])}</span></div>' for i,g in enumerate(groups))+'</div>'
brand='<div id="brand-layer" class="clip" data-start="0" data-duration="30" data-track-index="5" style="z-index:40;pointer-events:none"><div class="brand">FITSNAP · BODY HQ</div><div class="source">Science: NIH · NIDDK · NHS<br style="display:none"> </div><div class="progress"><div class="progress-inner" id="progress"></div></div></div>'
# No prose line breaks: a single source line remains inside the safe footer.
brand=brand.replace('<br style="display:none"> ','')
audio='<audio id="narration" src="public/audio/voice.wav" data-start="0.4" data-duration="26.32" data-volume="1" data-track-index="2"></audio>'
for i,t in enumerate([.42,6.02,10.45,15.72,27.4]):audio+=f'<audio id="paper-{i}" src=".media/audio/sfx/sfx_001.mp3" data-start="{t}" data-duration=".4" data-volume=".075" data-track-index="4"></audio>'
js="const tl=gsap.timeline({paused:true});tl.fromTo('#progress',{scaleX:0},{scaleX:1,duration:30,ease:'none'},0);"
for i,g in enumerate(groups):js+=f"tl.set('#cap-{i}',{{opacity:1}},{g['start']}).set('#cap-{i}',{{opacity:0}},{g['end']});"
(P/'index.html').write_text(head+hosts+caps+brand+audio+f"</div><script>{js}window.__timelines['main']=tl;</script></body></html>")
def stamp(t):
 ms=round(t*1000);return f'{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02},{ms%1000:03}'
(P/'renders/subtitles.en.srt').write_text('\n\n'.join(f'{i+1}\n{stamp(g["start"])} --> {stamp(g["end"])}\n{g["text"]}' for i,g in enumerate(groups))+'\n')
print('Built six scenes, captions and 30-second master.')
