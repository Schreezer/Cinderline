from pathlib import Path
import json, html

P=Path(__file__).resolve().parents[1]
config=json.loads((P/'public/audio/full-qwen-v2/timing.json').read_text())
TIMES=config['times']
DURATION=TIMES[-1]
TITLES=['The shelf appeal','Natural','Multigrain','No added sugar','Compare fairly','Flip it first']
SOURCES=['Fictional packs · real questions','Source: FDA · Natural food labeling','Source: NDSU Extension · Whole grains','Sources: AHA + FSSAI · Sugar claims','Illustrative values · compare similar foods','Sources & production notes included']
lines=json.loads((P/config['lines']).read_text())
words=json.loads((P/config['transcript']).read_text())
cream='#efe7d4'; green='#243a21'; pink='#e89cb1'; ink='#1a1a17'

CSS='''
*{box-sizing:border-box}html,body{margin:0;overflow:hidden;width:1080px;height:1920px;background:#243a21}
@font-face{font-family:'Source Serif 4';src:url('public/fonts/SourceSerif4.ttf')}@font-face{font-family:'JetBrains Mono';src:url('public/fonts/JetBrainsMono.ttf')}
#root{width:100%;height:100%;position:relative;overflow:hidden;font-family:'Source Serif 4',serif;color:#efe7d4}
.clip{position:absolute;inset:0}.scene{position:absolute;inset:0;overflow:hidden}.bg{position:absolute;inset:0}
.top{position:absolute;left:76px;right:76px;top:90px;display:flex;justify-content:space-between;align-items:center;border-bottom:2px solid currentColor;padding-bottom:27px;font:26px 'JetBrains Mono',monospace;letter-spacing:2px}
.num{font-size:25px}.head{position:absolute;left:76px;right:76px;top:225px;font-size:107px;line-height:1.03;letter-spacing:-4px;font-weight:500;margin:0}
.eyebrow{font:27px 'JetBrains Mono',monospace;letter-spacing:2px;text-transform:uppercase}.small{font:27px 'JetBrains Mono',monospace;line-height:1.5}
.source{position:absolute;left:76px;right:76px;top:1797px;font:23px 'JetBrains Mono',monospace;line-height:1.5;letter-spacing:0}
.pill{display:inline-block;padding:16px 28px;border:2px solid currentColor;border-radius:7px;font:29px 'JetBrains Mono',monospace}
.caption-layer{position:absolute;inset:0;z-index:30;pointer-events:none}.cap{position:absolute;left:90px;right:90px;top:1650px;text-align:center;display:flex;justify-content:center;align-items:center;min-height:95px;opacity:0}
.cap span{display:inline-block;background:#1a1a17;color:#efe7d4;padding:19px 28px;border-radius:8px;font:39px/1.32 'JetBrains Mono',monospace;max-width:900px}
.sheet{background:#efe7d4;color:#243a21;border:2px solid #243a21;border-radius:8px;padding:48px}.row{display:flex;justify-content:space-between;align-items:center;border-top:2px solid currentColor;padding:25px 0;font-size:51px}
.number{font-size:130px;line-height:1}.label{font:26px 'JetBrains Mono',monospace;line-height:1.5}.serif{font-family:'Source Serif 4',serif}.note{font-size:49px;line-height:1.25}
.rule{position:absolute;left:76px;right:76px;height:2px;background:currentColor}.stage{position:absolute;left:76px;right:76px;top:600px;height:840px}
'''

def body(i,content,bg=green,fg=cream):
    return f'<div class="bg" style="background:{bg}"></div><div class="scene" style="color:{fg}"><div class="top"><span>THE LABEL CHECK</span><span class="num">{i+1:02d} / 06</span></div>{content}<div class="source">{SOURCES[i]}</div></div>'

leaf='<svg width="180" height="180" viewBox="0 0 180 180" fill="none"><path d="M42 132C10 64 74 25 150 30C158 115 109 159 42 132Z" fill="#e89cb1"/><path d="M30 158L130 49M61 124L60 85M89 94L119 94" stroke="#243a21" stroke-width="7" stroke-linecap="round"/></svg>'
grain='<svg width="180" height="260" viewBox="0 0 180 260" fill="none"><path d="M90 245V25" stroke="currentColor" stroke-width="7"/><g fill="currentColor"><ellipse cx="62" cy="66" rx="18" ry="37" transform="rotate(-40 62 66)"/><ellipse cx="119" cy="99" rx="18" ry="37" transform="rotate(40 119 99)"/><ellipse cx="62" cy="140" rx="18" ry="37" transform="rotate(-40 62 140)"/><ellipse cx="119" cy="176" rx="18" ry="37" transform="rotate(40 119 176)"/></g></svg>'

contents=[]; anim=[]
contents.append(body(0,'''
<div id="f1-photo" style="position:absolute;left:0;top:235px;width:1080px;height:1447px;overflow:hidden"><img src="public/groceries.jpeg" style="width:1080px;height:1447px;object-fit:cover"/></div>
<div style="position:absolute;left:0;right:0;top:1620px;height:300px;background:#243a21"></div>
<h1 class="head" style="top:235px;font-size:113px" id="f1-title">Healthy-looking.</h1>
<div id="f1-question" style="position:absolute;top:370px;left:78px;font-size:86px;color:#e89cb1">But look closer.</div>
<div id="f1-tags" style="position:absolute;inset:0;color:#243a21"><div class="pill" style="position:absolute;top:820px;left:55px;background:#efe7d4">NATURAL</div><div class="pill" style="position:absolute;top:955px;left:348px;background:#efe7d4">MULTIGRAIN</div><div class="pill" style="position:absolute;top:1100px;right:56px;background:#efe7d4;font-size:25px">NO ADDED SUGAR</div></div>
<div id="f1-turn" class="pill" style="position:absolute;left:270px;top:1495px;background:#e89cb1;color:#243a21">TURN THE PACK ↻</div>
'''))
anim.append("tl.fromTo('#f1-photo',{scale:1.08},{scale:1,duration:2.6,ease:'power2.out'},0);tl.fromTo('#f1-title',{y:45,opacity:0},{y:0,opacity:1,duration:.65},.05);tl.fromTo('#f1-tags .pill',{y:32,opacity:0},{y:0,opacity:1,stagger:.3,duration:.5},.4);tl.fromTo('#f1-question',{y:30,opacity:0},{y:0,opacity:1,duration:.5},2.7);tl.fromTo('#f1-turn',{scale:.8,opacity:0},{scale:1,opacity:1,duration:.5,ease:'back.out(1.2)'},5.15);")

contents.append(body(1,f'''
<h1 class="head" id="f2-title">“Natural”</h1><div id="f2-sub" style="position:absolute;left:78px;top:365px;font-size:66px;color:#e89cb1">isn’t a nutrition score.</div>
<div style="position:absolute;left:170px;top:565px;width:740px;height:820px;perspective:2800px">
<div id="f2-front" class="sheet" style="opacity:0;position:absolute;inset:0;background:#3a5a36;color:#efe7d4;text-align:center;border-color:#efe7d4"><div style="margin:62px auto 30px;width:180px">{leaf}</div><div style="font-size:91px">Natural</div><div class="eyebrow" style="margin-top:40px">A claim on the front</div></div>
<div id="f2-back" class="sheet" style="opacity:0;position:absolute;inset:0"><div class="eyebrow">Look for the amounts</div><div style="font-size:72px;margin:24px 0 40px">Nutrition</div><div class="row" id="f2-row1"><span>Sugars</span><span>?</span></div><div class="row" id="f2-row2"><span>Salt / sodium</span><span>?</span></div><div class="row" id="f2-row3"><span>Saturated fat</span><span>?</span></div><div class="label" style="margin-top:35px"><div data-layout-allow-overlap>THE WORD ALONE</div><div data-layout-allow-overlap>DOESN’T TELL YOU.</div></div></div></div>
<div class="note" id="f2-note" style="position:absolute;left:90px;right:90px;top:1440px;text-align:center">Read the nutritional picture.</div>
'''))
anim.append("tl.fromTo('#f2-title',{y:40,opacity:0},{y:0,opacity:1,duration:.5},0);tl.fromTo('#f2-sub',{opacity:0,y:20},{opacity:1,y:0,duration:.5},.65);tl.fromTo('#f2-front',{opacity:0,scale:.74,rotationY:0,y:24},{opacity:1,scale:1,y:0,duration:.7,ease:'power3.out'},.1);tl.to('#f2-front',{rotationY:-90,opacity:0,duration:.55,ease:'power2.in'},1.9);tl.fromTo('#f2-back',{rotationY:90,opacity:0},{rotationY:0,opacity:1,duration:.65,ease:'power2.out'},2.35);tl.fromTo('#f2-row1',{opacity:0,x:30},{opacity:1,x:0,duration:.35},3.55);tl.fromTo('#f2-row2',{opacity:0,x:30},{opacity:1,x:0,duration:.35},4.0);tl.fromTo('#f2-row3',{opacity:0,x:30},{opacity:1,x:0,duration:.35},4.5);tl.fromTo('#f2-note',{opacity:0},{opacity:1,duration:.4},5.35);")

contents.append(body(2,f'''
<h1 class="head" id="f3-title">“Multigrain”</h1><div id="f3-sub" style="position:absolute;left:78px;top:365px;font-size:63px">More grains. Whole grains?</div>
<div id="f3-grains" style="position:absolute;left:200px;top:540px;width:680px;display:flex;justify-content:space-between"><div>{grain}<div class="eyebrow">WHEAT</div></div><div style="color:#3a5a36">{grain}<div class="eyebrow">OATS</div></div></div>
<div id="f3-eq" style="position:absolute;left:80px;right:80px;top:920px;text-align:center;font-size:74px">Multigrain <span style="color:#8b354f">≠</span> whole grain</div>
<div id="f3-ingredients" class="sheet" style="position:absolute;left:76px;right:76px;top:1085px;height:400px"><div class="eyebrow">Check the ingredients</div><div style="margin-top:40px;font-size:65px"><span id="f3-highlight" style="background:#e89cb1;padding:0 12px">Whole wheat</span> flour</div><div style="margin-top:28px;font-size:59px">or <span style="background:#e89cb1;padding:0 12px">whole oats</span></div><div class="label" style="margin-top:25px">Look near the start of the list.</div></div>
''',cream,green))
anim.append("tl.fromTo('#f3-title',{opacity:0,y:35},{opacity:1,y:0,duration:.5},0);tl.fromTo('#f3-sub',{opacity:0},{opacity:1,duration:.4},.4);tl.fromTo('#f3-grains>div',{scale:.6,opacity:0},{scale:1,opacity:1,duration:.6,stagger:.45,ease:'back.out(1.2)'},.2);tl.fromTo('#f3-eq',{opacity:0,y:25},{opacity:1,y:0,duration:.45},2.25);tl.fromTo('#f3-ingredients',{y:100,opacity:0},{y:0,opacity:1,duration:.6},4.05);")

contents.append(body(3,'''
<h1 class="head" id="f4-title" style="font-size:103px">“No added sugar”</h1><div id="f4-sub" style="position:absolute;left:78px;top:370px;font-size:74px">doesn’t mean sugar-free.</div>
<div id="f4-carton" style="position:absolute;left:98px;top:650px;width:260px;height:635px;background:#243a21;color:#efe7d4;border:3px solid #243a21"><div style="height:95px;background:#3a5a36;border-bottom:3px solid #efe7d4"></div><div class="eyebrow" style="padding:55px 28px;font-size:31px;line-height:1.4"><div data-layout-allow-overlap>FRUIT</div><div data-layout-allow-overlap>JUICE</div></div><svg style="margin-left:53px" width="150" height="150" viewBox="0 0 150 150"><circle cx="75" cy="75" r="68" fill="#efe7d4"/><path d="M75 14V136M14 75H136M32 32L118 118M32 118L118 32" stroke="#243a21" stroke-width="4"/></svg></div>
<div id="f4-panel" class="sheet" style="position:absolute;left:398px;top:610px;width:585px;height:785px;padding:42px"><div class="eyebrow">Illustrative label</div><div class="label" style="margin:20px 0 38px">PER 100 mL</div><div id="f4-total" style="border-top:3px solid #243a21;padding-top:28px"><div style="font-size:47px">Total sugars</div><div class="number" style="margin:5px 0 35px">9 g</div></div><div id="f4-added" style="border-top:2px solid #243a21;padding-top:28px"><div style="font-size:43px">Added sugars</div><div class="number" style="margin-top:10px">0 g</div></div></div>
<div id="f4-note" class="note" style="position:absolute;top:1460px;left:90px;right:90px;text-align:center">Naturally occurring sugars can remain.</div>
''',pink,green))
anim.append("tl.fromTo('#f4-title',{opacity:0,y:35},{opacity:1,y:0,duration:.5},0);tl.fromTo('#f4-sub',{opacity:0,y:25},{opacity:1,y:0,duration:.5},.95);tl.fromTo('#f4-carton',{x:-120,rotation:-10,opacity:0},{x:0,rotation:0,opacity:1,duration:.65},.2);tl.fromTo('#f4-panel',{rotationY:35,x:50,opacity:0},{rotationY:0,x:0,opacity:1,duration:.7},1.8);tl.fromTo('#f4-added',{opacity:0},{opacity:1,duration:.4},2.1);tl.fromTo('#f4-total',{opacity:0,y:20},{opacity:1,y:0,duration:.5},3.0);tl.fromTo('#f4-note',{opacity:0},{opacity:1,duration:.5},4.9);")

contents.append(body(4,'''
<h1 class="head" id="f5-title">Compare fairly.</h1><div style="position:absolute;left:78px;top:365px;font-size:64px" id="f5-sub">Same food type. Same amount.</div>
<div id="f5-pair" style="position:absolute;left:76px;right:76px;top:590px;display:flex;gap:28px">
<div class="sheet" style="width:450px;padding:35px;height:650px;background:#e6dcc4"><div class="eyebrow">CEREAL A</div><div class="label" style="margin-top:55px">SUGARS</div><div class="number" style="margin-top:25px">6 g</div><div class="label" style="margin-top:25px">PER 30 g</div><div class="normal" style="border-top:3px solid #243a21;margin-top:55px;padding-top:35px;font-size:65px">20 g<span style="display:block;font:26px 'JetBrains Mono'">PER 100 g</span></div></div>
<div class="sheet" style="width:450px;padding:35px;height:650px;background:#e89cb1"><div class="eyebrow">CEREAL B</div><div class="label" style="margin-top:55px">SUGARS</div><div class="number" style="margin-top:25px">8 g</div><div class="label" style="margin-top:25px">PER 40 g</div><div class="normal" style="border-top:3px solid #243a21;margin-top:55px;padding-top:35px;font-size:65px">20 g<span style="display:block;font:26px 'JetBrains Mono'">PER 100 g</span></div></div></div>
<div id="f5-equal" class="pill" style="position:absolute;top:1295px;left:178px;background:#243a21;color:#efe7d4">SAME SUGAR CONCENTRATION</div>
<div id="f5-portion" class="note" style="position:absolute;top:1450px;left:100px;right:100px;text-align:center">Then check your actual portion.</div>
''',cream,green))
anim.append("tl.fromTo('#f5-title',{opacity:0,y:40},{opacity:1,y:0,duration:.5},0);tl.fromTo('#f5-sub',{opacity:0},{opacity:1,duration:.45},.5);tl.fromTo('#f5-pair>.sheet',{y:75,opacity:0},{y:0,opacity:1,duration:.6,stagger:.2},.2);tl.fromTo('#f5-pair .normal',{opacity:0,y:30},{opacity:1,y:0,duration:.55,stagger:.1},2.5);tl.fromTo('#f5-equal',{opacity:0,scale:.8},{opacity:1,scale:1,duration:.45},3.2);tl.fromTo('#f5-portion',{opacity:0,y:20},{opacity:1,y:0,duration:.5},4.2);")

contents.append(body(5,'''
<div id="f6-image" style="position:absolute;left:76px;top:225px;width:928px;height:730px;overflow:hidden;border:2px solid #efe7d4"><img data-layout-allow-overflow src="public/groceries.jpeg" style="width:928px;height:1243px;object-fit:cover;position:absolute;top:-350px"/></div>
<div id="f6-title" style="position:absolute;left:76px;top:1000px;font-size:117px;line-height:.99;letter-spacing:-3px">Flip it.<span style="display:block;color:#e89cb1">Then pick it.</span></div>
<div id="f6-steps" style="position:absolute;left:76px;right:76px;top:1330px;display:flex;flex-direction:column;gap:17px"><div class="eyebrow">01 &nbsp; Ingredients</div><div class="eyebrow">02 &nbsp; Nutrition information</div><div class="eyebrow">03 &nbsp; Your portion</div></div>
<div id="f6-save" style="position:absolute;top:1545px;left:76px;font-size:38px">A small habit for your next shop.</div>
'''))
anim.append("tl.fromTo('#f6-image',{opacity:0,scale:1.04},{opacity:1,scale:1,duration:.7},0);tl.fromTo('#f6-title',{opacity:0,y:60},{opacity:1,y:0,duration:.6},1.1);tl.fromTo('#f6-steps>div',{opacity:0,x:30},{opacity:1,x:0,duration:.4,stagger:.25},2.2);tl.fromTo('#f6-save',{opacity:0},{opacity:1,duration:.4},4.6);")

# Reveal each explanation with the corresponding spoken phrase.
retiming = [
    {2.7:3.32, 5.15:4.98},
    {1.9:1.05, 2.35:1.55, 3.55:2.83, 4.0:3.61, 4.5:4.1, 5.35:5.0},
    {2.25:3.65, 4.05:4.73},
    {.95:1.65, 3.0:5.33, 4.9:2.76},
    {2.5:2.39, 3.2:3.3, 4.2:3.68},
    {1.1:1.41, 2.2:2.7, 4.6:4.36},
]
import re
for i, mapping in enumerate(retiming):
    anim[i]=re.sub(r",([0-9.]+)\);",lambda m: ','+str(mapping.get(float(m[1]),float(m[1])))+');',anim[i])
anim[0]=re.sub(r"tl.fromTo\('#f1-tags \.pill'.*?\);",'',anim[0])
for n,t in enumerate([.18,1.23,2.0],1):
    anim[0]+=f"tl.fromTo('#f1-tags .pill:nth-child({n})',{{y:32,opacity:0}},{{y:0,opacity:1,duration:.35}},{t});"

for i,(content,animation) in enumerate(zip(contents,anim)):
    cid=f'frame-{i+1:02d}'; dur=TIMES[i+1]-TIMES[i]
    sub=f'''<!doctype html><html><body><template><style>#{cid}-root{{position:absolute;inset:0;width:100%;height:100%}}</style><div id="{cid}-root" data-composition-id="{cid}" data-width="1080" data-height="1920" data-duration="{dur}"><div id="{cid}-content" class="clip" data-start="0" data-duration="{dur}" data-track-index="0">{content}</div></div><script>{{const tl=gsap.timeline({{paused:true}});{animation}window.__timelines['{cid}']=tl;}}</script></template></body></html>'''
    (P/f'compositions/frames/{i+1:02d}.html').write_text(sub)

# Phrase captions aligned to actual narration. Short groups avoid tiny mobile text.
# Phrase boundaries follow the intended sentences and breath groups.
phrase_lengths=[1,1,3,4,5, 3,5,4,5, 7,2,2,4,6, 6,3,3,4,4, 3,4,4,4,3, 5,5,2,2,6]
assert sum(phrase_lengths)==len(words)
groups=[]; cursor=0
for count in phrase_lengths:
    groups.append(words[cursor:cursor+count]); cursor+=count
caps=''; capanim=''; srt=[]
def stamp(sec):
    ms=round(sec*1000);return f'{ms//3600000:02d}:{ms//60000%60:02d}:{ms//1000%60:02d},{ms%1000:03d}'
for i,g in enumerate(groups):
    start=g[0]['start']; end=groups[i+1][0]['start'] if i+1<len(groups) else DURATION-.1
    text=' '.join(x['text'] for x in g)
    caps+=f'<div class="cap" id="cap-{i}"><span>{html.escape(text)}</span></div>'
    capanim+=f"tl.set('#cap-{i}',{{opacity:1}},{start});tl.set('#cap-{i}',{{opacity:0}},{end});"
    srt.append(f'{i+1}\n{stamp(start)} --> {stamp(end)}\n{text}\n')
(P/config['srt']).write_text('\n'.join(srt))
hosts=''.join(f'<div id="host-{i+1:02d}" class="clip" data-composition-id="frame-{i+1:02d}" data-composition-src="compositions/frames/{i+1:02d}.html" data-start="{TIMES[i]}" data-duration="{TIMES[i+1]-TIMES[i]}" data-track-index="1" data-width="1080" data-height="1920"></div>' for i in range(6))
sounds=''.join(f'<audio id="click-{i}" src="public/audio/click.mp3" data-start="{t}" data-duration="0.35" data-volume="0.1" data-track-index="4"></audio>' for i,t in enumerate(config['click_times']))
sounds+=''.join(f'<audio id="whoosh-{i}" src="public/audio/whoosh.mp3" data-start="{t}" data-duration="0.57" data-volume="0.08" data-track-index="5"></audio>' for i,t in enumerate(round(t-.15,3) for t in TIMES[1:-1]))
main=f'''<!doctype html><html lang="en"><head><meta charset="UTF-8"><script src="public/gsap.min.js"></script><style>{CSS}</style></head><body><div id="root" data-composition-id="main" data-width="1080" data-height="1920" data-duration="{DURATION}">{hosts}<div id="caption-layer" class="clip caption-layer" data-start="0" data-duration="{DURATION}" data-track-index="3">{caps}</div><div style="position:absolute;left:76px;right:76px;top:1870px;height:4px;background:#65725b;z-index:35"><div id="progress" style="height:4px;width:100%;background:#e89cb1;transform-origin:left"></div></div><audio id="narration" src="{config['audio_src']}" data-start="0" data-duration="{config['audio_duration']}" data-volume="1" data-track-index="2"></audio>{sounds}</div><script>const tl=gsap.timeline({{paused:true}});{capanim}tl.fromTo('#progress',{{scaleX:0}},{{scaleX:1,duration:{DURATION},ease:'none'}},0);window.__timelines['main']=tl;</script></body></html>'''
(P/'index.html').write_text(main)
(P/'capture/extracted/visible-text.txt').write_text('User selected the supermarket-label short, using HTML animation and Google Flow still images. No speaking people.\n\n'+'\n'.join(lines))
(P/'capture/extracted/tokens.json').write_text(json.dumps({'title':'Flip it. Then pick it.','description':'Three food label claims, explained','colors':[],'fonts':[]}))
story=f'---\nformat: 1080x1920\nduration: {DURATION}s\nmessage: Read beyond the front label.\narc: listicle\nmode: autonomous\nmusic: none\n---\n\n## Video direction\nEditorial Forest palette and typography. Flow still life opens the piece; HTML explanations carry the claims. Physical label rotation is adapted from ui-3d-reveal. All labels and numbers are authored text. Six scenes timed to measured local Qwen narration. Captions occupy y=1650–1760. Sources at y=1797.\n'
for i in range(6):
    story+=f'\n## Frame {i+1} — {TITLES[i]}\n\n- status: animated\n- src: compositions/frames/{i+1:02d}.html\n- duration: {TIMES[i+1]-TIMES[i]:.2f}s\n- transition_in: cut\n- type: '+('hook' if i==0 else 'cta' if i==5 else 'feature_showcase')+f'\n- persuasion: concrete demonstration\n- beat: curiosity into understanding\n- blueprint: compose\n- scene: {TITLES[i]}\n- voiceover: "{lines[i]}"\n- poster: 3s\n\nScene 1 (0–1.5s): establish the key label with a paced text entrance.\nScene 2 (1.5–4s): reveal the mechanism or information behind the claim.\nScene 3 (4s–end): reveal the practical reading cue and hold.\n'
(P/'STORYBOARD.md').write_text(story)
(P/'SCRIPT.md').write_text('# Script\n\n**Voice:** Local Qwen3-TTS 1.7B Base BF16, conditioned on the approved VoiceDesign BF16 synthetic reference; one continuous generation\n**Voice direction:** Conversational, clear English.\n\n'+'\n\n'.join(f'## Line {i+1} — {TITLES[i]} (Frame {i+1})\n\n**Time:** {TIMES[i]}–{TIMES[i+1]}s\n\n    {line}' for i,line in enumerate(lines)))
print(f'Built 6 scenes, {DURATION} seconds, phrase captions and source notes.')
