"""Import the complete authored visual target kit inside one Unreal editor process.

Usage: UnrealEditor-Cmd Cinderline.uproject /Engine/Maps/Entry
       -ExecutePythonScript=<absolute path to this file> -unattended -nosound -NullRHI
Full editor mode is required to initialize the static-mesh LOD subsystem.
Only the individual generators' owned VisualTarget assets are changed.
"""
from pathlib import Path
import json
import os
import sys
import traceback

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
REPORT = ROOT / "artifacts/visual-target/import-summary.json"


def run():
    import unreal
    import unreal_visual_target_materials as materials
    import unreal_visual_target_models as models
    import unreal_visual_target_model_finish as model_finish
    import unreal_visual_target_units as units
    import unreal_visual_target_scenery as scenery
    report = {"success": False, "stages": []}
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    try:
        materials.build_visual_target_materials()
        report["stages"].append("materials")
        model_finish.augment_visual_target_model_finish()
        report["stages"].append("model_surface_finish")
        models.import_visual_target_models(reimport=True)
        report["stages"].append("buildings_and_ore")
        os.environ["CINDER_REIMPORT_VISUAL_TARGET_UNITS"] = "1"
        units.run()
        report["stages"].append("units_and_motion")
        os.environ["CINDER_REIMPORT_SCENERY"] = "1"
        scenery.run()
        report["stages"].append("scenery")
        unreal.EditorAssetLibrary.save_directory("/Game/Art/VisualTarget", only_if_is_dirty=True, recursive=True)
        report["success"] = True
        unreal.log("CINDERLINE_VISUAL_TARGET_IMPORT_COMPLETE")
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        REPORT.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    run()
