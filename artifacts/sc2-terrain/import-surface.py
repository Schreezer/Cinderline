import importlib.util
import json
from pathlib import Path
import unreal
root = Path("/Users/chirag13/Documents/ChatGPT/starCraft")
spec = importlib.util.spec_from_file_location("sc2_terrain_surface", root / "scripts/unreal_canyon_surface.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
report = module.build_canyon_surface()
authored = unreal.CinderLandscapeAuthoringLibrary.author_frontier_landscapes()
assert authored and authored.startswith("Authored three"), authored
report["authoring"] = authored
result = unreal.CinderLandscapeAuthoringLibrary.finalize_canyon_assets()
assert result and result.startswith("Finalized 3"), result
report["shader_validation"] = result
(root / "artifacts/sc2-terrain/material-import.json").write_text(json.dumps(report, indent=2) + "\n")
unreal.log("CINDERLINE_SC2_TERRAIN_IMPORT_OK " + result)
