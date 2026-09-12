"""Import and validate Cinderline's richer unit models and articulated parts.

Run explicitly with UnrealEditor-Cmd -ExecutePythonScript after the visual-target
materials exist. Set CINDER_REIMPORT_VISUAL_TARGET_UNITS=1 to replace assets
previously created by this script. No maps, actors, gameplay state, or source
assets outside the two visual-target unit destinations are changed.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "RawAssets/VisualTarget/Units"
MODEL_DESTINATION = "/Game/Art/VisualTarget/Models"
MOTION_DESTINATION = "/Game/Art/VisualTarget/Motion"
MATERIAL_DESTINATION = "/Game/Art/VisualTarget/Materials"
REPORT = ROOT / "artifacts/visual-target/units/unreal-import-results.json"
SUCCESS = ROOT / "artifacts/visual-target/units/unreal-import.success"
OWNER = "scripts/unreal_visual_target_units.py"
SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")
MATERIALS = {slot: MATERIAL_DESTINATION + "/MI_VT_" + slot for slot in SLOTS}
LOD_TRIANGLE_PERCENTAGES = (1.0, .65, .32)
LOD_SCREEN_SIZES = (1.0, .055, .018)
EXPECTED_DIMENSIONS = {
    "Drudge": (32.0, 19.907, 25.0), "Ember": (40.0, 30.854, 48.0),
    "Needle": (42.0, 23.807, 47.0), "Skim": (36.0, 17.294, 14.0),
    "Anvil": (68.0, 59.691, 38.0), "Cinderthrow": (60.0, 41.846, 38.0),
    "Mend": (38.0, 34.244, 39.0), "Veil": (49.695, 52.0, 18.0),
}
EXPECTED_PARTS = {
    "Drudge": ("Body", "LegFL", "LegFR", "LegRL", "LegRR", "Tools"),
    "Ember": ("Body", "LegL", "LegR", "Weapon"),
    "Needle": ("Body", "LegL", "LegR", "Weapon"),
    "Anvil": ("Body", "Weapon"), "Cinderthrow": ("Body", "Weapon"),
}


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline visual-target units: " + message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(asset, key):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + key))


def tag(asset, key, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Cinderline." + key, str(value))


def load_manifest():
    path = SOURCE / "manifest.json"
    require(path.is_file(), "missing manifest " + str(path))
    manifest = json.loads(path.read_text(encoding="utf-8"))
    require(manifest.get("original_geometry") is True, "manifest must identify original geometry")
    require(manifest.get("unit") == "centimeter", "manifest unit changed")
    require(manifest.get("forward_axis") == "+X" and manifest.get("up_axis") == "+Z",
            "manifest axes changed")
    require(manifest.get("fbx_coordinate_conversion") ==
            "post-import Unreal position = (source X, -source Y, source Z)",
            "legacy FBX coordinate conversion changed")
    require(manifest.get("root_origin", "").startswith("same bottom-centered (0,0,0) complete-model root"),
            "full models and motion parts must retain the shared root")
    require(manifest.get("model_destination") == MODEL_DESTINATION and
            manifest.get("motion_destination") == MOTION_DESTINATION, "destination contract changed")
    require(manifest.get("collision") == "none" and manifest.get("nanite") is False,
            "presentation assets must disable collision and Nanite")
    require(tuple(manifest.get("material_slot_order", ())) == SLOTS, "material order changed")

    records = manifest.get("assets", [])
    model_names = {"SM_" + unit for unit in EXPECTED_DIMENSIONS}
    part_names = {"SM_{}_{}".format(unit, part)
                  for unit, parts in EXPECTED_PARTS.items() for part in parts}
    names = [record.get("name") for record in records]
    require(len(records) == 26 and len(names) == len(set(names)) and set(names) == model_names | part_names,
            "manifest must contain eight models and eighteen motion parts exactly once")

    pivot_source = json.loads((ROOT / "RawAssets/Motion/manifest.json").read_text(encoding="utf-8"))
    pivot_contract = {record["name"]: record["joint_pivot_cm"] for record in pivot_source["assets"]}
    root = ROOT.resolve(); by_unit = {unit: [] for unit in EXPECTED_PARTS}; models = {}
    for record in records:
        name, unit, asset_type = record["name"], record.get("unit"), record.get("asset_type")
        require(unit in EXPECTED_DIMENSIONS, name + " has an unexpected unit")
        require(asset_type in ("model", "motion"), name + " has an unexpected asset type")
        expected_source = SOURCE / ("Models/FBX" if asset_type == "model" else "Motion/FBX")
        source = (ROOT / record.get("fbx", "")).resolve()
        require(os.path.commonpath((str(root), str(source))) == str(root) and source.parent == expected_source.resolve(),
                name + " source path escaped its pack")
        require(source.is_file() and sha256(source) == record.get("sha256"), name + " source hash differs")
        record["_source"] = source
        for field in ("bounds_min_cm", "bounds_max_cm", "dimensions_cm"):
            values = record.get(field, [])
            require(len(values) == 3 and all(isinstance(value, (int, float)) and math.isfinite(value)
                                              for value in values), name + " has invalid " + field)
        lo, hi, size = record["bounds_min_cm"], record["bounds_max_cm"], record["dimensions_cm"]
        require(all(hi[i] > lo[i] and abs((hi[i]-lo[i])-size[i]) < .002 for i in range(3)),
                name + " dimensions differ from bounds")
        slots = tuple(record.get("material_slots", ()))
        require(slots and slots == tuple(slot for slot in SLOTS if slot in slots),
                name + " slots are not an ordered palette subset")
        require(isinstance(record.get("triangles"), int) and record["triangles"] > 0,
                name + " has invalid triangles")
        minimum_area = record.get("minimum_triangle_area_cm2")
        require(isinstance(minimum_area, (int, float)) and math.isfinite(minimum_area) and minimum_area >= .00005 and
                record.get("unreal_degenerate_triangles") == 0,
                name + " contains geometry below Unreal's triangle-area threshold")
        if asset_type == "model":
            require(name == "SM_" + unit and slots == SLOTS, name + " must contain all five material slots")
            require(3000 <= record["triangles"] <= 8000, name + " exceeds the unit triangle budget")
            require(max(abs(size[i]-EXPECTED_DIMENSIONS[unit][i]) for i in range(3)) < .002,
                    name + " dimensions differ from the canonical model")
            require(max(abs(lo[i]-(-size[i]*.5 if i < 2 else 0.0)) for i in range(3)) < .002,
                    name + " root is not bottom-center")
            models[unit] = record
        else:
            part = record.get("part")
            require(unit in EXPECTED_PARTS and part in EXPECTED_PARTS[unit] and
                    name == "SM_{}_{}".format(unit, part), name + " has inconsistent part identity")
            require(record.get("joint_pivot_cm") == pivot_contract.get(name),
                    name + " changed the validated CinderMotionAssetParts pivot")
            by_unit[unit].append(record)

    require(set(models) == set(EXPECTED_DIMENSIONS), "full model table is incomplete")
    for unit, parts in by_unit.items():
        require({row["part"] for row in parts} == set(EXPECTED_PARTS[unit]) and
                len(parts) == len(EXPECTED_PARTS[unit]), unit + " motion part set is incomplete")
        model = models[unit]
        combined_lo = [min(row["bounds_min_cm"][axis] for row in parts) for axis in range(3)]
        combined_hi = [max(row["bounds_max_cm"][axis] for row in parts) for axis in range(3)]
        require(sum(row["triangles"] for row in parts) == model["triangles"] and
                max(abs(combined_lo[i]-model["bounds_min_cm"][i]) for i in range(3)) < .002 and
                max(abs(combined_hi[i]-model["bounds_max_cm"][i]) for i in range(3)) < .002,
                unit + " parts do not reconstruct the full model")
    return manifest, records


def import_options():
    options = unreal.FbxImportUI()
    for key, value in (("automated_import_should_detect_type", False),
                       ("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH),
                       ("import_as_skeletal", False), ("import_mesh", True),
                       ("import_animations", False), ("import_materials", False),
                       ("import_textures", False), ("create_physics_asset", False),
                       ("override_full_name", True)):
        options.set_editor_property(key, value)
    data = options.get_editor_property("static_mesh_import_data")
    require(data is not None, "FBX static mesh import data is unavailable")
    for key, value in (("combine_meshes", True), ("convert_scene", True), ("convert_scene_unit", True),
                       ("force_front_x_axis", False), ("import_translation", unreal.Vector(0, 0, 0)),
                       ("import_rotation", unreal.Rotator(0, 0, 0)), ("import_uniform_scale", 1.0),
                       ("transform_vertex_to_absolute", True), ("bake_pivot_in_vertex", False),
                       ("reorder_material_to_fbx_order", True),
                       ("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS),
                       ("auto_generate_collision", False), ("generate_lightmap_u_vs", False),
                       ("build_nanite", False), ("remove_degenerates", True)):
        data.set_editor_property(key, value)
    vertex_color_option = getattr(unreal, "VertexColorImportOption", None)
    require(vertex_color_option is not None, "editor does not expose VertexColorImportOption")
    data.set_editor_property("vertex_color_import_option", vertex_color_option.REPLACE)
    return options


def import_mesh(record, replace):
    destination = MODEL_DESTINATION if record["asset_type"] == "model" else MOTION_DESTINATION
    task = unreal.AssetImportTask()
    for key, value in {"filename": str(record["_source"]), "destination_path": destination,
                       "destination_name": record["name"], "automated": True, "async_": False,
                       "save": False, "replace_existing": replace, "replace_existing_settings": replace,
                       "factory": unreal.FbxFactory(), "options": import_options()}.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = destination + "/" + record["name"] + "." + record["name"]
    meshes = [asset for asset in task.get_objects()
              if isinstance(asset, unreal.StaticMesh) and asset.get_path_name() == path]
    require(len(meshes) == 1, record["name"] + " did not import as one StaticMesh")
    return meshes[0]


def set_lods(mesh, percentages):
    settings_type = getattr(unreal, "StaticMeshReductionSettings", None)
    options_type = getattr(unreal, "StaticMeshReductionOptions", None)
    library = getattr(unreal, "EditorStaticMeshLibrary", None)
    if settings_type is None or options_type is None or library is None or not hasattr(library, "set_lods"):
        unreal.log_warning(mesh.get_name() + ": editor reduction API unavailable; LOD0 remains valid")
        return False
    settings = []
    for percentage, screen_size in zip(percentages, LOD_SCREEN_SIZES):
        row = settings_type(); row.set_editor_property("percent_triangles", float(percentage))
        row.set_editor_property("screen_size", float(screen_size)); settings.append(row)
    options = options_type(); options.set_editor_property("auto_compute_lod_screen_size", False)
    options.set_editor_property("reduction_settings", settings)
    require(int(library.set_lods(mesh, options)) >= 3, mesh.get_name() + " did not create three LODs")
    return True


def validate_mesh(mesh, record, subsystem):
    name = record["name"]
    slots = tuple(str(row.material_slot_name) for row in mesh.get_editor_property("static_materials"))
    require(slots == tuple(record["material_slots"]), name + " material slots changed")
    bounds = mesh.get_bounds(); origin, extent = bounds.origin, bounds.box_extent
    lo = [origin.x-extent.x, origin.y-extent.y, origin.z-extent.z]
    hi = [origin.x+extent.x, origin.y+extent.y, origin.z+extent.z]
    tolerance = max(.10, max(record["dimensions_cm"])*.005)
    require(all(abs(lo[i]-record["bounds_min_cm"][i]) <= tolerance and
                abs(hi[i]-record["bounds_max_cm"][i]) <= tolerance for i in range(3)),
            name + " changed complete-model coordinates")
    imported_triangles = mesh.get_num_triangles(0)
    require(imported_triangles == record["triangles"],
            name + " imported {} triangles; manifest expects {}".format(imported_triangles, record["triangles"]))
    require(mesh.get_num_sections(0) == len(slots), name + " has an empty or merged material section")
    require(subsystem.get_simple_collision_count(mesh) == 0, name + " unexpectedly has collision")
    require(not mesh.get_editor_property("nanite_settings").get_editor_property("enabled"),
            name + " unexpectedly enabled Nanite")
    return {"name": name, "asset": mesh.get_path_name(), "unit": record["unit"],
            "asset_type": record["asset_type"], "part": record.get("part"),
            "bounds_min_cm": lo, "bounds_max_cm": hi, "triangles_lod0": record["triangles"],
            "material_slots": list(slots), "vertex_color_import": "replace from FBX Color alpha AO",
            "joint_pivot_cm": record.get("joint_pivot_cm"), "simple_collisions": 0, "nanite": False}


def run():
    REPORT.parent.mkdir(parents=True, exist_ok=True); SUCCESS.unlink(missing_ok=True)
    report = {"success": False, "utc": datetime.now(timezone.utc).isoformat(), "owner": OWNER,
              "destinations": [MODEL_DESTINATION, MOTION_DESTINATION], "assets": []}
    try:
        _manifest, records = load_manifest()
        materials = {}
        for slot, path in MATERIALS.items():
            material = unreal.load_asset(path)
            require(isinstance(material, unreal.MaterialInterface), "missing visual-target material " + path)
            materials[slot] = material
        unreal.EditorAssetLibrary.make_directory(MODEL_DESTINATION)
        unreal.EditorAssetLibrary.make_directory(MOTION_DESTINATION)
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        require(subsystem is not None, "StaticMeshEditorSubsystem is unavailable")
        replace = os.environ.get("CINDER_REIMPORT_VISUAL_TARGET_UNITS") == "1"
        for record in records:
            destination = MODEL_DESTINATION if record["asset_type"] == "model" else MOTION_DESTINATION
            asset_path = destination + "/" + record["name"]
            mesh = unreal.load_asset(asset_path)
            if mesh is not None:
                require(isinstance(mesh, unreal.StaticMesh), "unrelated asset occupies " + asset_path)
                require(metadata(mesh, "VisualTargetUnitOwner") == OWNER,
                        "refusing to replace asset owned by " + metadata(mesh, "VisualTargetUnitOwner"))
                require(replace or metadata(mesh, "SourceSHA256") == record["sha256"],
                        record["name"] + " changed; set CINDER_REIMPORT_VISUAL_TARGET_UNITS=1")
            imported = mesh is None or replace
            if imported: mesh = import_mesh(record, mesh is not None)
            validation = validate_mesh(mesh, record, subsystem)
            for index, slot in enumerate(record["material_slots"]):
                mesh.set_material(index, materials[slot])
                require(mesh.get_material(index).get_path_name() == materials[slot].get_path_name(),
                        record["name"] + " material mapping failed for " + slot)
            validation["lods_created"] = set_lods(mesh, LOD_TRIANGLE_PERCENTAGES)
            validation["lod_count"] = mesh.get_num_lods(); validation["imported"] = imported
            tag(mesh, "VisualTargetUnitOwner", OWNER); tag(mesh, "SourceSHA256", record["sha256"])
            tag(mesh, "VisualTargetUnit", record["unit"]); tag(mesh, "VertexAO", "FBX Color alpha")
            if record["asset_type"] == "motion":
                tag(mesh, "MotionPart", record["unit"] + "." + record["part"])
                tag(mesh, "JointPivotCm", json.dumps(record["joint_pivot_cm"], separators=(",", ":")))
            require(unreal.EditorAssetLibrary.save_loaded_asset(mesh), "could not save " + asset_path)
            report["assets"].append(validation)
            unreal.log("CINDERLINE_VISUAL_TARGET_UNIT_VALIDATED " + json.dumps(validation, sort_keys=True))
        report["success"] = True; report["count"] = len(report["assets"])
    except Exception as error:
        report["error"] = str(error); report["traceback"] = traceback.format_exc(); raise
    finally:
        REPORT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    SUCCESS.write_text(json.dumps({"success": True, "count": report["count"]}, sort_keys=True) + "\n")
    unreal.log("CINDERLINE_VISUAL_TARGET_UNITS_IMPORT_OK: 8 models + 18 motion parts; " + str(REPORT))
    return report


if __name__ == "__main__":
    run()
