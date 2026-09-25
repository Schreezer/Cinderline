import importlib.util,json,os
from pathlib import Path
import unreal
root=Path("/Users/chirag13/Documents/ChatGPT/starCraft")
# Geometry is already owned and source hashes are current. Validate while refreshing materials.
report={}
for name,filename in [("rock","unreal_canyon_assets.py"),("ground","unreal_canyon_surface.py")]:
 spec=importlib.util.spec_from_file_location("frontier_"+name,root/"scripts"/filename)
 m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 if name=="rock":
  m.run();report[name]=json.loads((root/"RawAssets/Canyon/unreal-import-results.json").read_text())
 else:report[name]=m.build_canyon_surface()
 assert report[name]["success"]
report["finalization"]=unreal.CinderLandscapeAuthoringLibrary.finalize_canyon_assets()
assert report["finalization"].startswith("Finalized 3"),report["finalization"]
(root/"artifacts/terrain-overhaul/stage3-import.json").write_text(json.dumps(report,indent=2)+"\n")
unreal.log("CINDERLINE_TERRAIN_STAGE3_OK "+report["finalization"])
