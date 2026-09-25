#!/usr/bin/env python3
"""Generate Cinderline's original modular sandstone canyon kit in Blender.

Run with a bounded CPU budget:
  /Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 \
    --python scripts/create_canyon_assets.py

The FBXs use centimeters, +X forward, +Z up, one shared material section, and
an origin at the bottom center. Geometry is presentation-only and deliberately
contains no collision meshes. LOD1/LOD2 are separately authored source meshes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import sys
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "RawAssets/Canyon"
FBX = OUT / "FBX"
for directory in (OUT, FBX):
    directory.mkdir(parents=True, exist_ok=True)

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
parser = argparse.ArgumentParser()
parser.add_argument("--no-render", action="store_true")
ARGS = parser.parse_args(argv)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for block in list(bpy.data.materials):
    bpy.data.materials.remove(block)

scene = bpy.context.scene
bpy.context.preferences.filepaths.save_version = 0
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 0.01


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


material = bpy.data.materials.new("M_CinderCanyonRock")
material.diffuse_color = (0.43, 0.18, 0.065, 1.0)
material.use_nodes = True
shader = material.node_tree.nodes.get("Principled BSDF")
shader.inputs["Base Color"].default_value = (0.43, 0.18, 0.065, 1.0)
shader.inputs["Roughness"].default_value = 0.9
shader.inputs["Metallic"].default_value = 0.0


def chunk_geometry(center, scale, rotation, seed):
    """Return a compact faceted talus chunk as vertices and triangles."""
    rng = random.Random(seed)
    base = [(-1, -0.72, -0.58), (0.82, -0.83, -0.50), (1, 0.66, -0.54),
            (-0.72, 0.92, -0.48), (-0.68, -0.52, 0.58), (0.66, -0.59, 0.72),
            (0.74, 0.54, 0.52), (-0.54, 0.64, 0.68)]
    c, s = math.cos(rotation), math.sin(rotation)
    vertices = []
    for x, y, z in base:
        x *= 1.0 + rng.uniform(-0.12, 0.12)
        y *= 1.0 + rng.uniform(-0.12, 0.12)
        rx, ry = x * c - y * s, x * s + y * c
        vertices.append((center[0] + rx * scale[0], center[1] + ry * scale[1],
                         center[2] + z * scale[2]))
    faces = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
             (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    return vertices, faces


def append_geometry(vertices, faces, extra_vertices, extra_faces):
    offset = len(vertices)
    vertices.extend(extra_vertices)
    faces.extend(tuple(offset + index for index in face) for face in extra_faces)


ROCK_SPECS = [
    ("A", 78.0, 118.0, 0.08, 0.19),
    ("B", 91.0, 104.0, -0.12, 0.36),
    ("C", 72.0, 128.0, 0.18, 0.53),
    ("D", 96.0, 100.0, -0.20, 0.71),
    ("E", 84.0, 132.0, 0.13, 0.87),
    ("F", 89.0, 112.0, -0.06, 1.04),
]
LOD_SPECS = {
    0: (28, 15, 10, 3),
    1: (18, 9, 5, 2),
    2: (12, 5, 2, 1),
}
# Ambient ground scatter. Each entry is (letter, other_xy_cm, height_cm, chunks, phase)
# against the same 100 cm largest-horizontal-dimension contract the rest of the kit uses;
# the runtime scales an instance down to a 10-30 cm footprint, so the authored height is
# a proportion, not a final size. Deliberately single-LOD: at that footprint the chip is
# culled long before an LOD transition could pay for its own import surface, and 2-4
# chunks lands each mesh at 24-48 triangles so 220 of them cost less than one boulder.
CHIP_SPECS = [
    ("A", 74.0, 38.0, 3, 0.11),
    ("B", 88.0, 29.0, 2, 0.47),
    ("C", 66.0, 44.0, 4, 0.73),
    ("D", 81.0, 33.0, 3, 0.95),
]


def normalize_bounds(vertices, target):
    low = [min(vertex[axis] for vertex in vertices) for axis in range(3)]
    high = [max(vertex[axis] for vertex in vertices) for axis in range(3)]
    size = [high[axis] - low[axis] for axis in range(3)]
    result = []
    for vertex in vertices:
        result.append(tuple(
            ((vertex[axis] - low[axis]) / max(size[axis], 1e-6) - (0.5 if axis < 2 else 0.0)) * target[axis]
            for axis in range(3)))
    return result


def rock_mesh(letter, variant, lod):
    other_xy, height, lean, phase = ROCK_SPECS[variant][1:]
    radial_segments, regular_levels, talus_count, ledge_count = LOD_SPECS[lod]
    rng = random.Random(8128 + variant * 311 + lod * 17)
    ledges = [0.24 + 0.17 * index + rng.uniform(-0.025, 0.025) for index in range(ledge_count)]
    levels = {0.0, 1.0}
    for index in range(1, regular_levels):
        levels.add(index / regular_levels)
    for ledge in ledges:
        levels.add(max(0.02, ledge - 0.008))
        levels.add(min(0.98, ledge + 0.008))
    levels = sorted(levels)
    fault_angles = [phase * math.tau, (phase + 0.43) * math.tau]
    fault_ranges = [(0.18, 0.82), (0.43, 0.95)]
    vertices = []
    for ring, z01 in enumerate(levels):
        taper = 1.0 - 0.31 * z01 ** 1.35
        terrace = 1.0 - 0.047 * sum(z01 >= ledge + 0.008 for ledge in ledges)
        center_x = lean * z01 + math.sin(z01 * 5.1 + phase * 4.0) * 0.028
        center_y = math.sin(z01 * 3.7 + phase * 7.0) * 0.035
        for segment in range(radial_segments):
            angle = segment * math.tau / radial_segments
            broad = 1.0 + 0.10 * math.sin(angle * 3 + phase * 5.0) + 0.055 * math.sin(angle * 7 - phase * 9.0)
            fracture = 0.0
            for fault_angle, (start, end) in zip(fault_angles, fault_ranges):
                delta = abs((angle - fault_angle + math.pi) % math.tau - math.pi)
                if start <= z01 <= end and delta < 0.14:
                    fracture += (1.0 - delta / 0.14) * (0.13 if lod == 0 else 0.09)
            radius = taper * terrace * broad * (1.0 - fracture)
            x = center_x + math.cos(angle) * radius
            y = center_y + math.sin(angle) * radius * (other_xy / 100.0)
            z = z01 * height + math.sin(angle * 4 + phase) * (0.35 if ring not in (0, len(levels) - 1) else 0.0)
            vertices.append((x, y, z))
    faces = []
    for ring in range(len(levels) - 1):
        start, next_start = ring * radial_segments, (ring + 1) * radial_segments
        for segment in range(radial_segments):
            following = (segment + 1) % radial_segments
            faces.append((start + segment, start + following, next_start + following))
            faces.append((start + segment, next_start + following, next_start + segment))
    bottom = len(vertices)
    vertices.append((0, 0, 0))
    top = len(vertices)
    vertices.append((lean, 0, height))
    for segment in range(radial_segments):
        following = (segment + 1) % radial_segments
        faces.append((bottom, following, segment))
        last = (len(levels) - 1) * radial_segments
        faces.append((top, last + segment, last + following))

    for index in range(talus_count):
        angle = phase * math.tau + index * math.tau / talus_count + rng.uniform(-0.16, 0.16)
        distance = rng.uniform(0.68, 0.91)
        scale = (rng.uniform(0.10, 0.18), rng.uniform(0.08, 0.15), rng.uniform(4.2, 8.5))
        extra = chunk_geometry((math.cos(angle) * distance, math.sin(angle) * distance * other_xy / 100.0,
                                scale[2] * 0.52), scale, angle, 9400 + variant * 100 + lod * 20 + index)
        append_geometry(vertices, faces, *extra)

    # Attached shelf fragments add unmistakable overhang silhouettes at LOD0/1.
    for index, ledge in enumerate(ledges[:2]):
        angle = phase * math.tau + 1.2 + index * 2.3
        extra = chunk_geometry((math.cos(angle) * 0.71, math.sin(angle) * 0.71 * other_xy / 100.0,
                                ledge * height), (0.24, 0.13, 4.8), angle, 12100 + variant * 10 + lod + index)
        append_geometry(vertices, faces, *extra)

    vertices = normalize_bounds(vertices, (100.0, other_xy, height))
    name = f"SM_CinderCanyon_Rock_{letter}" + ("" if lod == 0 else f"_LOD{lod}")
    return make_object(name, vertices, faces, variant)


def make_object(name, vertices, faces, variant):
    mesh = bpy.data.meshes.new(name + "_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.materials.append(material)
    mesh.update(calc_edges=True)
    uv = mesh.uv_layers.new(name="UVMap")
    colors = mesh.color_attributes.new(name="Color", type="FLOAT_COLOR", domain="CORNER")
    for polygon in mesh.polygons:
        polygon.use_smooth = False
        normal = polygon.normal
        axis = max(range(3), key=lambda item: abs(normal[item]))
        for loop_index in polygon.loop_indices:
            point = mesh.vertices[mesh.loops[loop_index].vertex_index].co
            if axis == 0:
                uv_value = (point.y * 0.025, point.z * 0.025)
            elif axis == 1:
                uv_value = (point.x * 0.025, point.z * 0.025)
            else:
                uv_value = (point.x * 0.025, point.y * 0.025)
            uv.data[loop_index].uv = uv_value
            strata = 0.28 + 0.68 * (0.5 + 0.5 * math.sin(point.z * 0.22 + variant * 0.91))
            rust = max(0.0, math.sin(point.z * 0.071 + point.x * 0.039 + variant) * 0.52)
            cavity = 0.74 + 0.24 * max(0.0, normal.z)
            colors.data[loop_index].color = (strata, rust, cavity, 1.0)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.location = (0, 0, 0)
    return obj


def skirt_mesh(lod):
    segments = (28, 18, 12)[lod]
    rings = (4, 3, 2)[lod]
    vertices = []
    for ring in range(rings):
        z01 = ring / (rings - 1)
        for index in range(segments):
            angle = index * math.tau / segments
            radius = 1.0 - z01 * 0.34 + 0.06 * math.sin(index * 2.7 + 0.4)
            vertices.append((math.cos(angle) * radius, math.sin(angle) * radius * 0.90,
                             z01 * 12.0 + math.sin(angle * 3) * z01 * 0.5))
    faces = []
    for ring in range(rings - 1):
        for index in range(segments):
            following = (index + 1) % segments
            a, b = ring * segments + index, ring * segments + following
            c, d = (ring + 1) * segments + following, (ring + 1) * segments + index
            faces.extend(((a, b, c), (a, c, d)))
    bottom = len(vertices); vertices.append((0, 0, 0))
    top = len(vertices); vertices.append((0, 0, 12))
    for index in range(segments):
        following = (index + 1) % segments
        faces.extend(((bottom, following, index),
                      (top, (rings - 1) * segments + index, (rings - 1) * segments + following)))
    vertices = normalize_bounds(vertices, (100.0, 90.0, 12.0))
    name = "SM_CinderCanyon_CliffMass" + ("" if lod == 0 else f"_LOD{lod}")
    return make_object(name, vertices, faces, 7)


def debris_mesh(lod):
    count = (16, 8, 4)[lod]
    rng = random.Random(15100 + lod)
    vertices, faces = [], []
    for index in range(count):
        angle = index * 2.399 + rng.uniform(-0.18, 0.18)
        distance = 0.10 + 0.78 * math.sqrt((index + 0.5) / count)
        scale = (rng.uniform(0.045, 0.11), rng.uniform(0.04, 0.095), rng.uniform(1.8, 4.8))
        extra = chunk_geometry((math.cos(angle) * distance, math.sin(angle) * distance * 0.68,
                                scale[2] * 0.55), scale, angle, 16100 + lod * 100 + index)
        append_geometry(vertices, faces, *extra)
    vertices = normalize_bounds(vertices, (100.0, 70.0, 20.0))
    name = "SM_CinderCanyon_Debris" + ("" if lod == 0 else f"_LOD{lod}")
    return make_object(name, vertices, faces, 8)


def chip_mesh(letter, variant):
    """Return one ambient scatter chip: a tight faceted pebble group, single LOD."""
    other_xy, height, chunk_count, phase = CHIP_SPECS[variant][1:]
    rng = random.Random(20700 + variant * 137)
    vertices, faces = [], []
    for index in range(chunk_count):
        angle = phase * math.tau + index * math.tau / chunk_count + rng.uniform(-0.22, 0.22)
        # The first chunk sits on the origin so the group always has a dominant mass; the
        # rest stay inside 0.74 so normalizing never stretches the dominant chunk thin.
        distance = 0.0 if index == 0 else rng.uniform(0.34, 0.74)
        scale = (rng.uniform(0.26, 0.44), rng.uniform(0.22, 0.38), rng.uniform(9.0, 17.0))
        extra = chunk_geometry((math.cos(angle) * distance, math.sin(angle) * distance * other_xy / 100.0,
                                scale[2] * 0.48), scale, angle, 20900 + variant * 40 + index)
        append_geometry(vertices, faces, *extra)
    vertices = normalize_bounds(vertices, (100.0, other_xy, height))
    return make_object(f"SM_CinderCanyon_Chip_{letter}", vertices, faces, 9 + variant)


def geometry_record(obj, path, lod):
    obj.data.calc_loop_triangles()
    triangles = len(obj.data.loop_triangles)
    areas = []
    for triangle in obj.data.loop_triangles:
        a, b, c = (obj.data.vertices[index].co for index in triangle.vertices)
        areas.append((b - a).cross(c - a).length * 0.5)
    bounds = [Vector(corner) for corner in obj.bound_box]
    low = [min(point[axis] for point in bounds) for axis in range(3)]
    high = [max(point[axis] for point in bounds) for axis in range(3)]
    size = [high[axis] - low[axis] for axis in range(3)]
    assert abs(low[2]) < 1e-5 and abs((low[0] + high[0]) * 0.5) < 1e-5 and abs((low[1] + high[1]) * 0.5) < 1e-5
    assert len(obj.data.materials) == 1 and obj.data.materials[0].name == material.name
    assert len(obj.data.uv_layers) == 1 and len(obj.data.uv_layers[0].data) == len(obj.data.loops)
    if "_Rock_" in obj.name and lod == 0:
        assert 800 <= triangles <= 2000, f"{obj.name}: {triangles} triangles"
    # Scatter chips are placed by the hundred and never LOD down, so their triangle count
    # is a hard budget rather than a target.
    if "_Chip_" in obj.name:
        assert triangles <= 96, f"{obj.name}: {triangles} triangles"
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
        global_scale=1, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        use_space_transform=True, bake_space_transform=False, axis_forward="-Y", axis_up="Z",
        mesh_smooth_type="FACE", colors_type="LINEAR", use_mesh_modifiers=True,
        use_triangles=True, add_leaf_bones=False, bake_anim=False)
    return {
        "lod": lod,
        "file": str(path.relative_to(ROOT)),
        "sha256": sha256(path),
        "triangles": triangles,
        "vertices": len(obj.data.vertices),
        "bounds_min_cm": [round(value, 5) for value in low],
        "bounds_max_cm": [round(value, 5) for value in high],
        "size_cm": [round(value, 3) for value in size],
        "minimum_triangle_area_cm2": round(min(areas), 8),
        "material_sections": 1,
        "uv_channels": 1,
        "vertex_color": "R strata, G iron staining, B broad cavity response",
    }


def verify_fbx_roundtrip(record):
    """Reimport one exported FBX and prove its portable geometry contract."""
    path = ROOT / record["file"]
    before = set(bpy.data.objects)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.import_scene.fbx(filepath=str(path), use_custom_normals=True)
    imported = [obj for obj in bpy.data.objects if obj not in before and obj.type == "MESH"]
    assert len(imported) == 1, f"{path.name}: expected one round-trip mesh, got {len(imported)}"
    obj = imported[0]
    obj.data.calc_loop_triangles()
    bounds = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = [min(point[axis] for point in bounds) for axis in range(3)]
    high = [max(point[axis] for point in bounds) for axis in range(3)]
    size = [high[axis] - low[axis] for axis in range(3)]
    tolerance = 0.002
    assert len(obj.data.loop_triangles) == record["triangles"], path.name + " triangle count changed on FBX round trip"
    assert all(abs(size[axis] - record["size_cm"][axis]) <= tolerance for axis in range(3)), \
        path.name + " bounds changed on FBX round trip"
    assert abs(low[2]) <= tolerance and abs((low[0] + high[0]) * 0.5) <= tolerance \
        and abs((low[1] + high[1]) * 0.5) <= tolerance, path.name + " lost bottom-center origin"
    assert len(obj.data.materials) == 1, path.name + " must round-trip with one material section"
    assert len(obj.data.uv_layers) >= 1, path.name + " lost UV0 on FBX round trip"
    record["fbx_roundtrip"] = {
        "triangles": len(obj.data.loop_triangles),
        "bounds_min_cm": [round(value, 5) for value in low],
        "bounds_max_cm": [round(value, 5) for value in high],
        "material_sections": len(obj.data.materials),
        "uv_channels": len(obj.data.uv_layers),
        "verified": True,
    }
    imported_meshes = [item.data for item in imported]
    for item in imported:
        bpy.data.objects.remove(item, do_unlink=True)
    for mesh_block in imported_meshes:
        if mesh_block.users == 0:
            bpy.data.meshes.remove(mesh_block)


asset_objects = {}
for variant, spec in enumerate(ROCK_SPECS):
    letter = spec[0]
    asset_objects[f"SM_CinderCanyon_Rock_{letter}"] = [rock_mesh(letter, variant, lod) for lod in range(3)]
asset_objects["SM_CinderCanyon_CliffMass"] = [skirt_mesh(lod) for lod in range(3)]
asset_objects["SM_CinderCanyon_Debris"] = [debris_mesh(lod) for lod in range(3)]
for variant, spec in enumerate(CHIP_SPECS):
    asset_objects[f"SM_CinderCanyon_Chip_{spec[0]}"] = [chip_mesh(spec[0], variant)]

records = []
for name, objects in asset_objects.items():
    lods = []
    for lod, obj in enumerate(objects):
        suffix = "" if lod == 0 else f"_LOD{lod}"
        lods.append(geometry_record(obj, FBX / f"{name}{suffix}.fbx", lod))
    base_size = lods[0]["size_cm"]
    assert all(max(abs(value - base_size[axis]) for axis, value in enumerate(record["size_cm"])) <= 0.002
               for record in lods[1:]), name + " LOD bounds drifted"
    # Single-LOD scatter chips have no ordering to check; every multi-LOD kit piece must
    # still shed triangles monotonically.
    if len(lods) > 1:
        assert lods[0]["triangles"] > lods[1]["triangles"] > lods[2]["triangles"], name + " LOD order invalid"
    records.append({"name": name, "material_slot": material.name, "lods": lods})

for record in records:
    for lod_record in record["lods"]:
        verify_fbx_roundtrip(lod_record)

material_contract = {
    "name": "M_CinderCanyonRock",
    "unreal_path": "/Game/Art/Canyon/Materials/M_CinderCanyonRock",
    "base_colors_linear": {"shadow": [0.23, 0.075, 0.026], "sandstone": [0.58, 0.24, 0.072],
                           "iron": [0.48, 0.105, 0.025]},
    "vertex_color_channels": {"R": "strata blend", "G": "iron stain blend", "B": "ambient occlusion"},
    "roughness": [0.82, 0.95],
    "metallic": 0.0,
    "texture_coordinates": "authored dominant-axis UV0 at 40 cm per tile; material color uses authored strata masks",
}
(OUT / "M_CinderCanyonRock.json").write_text(json.dumps(material_contract, indent=2) + "\n")
manifest = {
    "generator": "scripts/create_canyon_assets.py",
    "blender_version": bpy.app.version_string,
    "license": "original Cinderline procedural geometry and material",
    "unit": "centimeter",
    "forward_axis": "+X",
    "up_axis": "+Z",
    "origin": "bottom center; bounds_min_z_cm is exactly 0",
    "largest_horizontal_dimension_cm": 100.0,
    "collision": "none; authoritative simulation obstacles remain unchanged",
    "nanite": False,
    "material": material_contract,
    "assets": records,
}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "CinderCanyonAssets.blend"))
if ARGS.no_render:
    print("CINDER_CANYON_READY_NO_RENDER " + str(OUT), flush=True)
    raise SystemExit(0)

# Contact sheet uses LOD0 only and does not change the saved source transforms.
for objects in asset_objects.values():
    for lod, obj in enumerate(objects):
        obj.hide_render = lod != 0
positions = [(-225, 300), (-75, 300), (75, 300), (225, 300),
             (-225, 20), (-75, 20), (75, 20), (225, 20),
             (-225, -200), (-75, -200), (75, -200), (225, -200)]
for (name, objects), (x, y) in zip(asset_objects.items(), positions):
    objects[0].location = (x, y, 0)
    bpy.ops.object.text_add(location=(x - 36, y - 50, 1), rotation=(0, 0, 0))
    label = bpy.context.object
    label.data.body = name.replace("SM_CinderCanyon_", "")
    label.data.align_x = "LEFT"
    label.data.size = 10
    label.data.extrude = 0.15

bpy.ops.mesh.primitive_plane_add(size=900, location=(0, 0, -0.2))
ground = bpy.context.object
ground.name = "ContactSheetGround"
ground_mat = bpy.data.materials.new("ContactSheetGround")
ground_mat.diffuse_color = (0.028, 0.020, 0.016, 1)
ground.data.materials.append(ground_mat)

world = scene.world or bpy.data.worlds.new("Canyon studio")
scene.world = world
world.color = (0.018, 0.012, 0.009)
camera_data = bpy.data.cameras.new("Canyon contact camera")
camera = bpy.data.objects.new("Canyon contact camera", camera_data)
scene.collection.objects.link(camera)
camera.location = (520, -1180, 1150)
target = Vector((0, 0, 55))
camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
camera_data.type = "ORTHO"
# Three rows instead of two: the frame has to grow with the kit or the chip row falls
# off the bottom of the sheet the pass is reviewed from.
camera_data.ortho_scale = 1000
camera_data.clip_end = 5000
scene.camera = camera
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.studio_light = "paint.sl"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "WORLD"
scene.display.shading.curvature_ridge_factor = 1.7
scene.display.shading.curvature_valley_factor = 1.2
scene.render.resolution_x = 1600
scene.render.resolution_y = 1250
scene.render.resolution_percentage = 100
scene.render.threads_mode = "FIXED"
scene.render.threads = 2
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(OUT / "contact-sheet.png")
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "CinderCanyonContactSheet.blend"))
bpy.ops.render.render(write_still=True)
print("CINDER_CANYON_READY " + str(OUT), flush=True)
