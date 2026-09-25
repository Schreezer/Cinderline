from pathlib import Path
import json,subprocess,os,sys
P=Path(__file__).resolve().parents[1]
env=os.environ.copy();env['GFLOW_CHROME_PATH']='/Applications/Helium.app/Contents/MacOS/Helium'
only=set(sys.argv[1:])
for job in json.loads((P/'public/flow/prompts.json').read_text()):
    if only and job['id'] not in only: continue
    out=P/'public/flow'/job['id'];out.mkdir(exist_ok=True)
    if any(out.glob('*.jpeg')) or any(out.glob('*.jpg')) or any(out.glob('*.png')) or any(out.glob('*.zip')):
        print('SKIP existing',job['id'],flush=True);continue
    (out/'prompt.txt').write_text(job['prompt'])
    print('GENERATING',job['id'],flush=True)
    r=subprocess.run(['node','/Users/chirag13/src/gflow-cli/dist/src/index.js','image','--id',job['id'],'--prompt',job['prompt'],'--model','Nano Banana 2','--ratio','9:16','--outputs','1','--timeout','300','--out',str(out)],cwd='/Users/chirag13/Documents/ChatGPT/starCraft',env=env,capture_output=True,text=True)
    (out/'cli.log').write_text(r.stdout+'\n'+r.stderr)
    print(r.stdout,flush=True)
    if r.returncode: print(r.stderr,flush=True); raise SystemExit(r.returncode)
print('DONE',flush=True)
