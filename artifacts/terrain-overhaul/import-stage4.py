import importlib.util,json
from pathlib import Path
import unreal
root=Path("/Users/chirag13/Documents/ChatGPT/starCraft")
spec=importlib.util.spec_from_file_location("frontier_grass",root/"scripts/unreal_frontier_foliage.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.run()
report=json.loads((root/"RawAssets/FrontierFoliage/unreal-import-results.json").read_text())
assert report["success"]
(root/"artifacts/terrain-overhaul/stage4-import.json").write_text(json.dumps(report,indent=2)+"\n")
unreal.log("CINDERLINE_TERRAIN_STAGE4_OK")
