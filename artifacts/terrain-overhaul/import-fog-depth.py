import importlib.util,json
from pathlib import Path
import unreal
root=Path("/Users/chirag13/Documents/ChatGPT/starCraft")
spec=importlib.util.spec_from_file_location("cinder_rock_fog_depth",root/"scripts/unreal_canyon_assets.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.run()
r=json.loads((root/"RawAssets/Canyon/unreal-import-results.json").read_text());assert r["success"]
(root/"artifacts/terrain-overhaul/fog-depth-import.json").write_text(json.dumps(r,indent=2)+"\n")
unreal.log("CINDERLINE_FOG_DEPTH_IMPORT_OK")
