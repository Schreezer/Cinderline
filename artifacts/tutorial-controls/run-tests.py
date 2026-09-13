import subprocess,json,re
from pathlib import Path
root=Path(__file__).resolve().parents[2]
report=root/'Saved/TutorialControls/automation'
report.mkdir(parents=True,exist_ok=True)
cmd=['/Users/chirag13/Documents/ChatGPT/CinderlineEngineIOS27/Engine/Binaries/Mac/UnrealEditor-Cmd',str(root/'Cinderline.uproject'),'/Engine/Maps/Entry','-unattended','-nop4','-nosplash','-NullRHI','-nosound','-stdout','-FullStdOutLogOutput','-ExecCmds=Automation RunTests Cinderline.Tutorial+Cinderline.Integration.FirstRunTutorialOffer+Cinderline.UI.Mobile; Quit','-ReportExportPath='+str(report),'-abslog='+str(report/'UnrealEditor.log'),'-AssetRegistryCacheRootFolder='+str(root/'Saved/TutorialControls/TestRegistry')]
with (report/'stdout.log').open('w') as out:
 r=subprocess.run(cmd,cwd=root,stdout=out,stderr=subprocess.STDOUT)
runtime=(report/'UnrealEditor.log').read_text(errors='replace')
assert not re.search(r'Ensure condition failed:|Assertion failed:|Fatal error:',runtime), 'Startup/runtime ensure or fatal error'
j=json.loads((report/'index.json').read_text(encoding='utf-8-sig'))
results=[{'name':t['fullTestPath'],'state':t['state'],'entries':t.get('entries',[])} for t in j['tests']]
(root/'artifacts/tutorial-controls/automation.json').write_text(json.dumps(results,indent=2)+'\n')
assert r.returncode==0, [t for t in results if t['state']!='Success']
assert len(results) == 8, results
assert all(x['state']=='Success' for x in results),results
print(json.dumps([{'name':t['name'],'state':t['state']} for t in results],indent=2))
