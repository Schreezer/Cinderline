#!/usr/bin/env python3
"""Build articulated Cinderline static-mesh parts from the canonical builders.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
    --threads 2 --python scripts/create_motion_assets.py

The script loads only the allow-listed geometry function definitions from
create_blender_assets.py. It never executes that file's export or render body.
Every part keeps the complete model's normalized centimeter coordinate frame.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import math
import sys
from collections import Counter
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "scripts/create_blender_assets.py"
OUT = ROOT / "RawAssets/Motion"
EVIDENCE = ROOT / "artifacts/motion-assets"
SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")

# Object ranges refer to the append order in the canonical builder's PARTS list.
# Each authored object appears in exactly one range.
SPECS = {
    "Drudge": {
        "kind": "Worker", "radius_cm": 16, "height_cm": 25, "builder": "drudge",
        "parts": {
            # Unreal's legacy FBX position conversion negates source Y. Assign
            # semantic left/right names in the final Unreal coordinate frame.
            "LegRR": range(0, 3), "LegFR": range(3, 6),
            "LegRL": range(6, 9), "LegFL": range(9, 12),
            "Tools": range(20, 25),
            "Body": (*range(12, 20), *range(25, 28)),
        },
        "pivots": {
            "LegRR": (-.32*.6*.82, -.38*.63*.82, .56),
            "LegFR": (.32*.6*.82, -.38*.63*.82, .56),
            "LegRL": (-.32*.6*.82, .38*.63*.82, .56),
            "LegFL": (.32*.6*.82, .38*.63*.82, .56),
            "Tools": (.36, 0, .66), "Body": (0, 0, 0),
        },
        "motion": {"LegRL": ("rotate", "+Y"), "LegFL": ("rotate", "+Y"),
                   "LegRR": ("rotate", "+Y"), "LegFR": ("rotate", "+Y"),
                   "Tools": ("rotate", "+Y"), "Body": ("root", "+Z")},
    },
    "Ember": {
        "kind": "Striker", "radius_cm": 20, "height_cm": 48, "builder": "ember",
        "parts": {"LegR": range(0, 3), "LegL": range(3, 6),
                  "Weapon": range(17, 20),
                  "Body": (*range(6, 17), *range(20, 23))},
        "pivots": {"LegR": (0, -.38*.63, .56), "LegL": (0, .38*.63, .56),
                   "Weapon": (.22, -.28, .91), "Body": (0, 0, 0)},
        "motion": {"LegL": ("rotate", "+Y"), "LegR": ("rotate", "+Y"),
                   "Weapon": ("recoil", "-X"), "Body": ("root", "+Z")},
    },
    "Needle": {
        "kind": "Lancer", "radius_cm": 21, "height_cm": 47, "builder": "needle",
        "parts": {"LegR": range(0, 3), "LegL": range(3, 6),
                  "Weapon": (*range(9, 13), *range(15, 19)),
                  "Body": (*range(6, 9), *range(13, 15), *range(19, 27))},
        "pivots": {"LegR": (0, -.38*.63*.95, .56), "LegL": (0, .38*.63*.95, .56),
                   "Weapon": (.13, 0, 1.06), "Body": (0, 0, 0)},
        "motion": {"LegL": ("rotate", "+Y"), "LegR": ("rotate", "+Y"),
                   "Weapon": ("recoil", "-X"), "Body": ("root", "+Z")},
    },
    "Anvil": {
        "kind": "Bastion", "radius_cm": 34, "height_cm": 38, "builder": "anvil",
        "parts": {"Weapon": range(44, 47),
                  "Body": (*range(0, 44), *range(47, 49))},
        "pivots": {"Weapon": (.25, 0, 1.12), "Body": (0, 0, 0)},
        "motion": {"Weapon": ("recoil", "-X"), "Body": ("root", "+Z")},
    },
    "Cinderthrow": {
        "kind": "Mortar", "radius_cm": 30, "height_cm": 38, "builder": "cinderthrow",
        "parts": {"Weapon": range(33, 37),
                  "Body": (*range(0, 33), *range(37, 44))},
        "pivots": {"Weapon": (-.36, 0, 1.13), "Body": (0, 0, 0)},
        "motion": {"Weapon": ("recoil", "barrel -X"), "Body": ("root", "+Z")},
    },
}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", choices=tuple(SPECS), action="append",
                        help="Regenerate named units. Validation still requires a complete existing pack.")
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return parser.parse_args(argv)


def load_canonical_builders():
    """Compile only allow-listed function declarations from the model source."""
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))
    allowed = {"material", "finish", "box", "cylinder", "beam", "hull", "diamond",
               "torus", "core", "vents", "bolts", "feet", "track", "barrel",
               *(spec["builder"] for spec in SPECS.values())}
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in allowed]
    found = {node.name for node in functions}
    if found != allowed:
        raise RuntimeError("canonical builder declarations changed: " + repr(sorted(allowed - found)))
    module = ast.Module(body=functions, type_ignores=[])
    ast.fix_missing_locations(module)
    namespace = {"bpy": bpy, "bmesh": bmesh, "math": math, "Vector": Vector,
                 "PARTS": [], "MATS": []}
    exec(compile(module, str(SOURCE), "exec"), namespace)
    return namespace


def reset_scene(namespace):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablock in list(bpy.data.materials):
        bpy.data.materials.remove(datablock)
    scene = bpy.context.scene
    bpy.context.preferences.filepaths.save_version = 0
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = .01
    namespace["MATS"] = [
        namespace["material"]("HullDark", (.065, .098, .13), .65, .36),
        namespace["material"]("HullLight", (.29, .37, .40), .55, .30),
        namespace["material"]("Metal", (.14, .19, .22), .83, .26),
        namespace["material"]("TeamPanel", (.018, .53, .43), .38, .30),
        namespace["material"]("CoreGlow", (.10, .96, .72), .1, .22, 2.2),
    ]
    collection = bpy.data.collections.new("Motion parts, complete-model coordinates")
    scene.collection.children.link(collection)
    return collection


def world_vertices(objects):
    bpy.context.view_layer.update()
    return [obj.matrix_world @ vertex.co for obj in objects for vertex in obj.data.vertices]


def bounds(points):
    lo = Vector(tuple(min(point[i] for point in points) for i in range(3)))
    hi = Vector(tuple(max(point[i] for point in points) for i in range(3)))
    return lo, hi


def normalize(point, lo, hi, radius, height):
    span = hi - lo
    scale_xy = 2 * radius / max(span.x, span.y)
    center = (lo + hi) / 2
    return Vector(((point.x-center.x)*scale_xy, (point.y-center.y)*scale_xy,
                   (point.z-lo.z)*height/span.z))


def to_unreal(point):
    """Match FFbxDataConverter::ConvertPos: Unreal X, negated Y, Z."""
    return Vector((point.x, -point.y, point.z))


def join_part(objects, name, collection, mats, lo, hi, radius, height):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    for vertex in obj.data.vertices:
        vertex.co = normalize(obj.matrix_world @ vertex.co, lo, hi, radius, height)
    obj.matrix_world.identity()

    old_materials = list(obj.data.materials)
    old_names = [material.name for material in old_materials]
    polygon_names = [old_names[polygon.material_index] for polygon in obj.data.polygons]
    used = tuple(slot for slot in SLOTS if slot in polygon_names)
    if not used:
        raise RuntimeError(name + " contains no material-backed geometry")
    obj.data.materials.clear()
    for slot in used:
        obj.data.materials.append(mats[SLOTS.index(slot)])
    for polygon, slot in zip(obj.data.polygons, polygon_names):
        polygon.material_index = used.index(slot)

    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(obj.data)
    bm.free()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    bpy.context.view_layer.update()
    obj.data.calc_loop_triangles()
    return obj, used


def vector_values(vector, digits=5):
    return [round(vector[i], digits) for i in range(3)]


def export_part(obj, entry):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    path = ROOT / entry["fbx"]
    bpy.ops.export_scene.fbx(
        filepath=str(path), use_selection=True, object_types={"MESH"}, global_scale=1,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", use_space_transform=True,
        bake_space_transform=False, axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE",
        use_mesh_modifiers=True, use_triangles=True, add_leaf_bones=False, bake_anim=False,
        path_mode="AUTO")
    entry["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()


def validate_roundtrip(objects, records, whole_records):
    checks = []
    imported_by_unit = {unit: [] for unit in SPECS}
    for original, record in zip(objects, records):
        bpy.ops.object.select_all(action="DESELECT")
        bpy.ops.import_scene.fbx(filepath=str(ROOT / record["fbx"]), use_custom_normals=True)
        imported = [obj for obj in bpy.context.selected_objects if obj.type == "MESH"]
        if len(imported) != 1:
            raise RuntimeError(record["name"] + " did not import as one mesh")
        obj = imported[0]
        points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
        lo, hi = bounds([to_unreal(point) for point in points])
        obj.data.calc_loop_triangles()
        slots = [material.name.split(".")[0] for material in obj.data.materials]
        if slots != record["material_slots"]:
            raise RuntimeError(record["name"] + " material slots changed: " + repr(slots))
        if len(obj.data.loop_triangles) != record["triangles"]:
            raise RuntimeError(record["name"] + " triangle count changed")
        if max(abs(lo[i]-record["bounds_min_cm"][i]) for i in range(3)) >= .01 or \
                max(abs(hi[i]-record["bounds_max_cm"][i]) for i in range(3)) >= .01:
            raise RuntimeError(record["name"] + " bounds changed during FBX roundtrip")
        if obj.matrix_world.translation.length >= .001:
            raise RuntimeError(record["name"] + " FBX root shifted")
        tree = KDTree(len(original.data.vertices))
        for index, vertex in enumerate(original.data.vertices):
            tree.insert(vertex.co, index)
        tree.balance()
        error = max(tree.find(point)[2] for point in points)
        if error >= .001:
            raise RuntimeError(record["name"] + " geometry axes changed")
        imported_by_unit[record["unit"]].append(obj)
        checks.append({"name": record["name"], "passed": True,
                       "bounds_min_cm": vector_values(lo), "bounds_max_cm": vector_values(hi),
                       "triangles": len(obj.data.loop_triangles), "material_slots": slots,
                       "root_translation_cm": vector_values(obj.matrix_world.translation),
                       "maximum_vertex_error_cm": round(error, 7)})

    combined_checks = []
    for unit, imported in imported_by_unit.items():
        points = [to_unreal(obj.matrix_world @ vertex.co) for obj in imported for vertex in obj.data.vertices]
        lo, hi = bounds(points)
        triangles = sum(len(obj.data.loop_triangles) for obj in imported)
        expected = whole_records[unit]
        if max(abs(lo[i]-expected["bounds_min_cm"][i]) for i in range(3)) >= .01 or \
                max(abs(hi[i]-expected["bounds_max_cm"][i]) for i in range(3)) >= .01:
            raise RuntimeError(unit + " combined bounds differ from the canonical whole model")
        if triangles != expected["triangles"]:
            raise RuntimeError(unit + " combined triangle count differs from the canonical whole model")
        combined_checks.append({"unit": unit, "passed": True, "part_count": len(imported),
                                "bounds_min_cm": vector_values(lo), "bounds_max_cm": vector_values(hi),
                                "triangles": triangles, "canonical_triangles": expected["triangles"]})
        for obj in imported:
            bpy.data.objects.remove(obj, do_unlink=True)
    return checks, combined_checks


def main():
    args = parse_args()
    for directory in (OUT / "FBX", EVIDENCE):
        directory.mkdir(parents=True, exist_ok=True)
    model_manifest = json.loads((ROOT / "RawAssets/Models/manifest.json").read_text(encoding="utf-8"))
    canonical = {record["name"].removeprefix("SM_"): record for record in model_manifest["assets"]}
    namespace = load_canonical_builders()
    collection = reset_scene(namespace)
    assets, records, whole_records = [], [], {}

    for unit, spec in SPECS.items():
        namespace["PARTS"].clear()
        namespace[spec["builder"]]()
        source_parts = list(namespace["PARTS"])
        claimed = [index for indices in spec["parts"].values() for index in indices]
        if Counter(claimed) != Counter(range(len(source_parts))):
            missing = sorted(set(range(len(source_parts))) - set(claimed))
            extra = sorted(set(claimed) - set(range(len(source_parts))))
            duplicates = sorted(index for index, count in Counter(claimed).items() if count > 1)
            raise RuntimeError("{} partition mismatch: builder={}, claimed={}, missing={}, extra={}, duplicates={}".format(
                unit, len(source_parts), len(claimed), missing, extra, duplicates))
        raw_points = world_vertices(source_parts)
        raw_lo, raw_hi = bounds(raw_points)
        normalized_reference = [normalize(point, raw_lo, raw_hi, spec["radius_cm"], spec["height_cm"])
                                for point in raw_points]
        unreal_reference = [to_unreal(point) for point in normalized_reference]
        whole_lo, whole_hi = bounds(unreal_reference)
        canonical_dimensions = canonical[unit]["dimensions_cm"]
        if max(abs((whole_hi-whole_lo)[axis]-canonical_dimensions[axis]) for axis in range(3)) >= .002:
            raise RuntimeError(unit + " normalized builder bounds differ from the canonical whole-model manifest")
        whole_records[unit] = {"bounds_min_cm": vector_values(whole_lo),
                               "bounds_max_cm": vector_values(whole_hi),
                               "triangles": canonical[unit]["triangles"]}

        for part_name, indices in spec["parts"].items():
            name = "SM_{}_{}".format(unit, part_name)
            obj, slots = join_part([source_parts[index] for index in indices], name, collection,
                                   namespace["MATS"], raw_lo, raw_hi,
                                   spec["radius_cm"], spec["height_cm"])
            points = [vertex.co.copy() for vertex in obj.data.vertices]
            part_lo, part_hi = bounds([to_unreal(point) for point in points])
            triangles = len(obj.data.loop_triangles)
            areas = []
            for triangle in obj.data.loop_triangles:
                a, b, c = (obj.data.vertices[index].co for index in triangle.vertices)
                areas.append((b-a).cross(c-a).length * .5)
            if min(areas) < .00005:
                raise RuntimeError(name + " contains a triangle Unreal would remove")
            raw_pivot = Vector(spec["pivots"][part_name])
            motion, axis = spec["motion"][part_name]
            pivot = (Vector((0, 0, 0)) if motion == "root" else to_unreal(
                     normalize(raw_pivot, raw_lo, raw_hi, spec["radius_cm"], spec["height_cm"])))
            direction = None
            if unit == "Cinderthrow" and part_name == "Weapon":
                # The authored barrel points from (-.36,0,1.13) to (1.28,0,1.49).
                tip = to_unreal(normalize(Vector((1.28, 0, 1.49)), raw_lo, raw_hi,
                                         spec["radius_cm"], spec["height_cm"]))
                direction = (pivot-tip).normalized()
            elif motion == "recoil":
                direction = Vector((-1, 0, 0))
            entry = {
                "name": name, "unit": unit, "kind": spec["kind"], "part": part_name,
                "bounds_min_cm": vector_values(part_lo), "bounds_max_cm": vector_values(part_hi),
                "dimensions_cm": vector_values(part_hi-part_lo),
                "joint_pivot_cm": vector_values(pivot), "motion": motion, "axis": axis,
                "recoil_direction": vector_values(direction, 7) if direction is not None else None,
                "vertices": len(obj.data.vertices), "triangles": triangles,
                "minimum_triangle_area_cm2": min(areas), "material_slots": list(slots),
                "fbx": "RawAssets/Motion/FBX/" + name + ".fbx",
            }
            if args.only is None or unit in args.only:
                export_part(obj, entry)
            else:
                path = ROOT / entry["fbx"]
                if not path.is_file():
                    raise RuntimeError("cannot retain missing " + str(path))
                entry["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
            assets.append(obj)
            records.append(entry)
            print("CINDER_MOTION " + json.dumps(entry, sort_keys=True), flush=True)

        produced = sum(record["triangles"] for record in records if record["unit"] == unit)
        if produced != canonical[unit]["triangles"]:
            raise RuntimeError("{} part triangles {} differ from canonical {}".format(
                unit, produced, canonical[unit]["triangles"]))
        produced_points = [vertex.co for obj in assets if obj.name.startswith("SM_"+unit+"_")
                           for vertex in obj.data.vertices]
        tree = KDTree(len(normalized_reference))
        for index, point in enumerate(normalized_reference):
            tree.insert(point, index)
        tree.balance()
        if len(produced_points) != len(normalized_reference) or \
                max(tree.find(point)[2] for point in produced_points) >= .00001:
            raise RuntimeError(unit + " partition does not preserve the canonical normalized geometry")

    part_checks, combined_checks = validate_roundtrip(assets, records, whole_records)
    manifest = {
        "generator": "scripts/create_motion_assets.py",
        "canonical_builder": "scripts/create_blender_assets.py allow-listed AST function declarations",
        "canonical_builder_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        "blender_version": bpy.app.version_string, "original_geometry": True,
        "unit": "centimeter", "forward_axis": "+X", "up_axis": "+Z",
        "handedness": "Unreal left-handed coordinates",
        "fbx_coordinate_conversion": "post-import Unreal position = (source X, -source Y, source Z)",
        "root_origin": "same (0,0,0) complete-model root in every part; vertices are not recentered",
        "destination": "/Game/Art/Motion", "collision": "none", "nanite": False,
        "material_slot_order": list(SLOTS), "units": whole_records, "assets": records,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    validation = {"passed": True, "checks": part_checks, "combined_checks": combined_checks,
                  "assertions": ["FBX part bounds and root coordinates roundtrip",
                                 "part triangle sums equal canonical whole models",
                                 "combined bounds equal canonical whole models",
                                 "every canonical builder object belongs to exactly one part",
                                 "material slots are ordered and contain geometry"]}
    (EVIDENCE / "fbx-roundtrip-validation.json").write_text(
        json.dumps(validation, indent=2) + "\n", encoding="utf-8")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "Cinderline_Motion_Parts.blend"))
    print("CINDERLINE_MOTION_COMPLETE " + str(OUT), flush=True)


if __name__ == "__main__":
    main()
