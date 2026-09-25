import importlib.util,json,os
from pathlib import Path
import unreal
root=Path("/Users/chirag13/Documents/ChatGPT/starCraft")
os.environ["CINDER_REIMPORT_CANYON"]="1"
spec=importlib.util.spec_from_file_location("frontier_rocks",root/"scripts/unreal_canyon_assets.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.run()
report=json.loads((root/"RawAssets/Canyon/unreal-import-results.json").read_text())
assert report["success"]
report["authoring"]=unreal.CinderLandscapeAuthoringLibrary.author_frontier_landscapes()
assert report["authoring"].startswith("Authored three"),report["authoring"]
report["finalization"]=unreal.CinderLandscapeAuthoringLibrary.finalize_canyon_assets()
assert report["finalization"].startswith("Finalized 3"),report["finalization"]
(root/"artifacts/terrain-overhaul/stage2-import.json").write_text(json.dumps(report,indent=2)+"\n")
unreal.log("CINDERLINE_TERRAIN_STAGE2_OK "+report["finalization"])
