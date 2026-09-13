from pathlib import Path
import subprocess, time, json, re, shutil, sys, hashlib

root = Path(__file__).resolve().parents[2]
out = root / 'artifacts/tutorial-controls/mac'
out.mkdir(parents=True, exist_ok=True)
cases = [('kiln-phone', 'kiln', 956, 440, True), ('kiln-focus-phone', 'kiln-focus', 956, 440, True), ('kiln-details-phone', 'kiln-details', 956, 440, True), ('kiln-details-small', 'kiln-details', 667, 375, True), ('research-focus-phone', 'research-focus', 956, 440, True), ('research-details-small', 'research-details', 667, 375, True), ('camera-focus-phone', 'camera-focus', 956, 440, True), ('kiln-desktop', 'kiln', 1920, 1080, False)]
cases += [('defend-details-small', 'defend-details', 667, 375, True), ('assault-phone', 'assault', 956, 440, True)]
if len(sys.argv) > 1:
    cases = [c for c in cases if c[0] in sys.argv[1:]]
assert cases
protectedSaved = root / 'Saved'
protectedPaths = [protectedSaved / p for p in ('Matches/skirmish.cinder','Config/Training.ini','Config/Skirmish.ini')]
def protectedHashes():
    return {str(p.relative_to(protectedSaved)):hashlib.file_digest(p.open('rb'),'sha256').hexdigest() if p.exists() else None for p in protectedPaths}
protectedBefore=protectedHashes()
for label, state, width, height, compact in cases:
    log = out / (label + '-runtime.txt')
    shot = root / f'Saved/GuidedMatch/{state}.png'
    command = ['/Users/chirag13/Documents/ChatGPT/CinderlineEngineIOS27/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor',
               str(root / 'Cinderline.uproject'), '-game', '-windowed', '-ForceRes',
               f'-ResX={width}', f'-ResY={height}', '-nosound', '-nosplash', '-unattended',
               '-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=8',
               '-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame=8',
               f'-ExecCmds=t.MaxFPS 30,cinder.guidedpreview {state}',
               '-AssetRegistryCacheRootFolder=' + str(root / 'Saved/TutorialControls/PreviewRegistry'),
               '-abslog=' + str(log)]
    if compact: command.append('-mobilehud')
    start = time.time(); complete = False
    with (out / (label + '-stdout.txt')).open('w') as stdout:
        process = subprocess.Popen(command, cwd=root, stdout=stdout, stderr=subprocess.STDOUT, start_new_session=True)
        print(f'{label}: PID {process.pid}', flush=True)
        try:
            while process.poll() is None and time.time() - start < 180:
                current = log.read_text(errors='replace') if log.exists() and log.stat().st_mtime > start else ''
                if 'CINDERLINE_GUIDED_PREVIEW failed=' in current or 'CINDERLINE_GUIDED_PREVIEW refused=' in current:
                    break
                if shot.exists() and shot.stat().st_mtime > start and f'CINDERLINE_GUIDED_PREVIEW state={state} ' in current:
                    time.sleep(1)
                    shutil.copy2(shot, out / (label + '.png')); complete = True; break
                time.sleep(.5)
        finally:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
    assert complete, f'{label}: no fresh capture; see {log}'
    current = log.read_text(errors='replace')
    assert not re.search(r'Fatal error:|Assertion failed:|Ensure condition failed:', current), label
    selected = re.search(r'CINDERLINE_GUIDED_SELECTION id=(\d+) kind=(\w+)', current)
    assert selected, label
    if state == 'kiln-focus': assert selected[2] == 'Drudge', selected[0]
    if state == 'research-focus': assert selected[2] == 'Resonator', selected[0]
    marker = re.search(r'CINDERLINE_GUIDED_PREVIEW state=(\S+) offer=(\d) menu=(\d) step=(\d+) complete=(\d) preference_completed=(\d) winner=(-?\d+) opponent_orders=(\d+) tick=(\d+)', current)
    assert marker, label
    fields = ['offer','menu','step','complete','preferenceCompleted','winner','opponentOrders','tick']
    observed = dict(zip(fields,map(int,marker.groups()[1:])))
    if state == 'camera-focus': assert observed['step'] == 0, observed
    if state == 'offer': assert observed['offer'] == 1 and observed['menu'] == 1
    elif state == 'skip': assert observed['offer'] == 0 and observed['menu'] == 1
    elif state == 'win': assert observed['winner'] == 0 and observed['complete'] == 1 and observed['preferenceCompleted'] == 1
    else: assert observed['menu'] == 0 and observed['offer'] == 0 and observed['winner'] == -1
    header = re.search(r'CINDERLINE_MOBILE_HUD_REPORT width=([\d.]+) height=([\d.]+) scale=([\d.]+)', current)
    assert header, label
    w,h,scale = map(float,header.groups()); buttons = []
    for m in re.finditer(r'CINDERLINE_MOBILE_HUD_BUTTON index=(\d+) action=(\S+) argument=(-?\d+) bounds=\(([\d.-]+),([\d.-]+)\)-\(([\d.-]+),([\d.-]+)\)', current):
        index,action,argument,*bounds = m.groups()
        b={'index':int(index),'action':action,'argument':int(argument),'bounds':list(map(float,bounds))};buttons.append(b)
        x1,y1,x2,y2=b['bounds']
        assert x1>=-.1 and y1>=-.1 and x2<=w+.1 and y2<=h+.1,(label,b)
    if state=='offer': assert sorted(b['action'] for b in buttons)==['onboardlearn','onboardskip'],buttons
    if state.endswith('-details'): assert any(b['action']=='tutorialhelp' for b in buttons),buttons
    overlaps=[]
    for i,a in enumerate(buttons):
        for b in buttons[i+1:]:
            x1,y1,x2,y2=a['bounds'];u1,v1,u2,v2=b['bounds']
            if min(x2,u2)-max(x1,u1)>.2 and min(y2,v2)-max(y1,v1)>.2: overlaps.append([a,b])
    assert not overlaps,(label,overlaps)
    result={'case':label,'state':state,'viewport':[w,h],'scale':scale,'observed':observed,'buttons':buttons,
            'noButtonOverlap':True,'buttonsWithinViewport':True,'capture':label+'.png',
            'selection':{'id':int(selected[1]),'kind':selected[2]},'scope':'Actual Unreal renderer and deterministic ordinary-command tutorial pilot; no physical gesture test'}
    (out/(label+'-verification.json')).write_text(json.dumps(result,indent=2)+'\n')
    print(f'{label}: captured; state and all {len(buttons)} button bounds pass',flush=True)

protectedAfter=protectedHashes()
assert protectedBefore==protectedAfter, 'Preview changed a player save/preference'
(out/'protected-player-state.json').write_text(json.dumps({'unchanged':True,'before':protectedBefore,'after':protectedAfter},indent=2)+'\n')
