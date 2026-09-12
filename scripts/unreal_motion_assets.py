"""Explicit idempotent import for Cinderline articulated static-mesh parts.

Run this file with UnrealEditor-Cmd -ExecutePythonScript. The importer writes
artifacts/motion-assets/unreal-import-results.json and creates the success marker
only after all 18 meshes satisfy the manifest contract.

Set CINDER_REIMPORT_MOTION=1 to replace an earlier import after FBX changes.
This script loads the existing model material instances. It never creates or
rebuilds materials, maps, actors, collision, or gameplay state.
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
SOURCE = ROOT / "RawAssets/Motion"
DESTINATION = "/Game/Art/Motion"
MATERIAL_DESTINATION = "/Game/Art/Materials"
REPORT = ROOT / "artifacts/motion-assets/unreal-import-results.json"
SUCCESS_MARKER = ROOT / "artifacts/motion-assets/unreal-import.success"
OWNER = "scripts/unreal_motion_assets.py"
SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")
EXPECTED_PARTS = {
    "Drudge": ("Body", "LegFL", "LegFR", "LegRL", "LegRR", "Tools"),
    "Ember": ("Body", "LegL", "LegR", "Weapon"),
    "Needle": ("Body", "LegL", "LegR", "Weapon"),
    "Anvil": ("Body", "Weapon"),
    "Cinderthrow": ("Body", "Weapon"),
}
EXPECTED_KINDS = {"Drudge": "Worker", "Ember": "Striker", "Needle": "Lancer",
                  "Anvil": "Bastion", "Cinderthrow": "Mortar"}


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline motion assets: " + message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(asset, key):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + key))


def tag(asset, key, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Cinderline." + key, str(value))


def save(asset):
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset), "could not save " + asset.get_path_name())


def load_manifest():
    path = SOURCE / "manifest.json"
    require(path.is_file(), "missing manifest " + str(path))
    manifest = json.loads(path.read_text(encoding="utf-8"))
    require(manifest.get("original_geometry") is True, "manifest must identify original geometry")
    require(manifest.get("canonical_builder_sha256") == sha256(ROOT / "scripts/create_blender_assets.py"),
            "motion pack is stale relative to the canonical model builder")
    require(manifest.get("unit") == "centimeter", "manifest unit must be centimeter")
    require(manifest.get("forward_axis") == "+X" and manifest.get("up_axis") == "+Z",
            "manifest axes must be +X forward and +Z up")
    require(manifest.get("handedness") == "Unreal left-handed coordinates" and
            manifest.get("fbx_coordinate_conversion") ==
            "post-import Unreal position = (source X, -source Y, source Z)",
            "manifest must record the legacy FBX source-Y conversion")
    require(manifest.get("root_origin", "").startswith("same (0,0,0) complete-model root"),
            "manifest must preserve the complete-model root")
    require(manifest.get("destination") == DESTINATION, "manifest destination changed")
    require(manifest.get("collision") == "none" and manifest.get("nanite") is False,
            "manifest must disable collision and Nanite")
    require(tuple(manifest.get("material_slot_order", ())) == SLOTS,
            "manifest material order changed")
    records = manifest.get("assets", [])
    expected_names = {"SM_{}_{}".format(unit, part)
                      for unit, parts in EXPECTED_PARTS.items() for part in parts}
    names = [record.get("name") for record in records]
    require(len(records) == 18 and len(names) == len(set(names)) and set(names) == expected_names,
            "manifest must contain the 18 expected parts exactly once")

    root = ROOT.resolve()
    by_unit = {unit: [] for unit in EXPECTED_PARTS}
    for record in records:
        name, unit, part = record["name"], record.get("unit"), record.get("part")
        require(unit in EXPECTED_PARTS, str(name) + " has an unexpected unit")
        require(name == "SM_{}_{}".format(unit, part), name + " has inconsistent unit or part")
        require(part in EXPECTED_PARTS[unit], name + " has an unexpected part")
        require(record.get("kind") == EXPECTED_KINDS[unit], name + " has an unexpected simulation kind")
        require(record.get("motion") in ("root", "rotate", "recoil"), name + " has invalid motion")
        for field in ("bounds_min_cm", "bounds_max_cm", "dimensions_cm", "joint_pivot_cm"):
            values = record.get(field, [])
            require(len(values) == 3 and all(isinstance(value, (int, float)) and math.isfinite(value)
                                              for value in values), name + " has invalid " + field)
        lo, hi, dimensions = record["bounds_min_cm"], record["bounds_max_cm"], record["dimensions_cm"]
        require(all(hi[index] > lo[index] and abs((hi[index]-lo[index])-dimensions[index]) < .002
                    for index in range(3)), name + " dimensions differ from bounds")
        if record["motion"] == "root":
            require(record["joint_pivot_cm"] == [0.0, 0.0, 0.0], name + " root pivot must be zero")
        slots = tuple(record.get("material_slots", ()))
        require(slots and slots == tuple(slot for slot in SLOTS if slot in slots),
                name + " slots must be a populated ordered subset of the model palette")
        require(isinstance(record.get("triangles"), int) and record["triangles"] > 0,
                name + " has invalid triangles")
        source = (ROOT / record.get("fbx", "")).resolve()
        require(os.path.commonpath((str(root), str(source))) == str(root) and
                source.parent == (SOURCE / "FBX").resolve() and source.suffix.lower() == ".fbx",
                name + " has an invalid FBX path")
        require(source.is_file(), "missing FBX for " + name)
        require(sha256(source) == record.get("sha256"), name + " source hash differs from manifest")
        record["_source"] = source
        by_unit[unit].append(record)

    units = manifest.get("units", {})
    require(set(units) == set(EXPECTED_PARTS), "manifest whole-unit table changed")
    for unit, unit_records in by_unit.items():
        whole = units[unit]
        require(sum(record["triangles"] for record in unit_records) == whole.get("triangles"),
                unit + " part triangles differ from the whole model")
        combined_lo = [min(record["bounds_min_cm"][axis] for record in unit_records) for axis in range(3)]
        combined_hi = [max(record["bounds_max_cm"][axis] for record in unit_records) for axis in range(3)]
        require(max(abs(combined_lo[axis]-whole["bounds_min_cm"][axis]) for axis in range(3)) < .002 and
                max(abs(combined_hi[axis]-whole["bounds_max_cm"][axis]) for axis in range(3)) < .002,
                unit + " combined part bounds differ from the whole model")
    return manifest, records


def load_materials():
    materials = {}
    for slot in SLOTS:
        path = MATERIAL_DESTINATION + "/MI_Cinder" + slot
        material = unreal.load_asset(path)
        require(isinstance(material, unreal.MaterialInterface),
                "missing existing material interface " + path + "; run the model asset import first")
        materials[slot] = material
    return materials


def import_options():
    options = unreal.FbxImportUI()
    settings = (
        ("automated_import_should_detect_type", False),
        ("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH),
        ("import_as_skeletal", False), ("import_mesh", True),
        ("import_animations", False), ("import_materials", False),
        ("import_textures", False), ("create_physics_asset", False),
        ("override_full_name", True),
    )
    for key, value in settings:
        options.set_editor_property(key, value)
    data = options.get_editor_property("static_mesh_import_data")
    require(data is not None, "FBX static mesh import data is unavailable")
    static_settings = (
        ("combine_meshes", True), ("convert_scene", True), ("convert_scene_unit", True),
        ("force_front_x_axis", False), ("import_translation", unreal.Vector(0, 0, 0)),
        ("import_rotation", unreal.Rotator(0, 0, 0)), ("import_uniform_scale", 1.0),
        ("transform_vertex_to_absolute", True), ("bake_pivot_in_vertex", False),
        ("reorder_material_to_fbx_order", True),
        ("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS),
        ("auto_generate_collision", False), ("generate_lightmap_u_vs", False),
        ("build_nanite", False), ("remove_degenerates", True),
    )
    for key, value in static_settings:
        data.set_editor_property(key, value)
    return options


def import_mesh(record, replace_existing):
    task = unreal.AssetImportTask()
    values = {
        "filename": str(record["_source"]), "destination_path": DESTINATION,
        "destination_name": record["name"], "automated": True, "async_": False,
        "save": False, "replace_existing": replace_existing,
        "replace_existing_settings": replace_existing, "factory": unreal.FbxFactory(),
        "options": import_options(),
    }
    for key, value in values.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    objects = list(task.get_objects())
    path = DESTINATION + "/" + record["name"] + "." + record["name"]
    meshes = [obj for obj in objects if isinstance(obj, unreal.StaticMesh) and obj.get_path_name() == path]
    require(len(meshes) == 1, record["name"] + " did not import as one StaticMesh at " + path)
    return meshes[0]


def validate_mesh(mesh, record):
    name = record["name"]
    require(isinstance(mesh, unreal.StaticMesh), name + " is not a StaticMesh")
    slots = tuple(str(slot.get_editor_property("material_slot_name"))
                  for slot in mesh.get_editor_property("static_materials"))
    require(slots == tuple(record["material_slots"]),
            name + " material slots are " + repr(slots) + "; expected " + repr(record["material_slots"]))
    bounds = mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    lo = [origin.x-extent.x, origin.y-extent.y, origin.z-extent.z]
    hi = [origin.x+extent.x, origin.y+extent.y, origin.z+extent.z]
    tolerance = max(.10, max(record["dimensions_cm"]) * .005)
    expected_lo, expected_hi = record["bounds_min_cm"], record["bounds_max_cm"]
    minimum_delta = [lo[index]-expected_lo[index] for index in range(3)]
    maximum_delta = [hi[index]-expected_hi[index] for index in range(3)]
    bounds_match = (all(math.isfinite(value) and abs(minimum_delta[index]) <= tolerance
                        for index, value in enumerate(lo)) and
                    all(math.isfinite(value) and abs(maximum_delta[index]) <= tolerance
                        for index, value in enumerate(hi)))
    require(bounds_match, name + " bounds differ from the complete-model coordinate contract: " +
            json.dumps({"expected_min_cm": expected_lo, "actual_min_cm": lo,
                        "minimum_delta_cm": minimum_delta, "expected_max_cm": expected_hi,
                        "actual_max_cm": hi, "maximum_delta_cm": maximum_delta,
                        "tolerance_cm": tolerance}, sort_keys=True))
    triangles = mesh.get_num_triangles(0)
    sections = mesh.get_num_sections(0)
    require(triangles == record["triangles"],
            name + " imported {} triangles; manifest has {}".format(triangles, record["triangles"]))
    require(sections == len(slots), name + " must have one populated section per declared slot")
    require(not mesh.get_editor_property("nanite_settings").get_editor_property("enabled"),
            name + " unexpectedly enabled Nanite")
    mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    require(mesh_subsystem is not None, "StaticMeshEditorSubsystem is unavailable")
    simple_collisions = mesh_subsystem.get_simple_collision_count(mesh)
    require(simple_collisions == 0, name + " unexpectedly has simple collision")
    return {"name": name, "asset": mesh.get_path_name(), "unit": record["unit"],
            "part": record["part"], "bounds_min_cm": lo, "bounds_max_cm": hi,
            "joint_pivot_cm": record["joint_pivot_cm"], "motion": record["motion"],
            "axis": record["axis"], "recoil_direction": record.get("recoil_direction"),
            "triangles": triangles, "sections": sections, "material_slots": list(slots),
            "simple_collisions": simple_collisions, "nanite": False,
            "source_sha256": record["sha256"]}


def import_motion_assets():
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    SUCCESS_MARKER.unlink(missing_ok=True)
    report = {"success": False, "utc": datetime.now(timezone.utc).isoformat(),
              "owner": OWNER, "destination": DESTINATION, "assets": [],
              "generator_sha256": sha256(__file__), "manifest_sha256": None}
    try:
        manifest, records = load_manifest()
        report["manifest_sha256"] = sha256(SOURCE / "manifest.json")
        materials = load_materials()
        unreal.EditorAssetLibrary.make_directory(DESTINATION)
        reimport = os.environ.get("CINDER_REIMPORT_MOTION") == "1"
        meshes = {}
        for record in records:
            asset_path = DESTINATION + "/" + record["name"]
            mesh = unreal.load_asset(asset_path)
            if mesh is not None:
                require(isinstance(mesh, unreal.StaticMesh), "unrelated asset occupies " + asset_path)
                require(metadata(mesh, "MotionOwner") == OWNER,
                        "refusing to replace an asset owned by " + metadata(mesh, "MotionOwner"))
                require(reimport or metadata(mesh, "SourceSHA256") == record["sha256"],
                        record["name"] + " source changed; set CINDER_REIMPORT_MOTION=1")
            imported = mesh is None or reimport
            if imported:
                mesh = import_mesh(record, mesh is not None)
            validation = validate_mesh(mesh, record)
            for index, slot in enumerate(record["material_slots"]):
                mesh.set_material(index, materials[slot])
                assigned = mesh.get_material(index)
                require(assigned is not None and assigned.get_path_name() == materials[slot].get_path_name(),
                        record["name"] + " material mapping failed for slot " + slot)
            validation["material_assets"] = [materials[slot].get_path_name()
                                              for slot in record["material_slots"]]
            tag(mesh, "MotionOwner", OWNER)
            tag(mesh, "SourceSHA256", record["sha256"])
            tag(mesh, "MotionPart", record["unit"] + "." + record["part"])
            tag(mesh, "JointPivotCm", json.dumps(record["joint_pivot_cm"], separators=(",", ":")))
            tag(mesh, "MotionContract", "parts-v1,cm,+X-forward,+Z-up,shared-root,no-collision,no-nanite")
            save(mesh)
            validation["imported"] = imported
            report["assets"].append(validation)
            meshes[record["name"]] = mesh
            unreal.log("CINDERLINE_MOTION_VALIDATED " + json.dumps(validation, sort_keys=True))

        for unit, parts in EXPECTED_PARTS.items():
            rows = [row for row in report["assets"] if row["unit"] == unit]
            whole = manifest["units"][unit]
            combined_lo = [min(row["bounds_min_cm"][axis] for row in rows) for axis in range(3)]
            combined_hi = [max(row["bounds_max_cm"][axis] for row in rows) for axis in range(3)]
            require(sum(row["triangles"] for row in rows) == whole["triangles"] and
                    max(abs(combined_lo[axis]-whole["bounds_min_cm"][axis]) for axis in range(3)) < .25 and
                    max(abs(combined_hi[axis]-whole["bounds_max_cm"][axis]) for axis in range(3)) < .25,
                    unit + " imported parts do not reconstruct the whole-model bounds and triangles")
        report["success"] = True
        report["count"] = len(report["assets"])
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        REPORT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    SUCCESS_MARKER.write_text(json.dumps({"success": True, "count": report["count"],
                                          "manifest_sha256": report["manifest_sha256"]},
                                         sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CINDERLINE_MOTION_ASSETS_OK: 18 articulated static meshes validated; " + str(REPORT))
    return report


if __name__ == "__main__":
    import_motion_assets()
