#!/usr/bin/env python3
"""Author the original Cinderline frontier reed clump in an isolated Blender process.

  /Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 \
    --python scripts/create_frontier_foliage.py

No external assets, alpha cards, collision, or Nanite. The exported FBX has a
bottom-center origin and centimeters. Vertex R is the root-pinned bending mask;
G mixes a few dry stalks into the olive blades. The preview is not a game capture.
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
OUT = ROOT / "RawAssets/FrontierFoliage"
FBX = OUT / "FBX"
NAME = "SM_CinderFrontier_Grass_A"
MATERIAL = "M_CinderFrontierFoliage"
for directory in (OUT, FBX):
    directory.mkdir(parents=True, exist_ok=True)
parser = argparse.ArgumentParser()
parser.add_argument("--no-render", action="store_true")
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])

# This script is intentionally an isolated background authoring job. Never run
# it through MCP in a user's live Blender scene.
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 0.01
bpy.context.preferences.filepaths.save_version = 0

vertices, faces, colors, uvs = [], [], [], []
rng = random.Random(1403)


def add_vertex(point, height_mask, tone, uv):
    index = len(vertices)
    vertices.append(tuple(point))
    colors.append((height_mask, tone, 0.80 + 0.20 * height_mask, 1.0))
    uvs.append(uv)
    return index


# A folded, bent ribbon is real opaque geometry. Broad uneven leaves stay
# legible at a 15-50 cm runtime footprint; no opacity texture is required.
for blade in range(10):
    angle = blade * math.tau / 10 + rng.uniform(-0.22, 0.22)
    direction = Vector((math.cos(angle), math.sin(angle), 0))
    width_axis = Vector((-math.sin(angle), math.cos(angle), 0))
    base = direction * rng.uniform(0.8, 3.0)
    height = rng.uniform(17, 28)
    reach = rng.uniform(10, 20)
    width = rng.uniform(2.6, 4.5)
    tone = rng.uniform(0.02, 0.25) if blade != 6 else 0.52
    points = [
        (0.00, -0.32, 0.00), (0.00, 0.32, 0.00),
        (0.45, -0.50, 0.33), (0.45, 0.00, 0.29), (0.45, 0.50, 0.33),
        (0.80, -0.28, 0.73), (0.80, 0.28, 0.73),
        (1.00, 0.00, 1.00),
    ]
    ids = []
    for t, side, bend in points:
        # Leaves first rise, then bow outward; the central fold gives a restrained
        # broad highlight rather than a perfectly flat green polygon.
        point = base + width_axis * (side * width) + direction * (reach * bend)
        point.z = height * (t - 0.13 * t * t)
        if t == 0.45 and side == 0:
            point += direction * -1.1
        ids.append(add_vertex(point, t, tone, (side + 0.5, t)))
    for triangle in [(0, 1, 3), (0, 3, 2), (1, 4, 3),
                     (2, 3, 5), (3, 6, 5), (3, 4, 6), (5, 6, 7)]:
        faces.append(tuple(ids[index] for index in triangle))

# Two seed stems: a narrow bent blade and a faceted seed head. They are accents,
# not a bright floral focal point. Each stem/head pair adds eleven triangles.
for index, (angle, height, lean) in enumerate(((0.5, 33.0, 4.0), (3.1, 29.0, 6.0))):
    direction = Vector((math.cos(angle), math.sin(angle), 0))
    side = Vector((-math.sin(angle), math.cos(angle), 0))
    base = direction * (1.0 + index)
    stem = []
    for t, w in ((0, -0.30), (0, 0.30), (.70, -.22), (.70, .22), (.92, 0)):
        point = base + direction * (lean * t * t) + side * w
        point.z = height * t
        stem.append(add_vertex(point, t, .84, (0 if w < 0 else 1, t)))
    faces.extend([(stem[0], stem[1], stem[2]), (stem[1], stem[3], stem[2]),
                  (stem[2], stem[3], stem[4])])
    center = base + direction * (lean * .89)
    center.z = height * .87
    head = []
    for delta in (Vector((0, 0, -2.2)), side * 1.25, direction * .85,
                  side * -1.25, direction * -.85, Vector((0, 0, 3.7))):
        point = center + delta
        head.append(add_vertex(point, min(1.0, point.z / height), .94, (.5, point.z / height)))
    for k in range(4):
        a, b = 1 + k, 1 + (k + 1) % 4
        faces.extend([(head[0], head[b], head[a]), (head[5], head[a], head[b])])

low = [min(p[axis] for p in vertices) for axis in range(3)]
high = [max(p[axis] for p in vertices) for axis in range(3)]
factor = 40.0 / max(high[0] - low[0], high[1] - low[1])
vertices = [tuple((p[axis] - (low[axis] if axis == 2 else (low[axis] + high[axis]) * .5)) * factor
                  for axis in range(3)) for p in vertices]
mesh = bpy.data.meshes.new(NAME)
mesh.from_pydata(vertices, [], faces)
mesh.update()
obj = bpy.data.objects.new(NAME, mesh)
scene.collection.objects.link(obj)
attr = mesh.color_attributes.new(name="Color", type="FLOAT_COLOR", domain="CORNER")
uv = mesh.uv_layers.new(name="UV0")
for polygon in mesh.polygons:
    polygon.use_smooth = False
    for loop_index in polygon.loop_indices:
        vertex_index = mesh.loops[loop_index].vertex_index
        attr.data[loop_index].color = colors[vertex_index]
        uv.data[loop_index].uv = uvs[vertex_index]
mesh.color_attributes.active_color = attr

material = bpy.data.materials.new(MATERIAL)
material.use_nodes = True
material.diffuse_color = (.19, .22, .095, 1)
nodes = material.node_tree.nodes
links = material.node_tree.links
shader = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
shader.inputs["Roughness"].default_value = .95
shader.inputs["Specular IOR Level"].default_value = .15
vertex_color = nodes.new("ShaderNodeVertexColor")
vertex_color.layer_name = "Color"
channels = nodes.new("ShaderNodeSeparateColor")
links.new(vertex_color.outputs["Color"], channels.inputs["Color"])
height_color = nodes.new("ShaderNodeMixRGB")
height_color.blend_type = "MIX"
height_color.inputs[1].default_value = (.08, .105, .040, 1)
height_color.inputs[2].default_value = (.205, .245, .105, 1)
links.new(channels.outputs["Red"], height_color.inputs[0])
color_mix = nodes.new("ShaderNodeMixRGB")
color_mix.blend_type = "MIX"
color_mix.inputs[2].default_value = (.31, .25, .115, 1)
links.new(height_color.outputs["Color"], color_mix.inputs[1])
links.new(channels.outputs["Green"], color_mix.inputs[0])
links.new(color_mix.outputs["Color"], shader.inputs["Base Color"])
obj.data.materials.append(material)

bpy.context.view_layer.objects.active = obj
obj.select_set(True)
mesh.calc_loop_triangles()
areas = [(mesh.vertices[t.vertices[1]].co - mesh.vertices[t.vertices[0]].co).cross(
         mesh.vertices[t.vertices[2]].co - mesh.vertices[t.vertices[0]].co).length * .5
         for t in mesh.loop_triangles]
assert 50 <= len(mesh.loop_triangles) <= 100
assert min(areas) > 0.005
path = FBX / (NAME + ".fbx")
bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
    global_scale=1, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
    use_space_transform=True, bake_space_transform=False, axis_forward="-Y", axis_up="Z",
    mesh_smooth_type="FACE", colors_type="LINEAR", use_mesh_modifiers=True,
    use_triangles=True, add_leaf_bones=False, bake_anim=False)


def geometry_bounds(target):
    points = [target.matrix_world @ Vector(corner) for corner in target.bound_box]
    minimum = [min(p[axis] for p in points) for axis in range(3)]
    maximum = [max(p[axis] for p in points) for axis in range(3)]
    return minimum, maximum


low, high = geometry_bounds(obj)
record = {"lod": 0, "file": str(path.relative_to(ROOT)),
          "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
          "triangles": len(mesh.loop_triangles), "vertices": len(mesh.vertices),
          "bounds_min_cm": low, "bounds_max_cm": high,
          "size_cm": [b - a for a, b in zip(low, high)],
          "material_sections": 1, "uv_channels": 1,
          "minimum_triangle_area_cm2": min(areas)}

before = set(bpy.data.objects)
bpy.ops.object.select_all(action="DESELECT")
bpy.ops.import_scene.fbx(filepath=str(path), use_custom_normals=True, colors_type="LINEAR")
roundtrip = [entry for entry in bpy.data.objects if entry not in before]
imported = [entry for entry in roundtrip if entry.type == "MESH"]
assert len(imported) == 1
returned = imported[0]
returned.data.calc_loop_triangles()
returned_low, returned_high = geometry_bounds(returned)
assert len(returned.data.loop_triangles) == record["triangles"]
assert all(abs(a - b) < .002 for a, b in zip(low + high, returned_low + returned_high))
assert len(returned.data.materials) == 1 and len(returned.data.uv_layers) == 1
color = returned.data.color_attributes.active_color
assert color is not None and len(color.data)
color_ranges = [[min(c.color[i] for c in color.data), max(c.color[i] for c in color.data)] for i in range(4)]
assert color_ranges[0][0] == 0 and color_ranges[0][1] > .99
assert color_ranges[1][1] - color_ranges[1][0] > .8
record["fbx_roundtrip"] = {"verified": True, "triangles": len(returned.data.loop_triangles),
    "material_sections": 1, "uv_channels": 1, "vertex_colors": True,
    "vertex_color_ranges": color_ranges, "bounds_min_cm": returned_low, "bounds_max_cm": returned_high}
for entry in roundtrip:
    bpy.data.objects.remove(entry, do_unlink=True)

manifest = {"version": "cinder-frontier-foliage-v1", "blender_version": bpy.app.version_string,
    "license": "original Cinderline procedural geometry and material; no third-party assets",
    "unit": "centimeter", "forward_axis": "+X", "up_axis": "+Z",
    "origin": "bottom center; bounds_min_z_cm is exactly 0", "nanite": False,
    "collision": "none; decorative foliage only", "alpha_cards": False,
    "material": {"name": MATERIAL, "unreal_path": "/Game/Art/FrontierFoliage/Materials/" + MATERIAL},
    "vertex_color_channels": {"R": "per-blade normalized height; roots zero for wind",
                              "G": "blade tone; olive low, dry ochre high", "B": "unused", "A": "one"},
    "wind_contract": {"time_parameter": "WindTime", "default": 0,
                      "maximum_horizontal_displacement_cm": 3, "root_mask": "R squared"},
    "intended_use": "sparse cliff toes and broken shoulders; 15-50 cm footprint; never lawns",
    "assets": [{"name": NAME, "material_slot": MATERIAL, "lods": [record]}]}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "CinderFrontierFoliage.blend"))
if args.no_render:
    raise SystemExit(0)

# Three views of the same mesh demonstrate silhouette and density without
# pretending that this isolated asset preview is delivered runtime quality.
obj.location.x = -48
for index, (x, yaw, scale) in enumerate(((0, 1.5, 1), (48, -.7, .75))):
    instance = obj.copy()
    instance.data = obj.data
    instance.name = "PreviewOnly_Grass_" + str(index)
    scene.collection.objects.link(instance)
    instance.location.x = x
    instance.rotation_euler.z = yaw
    instance.scale = (scale,) * 3
bpy.ops.mesh.primitive_plane_add(size=1000, location=(0, 0, -.12))
ground = bpy.context.object
ground.name = "PreviewOnly_Ground"
ground_material = bpy.data.materials.new("PreviewOnly_Ground")
ground_material.use_nodes = True
ground_shader = next(n for n in ground_material.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
ground_shader.inputs["Base Color"].default_value = (.24, .22, .17, 1)
ground_shader.inputs["Roughness"].default_value = 1
ground.data.materials.append(ground_material)
sun_data = bpy.data.lights.new("PreviewOnly_Sun", type="SUN")
sun_data.energy = 2
sun_data.angle = .16
sun = bpy.data.objects.new("PreviewOnly_Sun", sun_data)
scene.collection.objects.link(sun)
sun.rotation_euler = (math.radians(26), math.radians(-32), math.radians(-28))
world = bpy.data.worlds.new("PreviewOnly_World")
world.use_nodes = True
background = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
background.inputs["Color"].default_value = (.40, .48, .60, 1)
background.inputs["Strength"].default_value = .5
scene.world = world
camera_data = bpy.data.cameras.new("PreviewOnly_Camera")
camera = bpy.data.objects.new("PreviewOnly_Camera", camera_data)
scene.collection.objects.link(camera)
camera.location = (70, -180, 123)
camera.rotation_euler = (Vector((0, 0, 15)) - camera.location).to_track_quat("-Z", "Y").to_euler()
camera_data.type = "ORTHO"
camera_data.ortho_scale = 162
camera_data.clip_end = 3000
scene.camera = camera
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 32
scene.render.threads_mode = "FIXED"
scene.render.threads = 2
scene.render.resolution_x = 1200
scene.render.resolution_y = 700
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(OUT / "contact-sheet.png")
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "CinderFrontierFoliagePreview.blend"))
bpy.ops.render.render(write_still=True)
print("CINDERLINE_FRONTIER_FOLIAGE_AUTHORED " + str(OUT / "manifest.json"))
