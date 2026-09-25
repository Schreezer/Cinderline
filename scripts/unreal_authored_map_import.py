"""Build authored-map materials and rebake the three canonical Landscapes.

Run after the current C++ editor build, in one render-capable editor process.
"""
import importlib.util
import json
from pathlib import Path
import traceback
import unreal

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run():
    output = ROOT / "artifacts/map-redesign/import.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    report = {"success": False}
    try:
        load("unreal_canyon_surface").build_canyon_surface()
        load("unreal_map_architecture").build_map_architecture()
        report["authoring"] = unreal.CinderLandscapeAuthoringLibrary.author_frontier_landscapes()
        assert report["authoring"].startswith("Authored three"), report["authoring"]
        report["finalization"] = unreal.CinderLandscapeAuthoringLibrary.finalize_canyon_assets()
        assert report["finalization"].startswith("Finalized 3"), report["finalization"]
        report["success"] = True
        unreal.log("CINDERLINE_AUTHORED_MAP_IMPORT_OK")
    except Exception as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    run()
