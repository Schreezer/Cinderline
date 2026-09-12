#!/usr/bin/env python3
"""Author Cinderline's modular visual-target scenery in Blender.

Run with Blender 5.2 or newer:
  Blender --background --python scripts/create_visual_target_scenery.py

Delivery meshes use centimeters, +X forward, +Z up, one material slot, and a
bottom-centered origin. The six rocks contain authored strata and detached
fragments. They are intended for collision-free instancing inside authoritative
simulation obstacles.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import random
import sys

import bpy
import bmesh
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "RawAssets/VisualTarget/Scenery"
EVIDENCE = ROOT / "artifacts/visual-target/scenery"
FBX = OUT / "FBX"
EXTERNAL = OUT / "External/namaqualand_boulders_01"
for directory in (FBX, EVIDENCE):
    directory.mkdir(parents=True, exist_ok=True)

args_after_separator = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
parser = argparse.ArgumentParser()
parser.add_argument("--no-render", action="store_true")
ARGS = parser.parse_args(args_after_separator)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for block in list(bpy.data.materials):
    bpy.data.materials.remove(block)

scene = bpy.context.scene
bpy.context.preferences.filepaths.save_version = 0
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 0.01


def material(name, color, metallic, roughness):
    result = bpy.data.materials.new(name)
    result.diffuse_color = (*color, 1)
    result.use_nodes = True
    shader = result.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    return result


ROCK = material("Rock", (0.16, 0.13, 0.11), 0.02, 0.91)
METAL = material("Metal", (0.12, 0.16, 0.17), 0.72, 0.38)


def finish_parts(name, parts, mat, normalize=False):
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    if len(parts) > 1:
        bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name
    obj.data.name = name + "_Mesh"
    for polygon in obj.data.polygons:
        polygon.use_smooth = False
    if normalize:
        corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
        low = Vector((min(p.x for p in corners), min(p.y for p in corners), min(p.z for p in corners)))
        high = Vector((max(p.x for p in corners), max(p.y for p in corners), max(p.z for p in corners)))
        size = high - low
        scale = 100.0 / max(size.x, size.y, size.z)
        obj.scale = (scale, scale, scale)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
        low = Vector((min(p.x for p in corners), min(p.y for p in corners), min(p.z for p in corners)))
        high = Vector((max(p.x for p in corners), max(p.y for p in corners), max(p.z for p in corners)))
        obj.location -= Vector(((low.x + high.x) * 0.5, (low.y + high.y) * 0.5, low.z))
        bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    bm.normal_update()
    uv0 = bm.loops.layers.uv.verify()
    # Dominant-axis box projection gives every face stable UV0 coverage. One UV
    # unit spans 50 source centimeters; the runtime rock material may also use
    # world alignment to preserve density after nonuniform ISM scaling.
    for face in bm.faces:
        axis = max(range(3), key=lambda index: abs(face.normal[index]))
        for loop in face.loops:
            point = loop.vert.co
            if axis == 0:
                uv = (point.y * 0.02, point.z * 0.02)
            elif axis == 1:
                uv = (point.x * 0.02, point.z * 0.02)
            else:
                uv = (point.x * 0.02, point.y * 0.02)
            loop[uv0].uv = uv
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    return obj


def ico_part(location, scale, subdivisions=3):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=1, location=location)
    obj = bpy.context.object
    obj.scale = scale
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return obj


def deform_rock(obj, seed, ledge_phase, fault_axis):
    rng = random.Random(seed)
    directions = []
    for _ in range(8):
        direction = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.4, 0.7)))
        direction.normalize()
        directions.append((direction, rng.uniform(0.035, 0.10)))
    for vertex in obj.data.vertices:
        point = vertex.co
        normalized = point.normalized()
        noise = sum(math.sin(normalized.dot(direction) * 9.0 + i * 1.73) * amplitude
                    for i, (direction, amplitude) in enumerate(directions))
        z01 = max(0.0, min(1.0, (point.z + 1.0) * 0.5))
        taper = 0.72 + 0.38 * math.sin(z01 * math.pi)
        stratum = math.floor((z01 * 8.0 + ledge_phase) * 2.0) / 2.0
        ledge = 1.0 + 0.075 * math.sin(stratum * math.pi * 0.72)
        point.x *= taper * ledge * (1.0 + noise)
        point.y *= taper * ledge * (1.0 - noise * 0.55)
        point.z *= 1.0 + noise * 0.25
        fault = point.x * fault_axis.x + point.y * fault_axis.y
        if fault > 0.18:
            point.x += fault_axis.x * 0.07
            point.y += fault_axis.y * 0.07
        vertex.co = point


def create_rock(index):
    rng = random.Random(6200 + index * 137)
    main = ico_part((0, 0, 0.58), (0.62 + rng.random() * 0.12,
        0.48 + rng.random() * 0.16, 0.72 + rng.random() * 0.14), 4)
    angle = rng.uniform(0, math.tau)
    deform_rock(main, 900 + index, rng.random(), Vector((math.cos(angle), math.sin(angle), 0)))
    parts = [main]
    # A second shelf makes the silhouette read as fractured strata rather than a stretched boulder.
    shelf = ico_part((rng.uniform(-0.24, 0.24), rng.uniform(-0.22, 0.22), rng.uniform(0.31, 0.54)),
        (rng.uniform(0.34, 0.52), rng.uniform(0.25, 0.41), rng.uniform(0.16, 0.27)), 2)
    deform_rock(shelf, 1900 + index, rng.random(), Vector((-math.sin(angle), math.cos(angle), 0)))
    parts.append(shelf)
    # Detached fragments remain inside the nominal footprint and break the base contour.
    for fragment in range(2 + index % 2):
        fragment_angle = angle + fragment * 2.1 + rng.uniform(-0.35, 0.35)
        distance = rng.uniform(0.42, 0.56)
        piece = ico_part((math.cos(fragment_angle) * distance, math.sin(fragment_angle) * distance, rng.uniform(0.10, 0.18)),
            (rng.uniform(0.12, 0.19), rng.uniform(0.10, 0.17), rng.uniform(0.13, 0.24)), 2)
        deform_rock(piece, 2700 + index * 5 + fragment, rng.random(), Vector((math.cos(angle), math.sin(angle), 0)))
        parts.append(piece)
    return finish_parts(f"SM_CinderScenery_Rock_{chr(65 + index)}", parts, ROCK, True)


def load_photogrammetry_sources():
    source = EXTERNAL / "namaqualand_boulders_01_2k.gltf"
    assert source.is_file(), f"missing CC0 Poly Haven model {source}"
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(source), import_pack_images=False)
    imported = sorted((obj for obj in bpy.data.objects if obj not in before and obj.type == "MESH"),
                      key=lambda obj: obj.name)
    assert len(imported) == 2, f"Poly Haven source produced {len(imported)} meshes"
    assert all(len(obj.data.uv_layers) >= 1 for obj in imported)
    material = imported[0].data.materials[0]
    assert material and all(obj.data.materials[0] == material for obj in imported)
    material.name = "RockPhoto"
    for obj in imported:
        obj.hide_render = True
    return imported, material


def create_photogrammetry_rock(index, sources, material):
    source = sources[index % len(sources)]
    obj = source.copy()
    obj.data = source.data.copy()
    bpy.context.collection.objects.link(obj)
    obj.location = (0, 0, 0)
    obj.rotation_euler = (0, 0, 0)
    obj.scale = (1, 1, 1)

    # Preserve the scanned surface and UV atlas while giving the two source
    # boulders six distinct silhouettes. These are gentle affine/taper changes,
    # not synthetic surface noise.
    variants = ((1.00, 0.92, 1.05, 0.04), (0.88, 1.04, 1.15, -0.07),
                (1.08, 0.84, 0.91, 0.10), (0.94, 1.08, 1.00, -0.04),
                (1.07, 0.90, 1.13, 0.07), (0.86, 1.06, 0.94, -0.09))
    scale_x, scale_y, scale_z, shear = variants[index]
    low_z = min(vertex.co.z for vertex in obj.data.vertices)
    high_z = max(vertex.co.z for vertex in obj.data.vertices)
    height = max(1e-6, high_z - low_z)
    for vertex in obj.data.vertices:
        alpha = (vertex.co.z - low_z) / height
        taper = 1.0 - 0.10 * alpha + 0.035 * math.sin(alpha * math.pi * 2.0 + index)
        vertex.co.x = vertex.co.x * scale_x * taper + (vertex.co.z - low_z) * shear
        vertex.co.y = vertex.co.y * scale_y * (1.0 + 0.035 * math.sin(alpha * 5.1 + index * 0.7))
        vertex.co.z = low_z + (vertex.co.z - low_z) * scale_z

    obj.name = f"SM_CinderScenery_Rock_{chr(65 + index)}"
    obj.data.name = obj.name + "_Mesh"
    obj.data.materials.clear()
    obj.data.materials.append(material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    obj.data.calc_loop_triangles()
    target_triangles = (2600, 3100, 2200, 3000, 2450, 3300)[index]
    current_triangles = len(obj.data.loop_triangles)
    modifier = obj.modifiers.new("Mobile photogrammetry reduction", "DECIMATE")
    modifier.decimate_type = "COLLAPSE"
    modifier.ratio = min(1.0, target_triangles / current_triangles)
    modifier.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=modifier.name)

    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = Vector((min(p.x for p in corners), min(p.y for p in corners), min(p.z for p in corners)))
    high = Vector((max(p.x for p in corners), max(p.y for p in corners), max(p.z for p in corners)))
    size = high - low
    uniform = 100.0 / max(size.x, size.y, size.z)
    obj.scale = (uniform, uniform, uniform)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = Vector((min(p.x for p in corners), min(p.y for p in corners), min(p.z for p in corners)))
    high = Vector((max(p.x for p in corners), max(p.y for p in corners), max(p.z for p in corners)))
    obj.location -= Vector(((low.x + high.x) * 0.5, (low.y + high.y) * 0.5, low.z))
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    obj.data.update()
    return obj


def create_cliff_mass():
    # A shallow irregular stone-and-earth foot closes small gaps between scanned
    # boulders. It stays almost flush with the terrain so it cannot read as a
    # constructed pedestal at the gameplay camera distance.
    perimeter = [(-0.50, -0.38), (-0.38, -0.50), (-0.12, -0.46), (0.16, -0.50),
                 (0.40, -0.45), (0.50, -0.28), (0.46, -0.02), (0.50, 0.24),
                 (0.39, 0.46), (0.13, 0.50), (-0.14, 0.45), (-0.39, 0.50),
                 (-0.50, 0.31), (-0.46, 0.05), (-0.50, -0.18)]
    ring_scales = (1.0, 0.76)
    ring_heights = (0.0, 0.10)
    ring_offsets = ((0.0, 0.0), (0.025, -0.018))
    vertices = []
    count = len(perimeter)
    for ring, (scale, height) in enumerate(zip(ring_scales, ring_heights)):
        offset_x, offset_y = ring_offsets[ring]
        for index, (x, y) in enumerate(perimeter):
            wave = 0.0 if ring == 0 else 0.035 * math.sin(index * 2.17)
            vertices.append((x * scale + offset_x, y * scale + offset_y,
                height + wave))
    faces = [tuple(reversed(range(count)))]
    for ring in range(len(ring_scales) - 1):
        lower, upper = ring * count, (ring + 1) * count
        faces += [(lower + index, lower + (index + 1) % count,
                   upper + (index + 1) % count, upper + index) for index in range(count)]
    faces.append(tuple(range((len(ring_scales) - 1) * count, len(ring_scales) * count)))
    mesh = bpy.data.meshes.new("CliffMass_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("CliffMass", mesh)
    bpy.context.collection.objects.link(obj)
    result = finish_parts("SM_CinderScenery_CliffMass", [obj], PHOTO_ROCK, True)
    for polygon in result.data.polygons:
        polygon.use_smooth = True
    return result


def cube_part(location, scale, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object
    obj.scale = scale
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        modifier = obj.modifiers.new("Machined edge", "BEVEL")
        modifier.width = bevel
        modifier.segments = 1
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    return obj


def cylinder_part(location, radius, depth, vertices=12, rotation=(0, 0, 0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth,
        location=location, rotation=rotation)
    return bpy.context.object


def create_pad():
    # Keep this broad, nearly planar foundation un-bevelled. Bevels whose width is
    # close to the slab thickness create valid but sub-microscopic corner wedges
    # that Unreal removes during static-mesh import.
    parts = [cube_part((0, 0, 0.025), (1.0, 1.0, 0.05))]
    for x, y in ((-0.77, -0.77), (-0.77, 0.77), (0.77, -0.77), (0.77, 0.77)):
        parts.append(cube_part((x, y, 0.075), (0.12, 0.12, 0.10)))
    for side in (-1, 1):
        parts.append(cube_part((0, side * 0.86, 0.07), (0.58, 0.035, 0.045)))
    return finish_parts("SM_CinderScenery_Pad", parts, METAL, True)


def create_road():
    # A single shallow, uneven strip scales cleanly between buildings. Raised
    # longitudinal rails become oversized poles when the instance is stretched.
    stations = (-0.50, -0.28, -0.04, 0.23, 0.50)
    widths = (0.34, 0.40, 0.37, 0.41, 0.35)
    top_z = (0.022, 0.026, 0.021, 0.025, 0.020)
    vertices = []
    for x, width, z in zip(stations, widths, top_z):
        vertices += [(x, -width, 0.0), (x, width, 0.0),
                     (x, -width, z), (x, width, z)]
    faces = []
    for index in range(len(stations) - 1):
        a, b = index * 4, (index + 1) * 4
        faces += [(a, b, b + 2, a + 2), (a + 1, a + 3, b + 3, b + 1),
                  (a + 2, b + 2, b + 3, a + 3), (a + 1, b + 1, b, a)]
    faces += [(0, 2, 3, 1), (len(vertices) - 4, len(vertices) - 3,
                              len(vertices) - 1, len(vertices) - 2)]
    mesh = bpy.data.meshes.new("Road_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("Road", mesh)
    bpy.context.collection.objects.link(obj)
    return finish_parts("SM_CinderScenery_Road", [obj], METAL, True)


def create_pipe():
    parts = [cylinder_part((0, 0, 0), 0.18, 1.0, 14)]
    for z in (-0.44, 0.44):
        parts.append(cylinder_part((0, 0, z), 0.24, 0.08, 14))
    return finish_parts("SM_CinderScenery_Pipe", parts, METAL, True)


def create_crate():
    parts = [cube_part((0, 0, 0.5), (1.0, 0.82, 1.0), 0.06)]
    for z in (0.12, 0.88):
        parts.append(cube_part((0, 0, z), (1.06, 0.88, 0.07), 0.016))
    for x in (-0.88, 0.88):
        parts.append(cube_part((x, 0, 0.5), (0.07, 0.88, 0.88), 0.012))
    return finish_parts("SM_CinderScenery_Crate", parts, METAL, True)


def create_debris():
    parts = []
    for index, (location, scale) in enumerate([
        ((-0.28, 0.05, 0.11), (0.42, 0.23, 0.18)),
        ((0.19, -0.14, 0.08), (0.31, 0.19, 0.14)),
        ((0.32, 0.22, 0.055), (0.19, 0.12, 0.10))]):
        part = ico_part(location, scale, 2)
        deform_rock(part, 4100 + index, index * 0.27, Vector((0.8, 0.6, 0)))
        parts.append(part)
    return finish_parts("SM_CinderScenery_Debris", parts, ROCK, True)


def create_photogrammetry_debris(source, material):
    obj = source.copy()
    obj.data = source.data.copy()
    bpy.context.collection.objects.link(obj)
    obj.name = "SM_CinderScenery_Debris"
    obj.data.name = obj.name + "_Mesh"
    obj.location = (0, 0, 0)
    obj.scale = (1.0, 0.78, 0.55)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.calc_loop_triangles()
    modifier = obj.modifiers.new("Small rock reduction", "DECIMATE")
    modifier.decimate_type = "COLLAPSE"
    modifier.ratio = min(1.0, 620.0 / len(obj.data.loop_triangles))
    modifier.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.data.materials.clear()
    obj.data.materials.append(material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = Vector((min(p.x for p in corners), min(p.y for p in corners), min(p.z for p in corners)))
    high = Vector((max(p.x for p in corners), max(p.y for p in corners), max(p.z for p in corners)))
    uniform = 100.0 / max(high.x - low.x, high.y - low.y, high.z - low.z)
    obj.scale = (uniform, uniform, uniform)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = Vector((min(p.x for p in corners), min(p.y for p in corners), min(p.z for p in corners)))
    high = Vector((max(p.x for p in corners), max(p.y for p in corners), max(p.z for p in corners)))
    obj.location -= Vector(((low.x + high.x) * 0.5, (low.y + high.y) * 0.5, low.z))
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    obj.data.update()
    return obj


photo_sources, PHOTO_ROCK = load_photogrammetry_sources()
photo_rocks = [create_photogrammetry_rock(index, photo_sources, PHOTO_ROCK) for index in range(6)]
assets = photo_rocks + [create_cliff_mass(), create_pad(), create_road(), create_pipe(), create_crate(),
                        create_photogrammetry_debris(photo_rocks[1], PHOTO_ROCK)]


def export_asset(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    obj.data.calc_loop_triangles()
    triangles = len(obj.data.loop_triangles)
    triangle_areas = [((obj.data.vertices[triangle.vertices[1]].co
        - obj.data.vertices[triangle.vertices[0]].co).cross(
        obj.data.vertices[triangle.vertices[2]].co
        - obj.data.vertices[triangle.vertices[0]].co).length * 0.5)
        for triangle in obj.data.loop_triangles]
    assert min(triangle_areas) >= 0.00005, \
        f"{obj.name} has {min(triangle_areas):.12g} cm2 triangle below Unreal's threshold"
    if obj.name.startswith("SM_CinderScenery_Rock_"):
        assert 1000 <= triangles <= 4000, f"{obj.name} has {triangles} triangles"
    else:
        assert triangles <= 1200, f"{obj.name} has {triangles} triangles"
    assert len(obj.data.materials) == 1
    assert len(obj.data.uv_layers) >= 1 and len(obj.data.uv_layers[0].data) == len(obj.data.loops)
    uv_values = [value.uv for value in obj.data.uv_layers[0].data]
    assert all(math.isfinite(uv.x) and math.isfinite(uv.y) for uv in uv_values)
    covered_faces = 0
    for polygon in obj.data.polygons:
        triangle_uv = [uv_values[index] for index in polygon.loop_indices]
        uv_area = abs((triangle_uv[1].x - triangle_uv[0].x) * (triangle_uv[2].y - triangle_uv[0].y)
                      - (triangle_uv[1].y - triangle_uv[0].y) * (triangle_uv[2].x - triangle_uv[0].x)) * 0.5
        covered_faces += uv_area > 1e-16
    uv0_coverage = covered_faces / max(1, len(obj.data.polygons))
    assert uv0_coverage == 1.0, f"{obj.name} UV0 covers {uv0_coverage:.6f} of triangles"
    path = FBX / f"{obj.name}.fbx"
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
        global_scale=1, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        use_space_transform=True, bake_space_transform=False, axis_forward="-Y", axis_up="Z",
        mesh_smooth_type="FACE", use_mesh_modifiers=True,
        use_triangles=True, add_leaf_bones=False, bake_anim=False)
    bounds = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low_cm = [min(p[axis] for p in bounds) for axis in range(3)]
    high_cm = [max(p[axis] for p in bounds) for axis in range(3)]
    size_cm = [high_cm[axis] - low_cm[axis] for axis in range(3)]
    return {
        "name": obj.name,
        "file": str(path.relative_to(ROOT)),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "triangles": triangles,
        "minimum_triangle_area_cm2": min(triangle_areas),
        "bounds_min_cm": [round(value, 5) for value in low_cm],
        "bounds_max_cm": [round(value, 5) for value in high_cm],
        "size_cm": [round(value, 2) for value in size_cm],
        "material_slot": obj.data.materials[0].name,
        "uv0_source": ("preserved Poly Haven photogrammetry atlas" if obj.data.materials[0].name == "RockPhoto"
                       else "authored dominant-axis box projection"),
        "uv_channels": len(obj.data.uv_layers),
        "uv0_loops": len(uv_values),
        "uv0_loop_coverage": uv0_coverage,
    }


records = [export_asset(asset) for asset in assets]


def validate_fbx_roundtrip(record):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(ROOT / record["file"]), use_anim=False,
        automatic_bone_orientation=False)
    imported = [obj for obj in bpy.data.objects if obj not in before and obj.type == "MESH"]
    assert len(imported) == 1, f"{record['name']} roundtrip produced {len(imported)} meshes"
    mesh = imported[0].data
    mesh.calc_loop_triangles()
    assert len(mesh.loop_triangles) == record["triangles"]
    triangle_areas = [((mesh.vertices[triangle.vertices[1]].co
        - mesh.vertices[triangle.vertices[0]].co).cross(
        mesh.vertices[triangle.vertices[2]].co
        - mesh.vertices[triangle.vertices[0]].co).length * 0.5)
        for triangle in mesh.loop_triangles]
    assert min(triangle_areas) >= 0.00005, \
        f"{record['name']} FBX has {min(triangle_areas):.12g} cm2 triangle below Unreal's threshold"
    assert len(mesh.uv_layers) >= 1 and len(mesh.uv_layers[0].data) == len(mesh.loops)
    uv_values = [value.uv for value in mesh.uv_layers[0].data]
    assert all(math.isfinite(uv.x) and math.isfinite(uv.y) for uv in uv_values)
    covered_faces = 0
    for polygon in mesh.polygons:
        triangle_uv = [uv_values[index] for index in polygon.loop_indices]
        uv_area = abs((triangle_uv[1].x - triangle_uv[0].x) * (triangle_uv[2].y - triangle_uv[0].y)
                      - (triangle_uv[1].y - triangle_uv[0].y) * (triangle_uv[2].x - triangle_uv[0].x)) * 0.5
        covered_faces += uv_area > 1e-16
    uv0_coverage = covered_faces / max(1, len(mesh.polygons))
    assert uv0_coverage == 1.0, f"{record['name']} roundtrip UV0 covers {uv0_coverage:.6f} of triangles"
    result = {"name": record["name"], "triangles": len(mesh.loop_triangles),
              "uv_channels": len(mesh.uv_layers), "uv0_loops": len(mesh.uv_layers[0].data),
              "uv0_loop_coverage": uv0_coverage,
              "minimum_triangle_area_cm2": min(triangle_areas),
              "triangles_below_unreal_threshold": sum(area < 0.00005 for area in triangle_areas)}
    bpy.ops.object.select_all(action="DESELECT")
    for obj in imported:
        obj.select_set(True)
    bpy.ops.object.delete(use_global=False)
    return result


roundtrip = [validate_fbx_roundtrip(record) for record in records]
provenance = json.loads((EXTERNAL / "provenance.json").read_text())
assert provenance["asset_id"] == "namaqualand_boulders_01" and provenance["license"] == "CC0 1.0 Universal"
for source_record in provenance["files"]:
    source_path = EXTERNAL / source_record["path"]
    assert source_path.is_file() and hashlib.sha256(source_path.read_bytes()).hexdigest() == source_record["sha256"]
manifest = {
    "generator": "scripts/create_visual_target_scenery.py",
    "blender_version": bpy.app.version_string,
    "unit": "centimeter",
    "forward_axis": "+X",
    "up_axis": "+Z",
    "fbx_coordinate_conversion": "post-import Unreal position = (source X, -source Y, source Z)",
    "origin": "bottom center for rocks; authored local origin for low modular props",
    "collision": "none; presentation only",
    "fog_contract": "runtime creates scenery only from explored obstacles, friendly complete buildings, and known resource memory",
    "materials": ["RockPhoto", "Rock", "Metal"],
    "uv0": "photogrammetry rocks retain their source atlas; procedural support meshes use box projection",
    "external_source": {"asset_id": provenance["asset_id"], "title": provenance["title"],
                        "source_page": provenance["source_page"], "license": provenance["license"],
                        "provenance": str((EXTERNAL / "provenance.json").relative_to(ROOT))},
    "assets": records,
}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
(EVIDENCE / "asset-validation.json").write_text(json.dumps({
    "passed": True,
    "asset_count": len(records),
    "rock_variants": 6,
    "rock_triangle_budget": [1000, 4000],
    "prop_triangle_cap": 1200,
    "collision": "none",
    "instances": "runtime ISM batches with distance culling",
    "photogrammetry_source": manifest["external_source"],
    "checks": records,
    "fbx_roundtrip": roundtrip,
}, indent=2) + "\n")
(EVIDENCE / "fbx-roundtrip-validation.json").write_text(json.dumps({
    "passed": True,
    "asset_count": len(roundtrip),
    "required_uv_channels": 1,
    "checks": roundtrip,
}, indent=2) + "\n")

for index, asset in enumerate(assets):
    asset.location = ((index % 4 - 1.5) * 165, (index // 4 - 1.0) * 165, 0)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "CinderVisualTargetScenery.blend"))

if ARGS.no_render:
    print(f"CINDER_SCENERY_READY_NO_RENDER {OUT}", flush=True)
    raise SystemExit(0)

for asset in assets:
    asset.hide_render = False
world = scene.world or bpy.data.worlds.new("Scenery studio")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.035, 0.025, 0.022, 1)
world.node_tree.nodes["Background"].inputs[1].default_value = 0.38
camera_data = bpy.data.cameras.new("Scenery contact camera")
camera = bpy.data.objects.new("Scenery contact camera", camera_data)
scene.collection.objects.link(camera)
camera.location = (520, -860, 710)
camera.rotation_euler = (-camera.location).to_track_quat("-Z", "Y").to_euler()
camera_data.type = "ORTHO"
camera_data.ortho_scale = 720
camera_data.clip_end = 5000
scene.camera = camera
for name, location, energy, color, size in [
    ("Warm key", (400, -500, 800), 7000000, (1.0, 0.56, 0.31), 400),
    ("Cool fill", (-400, -200, 500), 4200000, (0.38, 0.62, 1.0), 400),
    ("Rim", (200, 500, 600), 5600000, (1.0, 0.76, 0.54), 300)]:
    data = bpy.data.lights.new(name, "AREA")
    data.energy, data.color, data.shape, data.size = energy, color, "DISK", size
    light = bpy.data.objects.new(name, data)
    scene.collection.objects.link(light)
    light.location = location
    light.rotation_euler = (-Vector(location)).to_track_quat("-Z", "Y").to_euler()
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 1600
scene.render.resolution_y = 1100
scene.render.resolution_percentage = 100
scene.render.threads_mode = "FIXED"
scene.render.threads = 2
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(EVIDENCE / "scenery-contact-sheet.png")
scene.view_settings.look = "AgX - Medium High Contrast"
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "CinderVisualTargetScenery_ContactSheet.blend"))
bpy.ops.render.render(write_still=True)
print(f"CINDER_SCENERY_READY {OUT}", flush=True)
