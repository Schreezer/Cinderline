"""Patch only battlefield-size mask UVs in four owned material assets.

Run explicitly in UnrealEditor-Cmd. The script preserves each material graph and
replaces the one 1/4800 map-mask multiplier input with the runtime scalar
parameter CinderWorldSizeInverse. Spatial detail tiling remains unchanged.
"""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import math
import traceback

import unreal


ROOT = Path(__file__).resolve().parent.parent
REPORT = ROOT / "artifacts/match-length/material-patch.json"
PARAMETER = "CinderWorldSizeInverse"
STANDARD_INVERSE = 1.0 / 4800.0
LIB = unreal.MaterialEditingLibrary
TARGETS = (
    ("/Game/Art/Materials/M_CinderFogV2", "scripts/unreal_visual_upgrade.py",
     "World centimeters to texture UV", ROOT / "Content/Art/Materials/M_CinderFogV2.uasset"),
    ("/Game/Art/Canyon/Materials/M_CinderCanyonGround", "scripts/unreal_canyon_surface.py",
     "Map World centimeters to texture UV", ROOT / "Content/Art/Canyon/Materials/M_CinderCanyonGround.uasset"),
    ("/Game/Art/Materials/M_CinderGroundV3", "scripts/unreal_terrain_surface.py",
     "Layers World centimeters to texture UV", ROOT / "Content/Art/Materials/M_CinderGroundV3.uasset"),
    ("/Game/Art/VisualTarget/Materials/M_CinderGroundV4", "scripts/unreal_visual_target_materials.py",
     "Layers World centimeters to texture UV", ROOT / "Content/Art/VisualTarget/Materials/M_CinderGroundV4.uasset"),
)


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline match-length material patch: " + message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(asset, name):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + name))


def patch_material(path, expected_owner, multiply_description, package_file):
    require(package_file.is_file(), "missing package " + str(package_file))
    before = sha256(package_file)
    material = unreal.load_asset(path)
    require(isinstance(material, unreal.Material), "missing material " + path)
    require(metadata(material, "VisualOwner") == expected_owner,
            "refusing non-owned material " + path + " owner=" + metadata(material, "VisualOwner"))

    expressions = list(LIB.get_material_expressions(material))
    multipliers = [node for node in expressions
                   if isinstance(node, unreal.MaterialExpressionMultiply)
                   and str(node.get_editor_property("desc")) == multiply_description]
    require(len(multipliers) == 1,
            path + " expected one map-mask multiplier, found " + str(len(multipliers)))
    multiply = multipliers[0]
    existing = [node for node in expressions
                if isinstance(node, unreal.MaterialExpressionScalarParameter)
                and str(node.get_editor_property("parameter_name")) == PARAMETER]
    require(len(existing) <= 1, path + " has duplicate " + PARAMETER + " parameters")
    if existing:
        scalar = existing[0]
    else:
        scalar = LIB.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, -780, -520)
        require(scalar is not None, "could not create scalar in " + path)
        scalar.set_editor_property("desc", "Active battlefield size inverse")
        scalar.set_editor_property("parameter_name", PARAMETER)
    scalar.set_editor_property("default_value", STANDARD_INVERSE)
    names = [str(name) for name in LIB.get_material_expression_input_names(multiply)]
    inputs = list(LIB.get_inputs_for_material_expression(material, multiply))
    already_connected = ("B" in names and names.index("B") < len(inputs)
                         and inputs[names.index("B")] == scalar)
    if not already_connected:
        require(LIB.connect_material_expressions(scalar, "", multiply, "B"),
                "could not connect " + PARAMETER + " to " + path)
        inputs = list(LIB.get_inputs_for_material_expression(material, multiply))
    require("B" in names and names.index("B") < len(inputs)
            and inputs[names.index("B")] == scalar,
            path + " multiplier B connection did not read back")
    parameters = list(LIB.get_scalar_parameter_names(material))
    require(PARAMETER in {str(name) for name in parameters},
            path + " does not expose " + PARAMETER)
    require(math.isclose(float(scalar.get_editor_property("default_value")),
                         STANDARD_INVERSE, rel_tol=0.0, abs_tol=1e-10),
            path + " retained the wrong Standard default")
    errors = list(LIB.recompile_material(material))
    require(not errors, path + " shader errors: " + repr(errors))
    unreal.EditorAssetLibrary.set_metadata_tag(
        material, "Cinderline.WorldSizeUV", PARAMETER)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material), "could not save " + path)
    require(package_file.is_file(), "package disappeared after save " + path)
    return {
        "path": path, "owner": expected_owner, "package": str(package_file.relative_to(ROOT)),
        "multiplier": multiply_description, "parameter": PARAMETER,
        "default": STANDARD_INVERSE, "beforeSha256": before,
        "afterSha256": sha256(package_file), "shaderErrors": errors,
    }


def patch_match_length_materials():
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    result = {
        "success": False, "utc": datetime.now(timezone.utc).isoformat(),
        "sourceSha256": sha256(__file__), "materials": [],
    }
    try:
        for target in TARGETS:
            result["materials"].append(patch_material(*target))
        require(len(result["materials"]) == len(TARGETS), "incomplete target set")
        result["success"] = True
    except Exception as error:
        result["error"] = str(error)
        result["traceback"] = traceback.format_exc()
        raise
    finally:
        REPORT.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    unreal.log("CINDERLINE_MATCH_LENGTH_MATERIALS_OK " + str(REPORT))
    return result


if __name__ == "__main__":
    patch_match_length_materials()
