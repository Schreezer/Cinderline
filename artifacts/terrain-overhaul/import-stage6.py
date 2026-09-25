import json
from pathlib import Path
import unreal
root=Path("/Users/chirag13/Documents/ChatGPT/starCraft")
report={}
report["authoring"]=unreal.CinderLandscapeAuthoringLibrary.author_frontier_landscapes()
assert report["authoring"].startswith("Authored three"),report["authoring"]
report["finalization"]=unreal.CinderLandscapeAuthoringLibrary.finalize_canyon_assets()
assert report["finalization"].startswith("Finalized 3"),report["finalization"]
report["success"]=True
(root/"artifacts/terrain-overhaul/stage6-landscapes.json").write_text(json.dumps(report,indent=2)+"\n")
unreal.log("CINDERLINE_TERRAIN_STAGE6_OK "+report["finalization"])
