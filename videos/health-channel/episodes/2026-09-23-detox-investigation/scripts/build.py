from pathlib import Path
import json,html
P=Path(__file__).resolve().parents[1];A=P/'public/audio'
OFF=0.5;TOTAL=27.0
gen=json.loads((A/'generation.json').read_text());DUR=round(gen['duration'],3)
raw=json.loads((A/'takes/question-s42.words.json').read_text());raw=raw if isinstance(raw,list) else raw['words']
script=("In 2009, some young scientists called fifteen detox brands. Two questions. What does detox mean? And where's the proof? "
 "No two brands meant the same thing. Most had no proof. Oh, and one of them? It was a hair straightener. "
 "Detox teas are no different. Often, it's senna. A laxative. You lose water, not toxins. "
 "The real detox? Your liver and kidneys. No straightener required.").split()
assert len(script)==len(raw),(len(script),len(raw))
words=[]
for s,r in zip(script,raw):
    st=min(r['start'],DUR);en=min(max(r['end'],st+.05),DUR)
    words.append({'text':s,'asr':r['text'],'start':round(st+OFF,3),'end':round(en+OFF,3)})
(A/'transcript.aligned.json').write_text(json.dumps(words,indent=2))
phr=[(0,5),(5,9),(9,11),(11,15),(15,19),(19,26),(26,30),(30,35),(35,40),(40,45),(45,50),(50,55),(55,58),(58,62),(62,65)]
groups=[]
for j,(a,b) in enumerate(phr):
    s=words[a]['start']
    e=min(words[b-1]['end']+.3,words[phr[j+1][0]]['start']-.08) if j+1<len(phr) else min(words[-1]['end']+1.2,TOTAL-.3)
    groups.append({'start':round(s-.05,3),'end':round(max(s+.4,e),3),'text':' '.join(w['text'] for w in words[a:b])})
(A/'caption-groups.json').write_text(json.dumps(groups,indent=2))
W=lambda i:words[i]['start']
# scene cuts slightly before the first word of each beat
cuts=[0,W(9)-.15,W(19)-.15,W(30)-.15,W(40)-.15,W(55)-.15,W(62)-.1,TOTAL]
cuts=[round(c,3) for c in cuts]
assets=['01-office','02-questions','03-answers','04-straightener','05-tea','07-organs','06-ending']
timing={'times':cuts,'audio_start':OFF,'audio_duration':DUR,'total':TOTAL,'gain_db':4.5,'take':'question-s42','script_words':len(script)}
(A/'timing.json').write_text(json.dumps(timing,indent=2))
cams={1:('50% 45%',1.0,1.045),2:('50% 33%',1.0,1.08),3:('50% 45%',1.0,1.04),5:('26% 45%',1.0,1.12),6:('50% 40%',1.0,1.0),7:('50% 50%',1.0,1.04)}
for i,(a,b,asset) in enumerate(zip(cuts,cuts[1:],assets),1):
    d=round(b-a,3);cid=f'frame-{i:02d}';special='';js=''
    loc=lambda t:round(t-a,3)
    if i in cams:
        o,s0,s1=cams[i]
        js+=f"tl.fromTo('#{cid}-camera',{{scale:{s0}}},{{scale:{s1},duration:{d},ease:'sine.inOut'}},0);"
    if i==4:
        # quiet hold, then a quick punch-in on the DETOX sticker at the reveal
        o='32% 41%';rv=loc(W(35))
        js+=f"tl.fromTo('#{cid}-camera',{{scale:1}},{{scale:1.03,duration:{rv},ease:'none'}},0).to('#{cid}-camera',{{scale:1.22,duration:.45,ease:'power3.out'}},{rv}).to('#{cid}-camera',{{scale:1.25,duration:{round(d-rv-.45,3)},ease:'none'}},{round(rv+.45,3)});"
    if i==5:
        t1=loc(W(47))-.1;t2=loc(W(49));t3=loc(W(50))-.05;t4=round(min(t3+1.05,d-.35),3)
        special=f'''<div id="{cid}-label" style="position:absolute;left:90px;right:90px;top:1180px;background:#fff8e8;border:5px solid #243a21;border-radius:14px;padding:26px 30px;color:#243a21;opacity:0" data-layout-ignore="true"><div style="font:600 26px/1.2 'JetBrains Mono',monospace;letter-spacing:3px">BACK OF THE BOX</div><div style="font:600 46px/1.35 'Source Serif 4',serif;margin-top:10px">Green tea · <span id="{cid}-senna" style="background:linear-gradient(#e89cb1,#e89cb1) no-repeat 0 85%/0% 45%;padding:0 4px">Senna leaf</span> · Peppermint</div><div id="{cid}-lax" style="font:600 30px/1.3 'JetBrains Mono',monospace;color:#8f3a4e;margin-top:10px;opacity:0">SENNA = STIMULANT LAXATIVE</div></div>
<div id="{cid}-scale" style="position:absolute;left:190px;right:190px;top:1150px;background:#243a21;color:#efe7d4;border-radius:18px;padding:22px 26px;text-align:center;opacity:0" data-layout-ignore="true"><div style="font:600 24px/1.2 'JetBrains Mono',monospace;letter-spacing:3px">SCALE · ILLUSTRATIVE</div><div style="position:relative;height:110px;margin-top:8px"><div id="{cid}-n1" style="position:absolute;inset:0;font:700 96px/110px 'JetBrains Mono',monospace;color:#e89cb1">68.9<span style="font-size:44px"> kg</span></div><div id="{cid}-n2" style="position:absolute;inset:0;font:700 96px/110px 'JetBrains Mono',monospace;opacity:0">70.0<span style="font-size:44px"> kg</span></div></div><div id="{cid}-t1" style="font:600 30px/1.3 'JetBrains Mono',monospace">water + gut contents</div><div id="{cid}-t2" style="font:600 30px/1.3 'JetBrains Mono',monospace;color:#e89cb1;opacity:0">…back after a glass of water</div></div>'''
        js+=(f"tl.fromTo('#{cid}-label',{{y:30,opacity:0}},{{y:0,opacity:1,duration:.35,ease:'power3.out'}},{t1});"
             f"tl.to('#{cid}-senna',{{backgroundSize:'100% 45%',duration:.35,ease:'power2.out'}},{round(t1+.25,3)});"
             f"tl.to('#{cid}-lax',{{opacity:1,duration:.3}},{t2});"
             f"tl.to('#{cid}-label',{{opacity:0,duration:.2}},{t3});"
             f"tl.fromTo('#{cid}-scale',{{y:30,opacity:0}},{{y:0,opacity:1,duration:.3,ease:'power3.out'}},{round(t3+.05,3)});"
             f"tl.to('#{cid}-n1',{{opacity:0,duration:.2}},{t4}).to('#{cid}-n2',{{opacity:1,duration:.2}},{t4}).to('#{cid}-t1',{{opacity:0,duration:.2}},{t4}).to('#{cid}-t2',{{opacity:1,duration:.2}},{t4});")
    if i==6:
        tl_=round(loc(W(59))-.1,3);tk=round(loc(W(61))-.1,3)
        special=f'''<svg width="1080" height="1920" viewBox="0 0 1080 1920" style="position:absolute;inset:0;pointer-events:none" data-layout-ignore="true"><rect id="{cid}-liver" x="70" y="370" width="940" height="430" rx="24" fill="none" stroke="#b76548" stroke-width="7" opacity="0"/><rect id="{cid}-kid" x="70" y="825" width="940" height="715" rx="24" fill="none" stroke="#b76548" stroke-width="7" opacity="0"/></svg>'''
        js+=f"tl.to('#{cid}-liver',{{opacity:.9,duration:.25}},{tl_}).to('#{cid}-kid',{{opacity:.9,duration:.25}},{tk});"
    if i==7:
        special=f'''<div id="{cid}-end" style="position:absolute;left:120px;right:120px;top:1420px;text-align:center;background:#243a21;color:#efe7d4;padding:22px 20px;font:700 50px/1.15 'Source Serif 4',serif;border-radius:10px;opacity:0" data-layout-ignore="true">Your organs do the detox.</div>'''
        js+=f"tl.fromTo('#{cid}-end',{{y:20,opacity:0}},{{y:0,opacity:1,duration:.45,ease:'power3.out'}},{round(d-2.0,3)});"
    frame=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"></head><body><template><style>#root{{position:absolute;inset:0;width:100%;height:100%;overflow:hidden}}#{cid}-camera{{position:absolute;inset:0;transform-origin:{o if i in cams or i==4 else '50% 50%'};will-change:transform}}#{cid}-art{{width:100%;height:100%;object-fit:cover;display:block}}</style><div id="root" data-composition-id="{cid}" data-width="1080" data-height="1920" data-duration="{d}"><div class="clip" id="{cid}-back" data-start="0" data-duration="{d}" data-track-index="0" style="position:absolute;inset:0;background:#efe7d4"><div id="{cid}-camera"><img id="{cid}-art" src="public/art/{asset}.jpeg" alt="Original illustrated scene {i}"/></div>{special}</div></div><script>(function(){{const tl=gsap.timeline({{paused:true}});{js}window.__timelines['{cid}']=tl;}})();</script></template></body></html>'''
    (P/f'compositions/frames/{i:02d}.html').write_text(frame)
head=f'''<!doctype html><html lang="en"><head><meta charset="UTF-8"><script src="public/gsap.min.js"></script><style>*{{box-sizing:border-box}}html,body{{margin:0;width:1080px;height:1920px;overflow:hidden;background:#efe7d4}}@font-face{{font-family:'Source Serif 4';src:url('public/fonts/SourceSerif4.ttf')}}@font-face{{font-family:'JetBrains Mono';src:url('public/fonts/JetBrainsMono.ttf')}}#root{{width:100%;height:100%;position:relative;overflow:hidden;background:#efe7d4;color:#243a21}}.clip{{position:absolute;inset:0}}.cap{{position:absolute;left:72px;right:72px;top:1585px;min-height:105px;display:flex;align-items:center;justify-content:center;text-align:center;opacity:0}}.cap span{{font:43px/1.3 'JetBrains Mono',monospace;padding:18px 24px;background:#243a21;color:#fff8e8;border-radius:9px;max-width:936px}}.brand{{position:absolute;left:62px;top:25px;font:22px/1.3 'JetBrains Mono',monospace;letter-spacing:3px;background:rgba(239,231,212,.85);padding:4px 10px;border-radius:6px}}.source{{position:absolute;left:62px;right:62px;top:1760px;font:21px/1.45 'JetBrains Mono',monospace;color:#243a21}}.progress{{position:absolute;left:62px;right:62px;top:1860px;height:4px;background:#b9bd9f}}.progress-inner{{width:100%;height:4px;transform-origin:left;background:#243a21}}</style></head><body><div id="root" data-composition-id="main" data-width="1080" data-height="1920" data-duration="{TOTAL}">'''
hosts=''.join(f'<div id="host-{i:02}" class="clip" data-composition-id="frame-{i:02}" data-composition-src="compositions/frames/{i:02}.html" data-start="{a}" data-duration="{round(b-a,3)}" data-track-index="1" data-width="1080" data-height="1920"></div>' for i,(a,b) in enumerate(zip(cuts,cuts[1:]),1))
caps=f'<div id="caption-layer" class="clip" data-start="0" data-duration="{TOTAL}" data-track-index="3" style="z-index:30">'+''.join(f'<div class="cap" id="cap-{i}"><span>{html.escape(g["text"])}</span></div>' for i,g in enumerate(groups))+'</div>'
brand=f'<div id="brand-layer" class="clip" data-start="0" data-duration="{TOTAL}" data-track-index="5" style="z-index:40;pointer-events:none"><div class="brand">FITSNAP · DETOX FILES</div><div class="source">Sources: Sense about Science (2009) · NCCIH · NIDDK · FTC<br>Fictional products · illustrative numbers</div><div class="progress"><div class="progress-inner" id="progress"></div></div></div>'
audio=f'<audio id="narration" src="public/audio/voice.wav" data-start="{OFF}" data-duration="{DUR}" data-volume="1" data-track-index="2"></audio>'
for i,t in enumerate(cuts[1:-1]):audio+=f'<audio id="paper-{i}" src=".media/audio/sfx/sfx_001.mp3" data-start="{round(max(0,t-.05),3)}" data-duration=".4" data-volume=".07" data-track-index="4"></audio>'
js=f"const tl=gsap.timeline({{paused:true}});tl.fromTo('#progress',{{scaleX:0}},{{scaleX:1,duration:{TOTAL},ease:'none'}},0);"
for i,g in enumerate(groups):js+=f"tl.set('#cap-{i}',{{opacity:1}},{g['start']}).set('#cap-{i}',{{opacity:0}},{g['end']});"
(P/'index.html').write_text(head+hosts+caps+brand+audio+f"</div><script>{js}window.__timelines['main']=tl;</script></body></html>")
def stamp(t):
    ms=round(t*1000);return f'{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02},{ms%1000:03}'
(P/'renders/subtitles.en.srt').write_text('\n\n'.join(f'{i+1}\n{stamp(g["start"])} --> {stamp(g["end"])}\n{g["text"]}' for i,g in enumerate(groups))+'\n')
print('cuts',cuts);print('audio',OFF,DUR);print(json.dumps(groups))
