#!/usr/bin/env python3
"""Create Cinderline's original normalized basalt terrain pack in Blender.

Run: /Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/create_terrain_assets.py
"""
from pathlib import Path
import bpy
import bmesh
import hashlib
import json
import math
import random
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "RawAssets/Terrain"
EVIDENCE = ROOT / "artifacts/terrain"
for path in (OUT / "FBX", EVIDENCE):
    path.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
bpy.context.preferences.filepaths.save_version = 0
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 0.01

mat = bpy.data.materials.new("Basalt")
mat.diffuse_color = (0.12, 0.16, 0.17, 1)
mat.use_nodes = True
shader = mat.node_tree.nodes.get("Principled BSDF")
shader.inputs["Roughness"].default_value = 0.91
shader.inputs["Metallic"].default_value = 0.0
color = mat.node_tree.nodes.new("ShaderNodeVertexColor")
color.layer_name = "RockTint"
mat.node_tree.links.new(color.outputs["Color"], shader.inputs["Base Color"])

# Positions, horizontal radii and heights define an authored family of faulted
# outcrops. Seeds only add bounded fractures, stratification and rubble variation.
SPECS = [
    ("A", 74101, [(-19, 1, 28, 31, 93), (13, 8, 27, 34, 100), (26, -22, 18, 22, 49)]),
    ("B", 74102, [(-14, 14, 32, 27, 100), (17, -11, 30, 25, 77), (-29, -19, 18, 20, 46)]),
    ("C", 74103, [(-23, 4, 24, 39, 100), (22, -6, 22, 32, 88), (-10, -32, 18, 14, 42)]),
    ("D", 74104, [(-4, -1, 35, 33, 100), (-30, 20, 20, 23, 64), (29, -20, 23, 22, 45)]),
]


def rock(vertices, faces, face_tints, rng, x, y, rx, ry, height, rubble=False):
    count = 7 if rubble else 9
    phase = rng.uniform(-math.pi, math.pi)
    outline = [rng.uniform(0.84, 1.13) for _ in range(count)]
    levels = [(0, 1.04), (.19, 1.0), (.72, .84), (1, .60)] if rubble else [
        (0, 1.04), (.08, 1.01), (.15, .91), (.35, .94), (.40, .83),
        (.62, .87), (.67, .76), (.84, .79), (1, .65)]
    base = len(vertices)
    # A tilted top, offset rings and alternating shelves break regular cylinders.
    tilt_x, tilt_y = rng.uniform(-.11, .11), rng.uniform(-.11, .11)
    drift_x, drift_y = rng.uniform(-rx*.12, rx*.12), rng.uniform(-ry*.12, ry*.12)
    for layer, (z_ratio, radius_ratio) in enumerate(levels):
        for side in range(count):
            angle = phase + side * math.tau / count
            rr = outline[side] * radius_ratio * rng.uniform(.965, 1.035)
            px = math.cos(angle) * rx * rr
            py = math.sin(angle) * ry * rr
            z = 0 if layer == 0 else max(.3, height*z_ratio + (px*tilt_x + py*tilt_y)*z_ratio)
            vertices.append((x+px+drift_x*z_ratio, y+py+drift_y*z_ratio, z))
    faces.append(tuple(base+i for i in reversed(range(count))))
    face_tints.append(rng.uniform(.72, .88))
    for layer in range(len(levels)-1):
        for side in range(count):
            a = base+layer*count+side
            b = base+layer*count+(side+1)%count
            faces.append((a, b, b+count, a+count))
            # Subtle intrinsic stratum colors, not a baked light direction.
            face_tints.append(rng.uniform(.80, 1.06) * (.91 if layer in (1, 4, 6) else 1))
    top = base+(len(levels)-1)*count
    center = len(vertices)
    vertices.append((x+drift_x, y+drift_y, height*rng.uniform(.98, 1.035)))
    for side in range(count):
        faces.append((top+side, top+(side+1)%count, center))
        face_tints.append(rng.uniform(.93, 1.12))


def create_variant(letter, seed, masses):
    rng = random.Random(seed)
    vertices, faces, tints = [], [], []
    for values in masses:
        rock(vertices, faces, tints, rng, *values)
    # Low peripheral chips and slabs keep an irregular natural skirt. Every
    # vertex is normalized inside the declared footprint after assembly.
    for i in range(12):
        angle = i*math.tau/12 + rng.uniform(-.11, .11)
        x, y = math.cos(angle)*rng.uniform(36, 46), math.sin(angle)*rng.uniform(35, 46)
        rock(vertices, faces, tints, rng, x, y, rng.uniform(6, 12), rng.uniform(6, 12), rng.uniform(7, 22), True)
    mesh = bpy.data.meshes.new("Basalt fractures "+letter)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("SM_BasaltCliff_"+letter, mesh)
    scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    lo = Vector(tuple(min(v.co[i] for v in mesh.vertices) for i in range(3)))
    hi = Vector(tuple(max(v.co[i] for v in mesh.vertices) for i in range(3)))
    for vertex in mesh.vertices:
        vertex.co.x = (vertex.co.x-lo.x)/(hi.x-lo.x)*100-50
        vertex.co.y = (vertex.co.y-lo.y)/(hi.y-lo.y)*100-50
        vertex.co.z = (vertex.co.z-lo.z)/(hi.z-lo.z)*100
    # Corner colors travel with the FBX. UE may use them as a low-cost tint mask.
    tint = mesh.color_attributes.new(name="RockTint", type="FLOAT_COLOR", domain="CORNER")
    for polygon, value in zip(mesh.polygons, tints):
        polygon.use_smooth = False
        for corner in polygon.loop_indices:
            tint.data[corner].color = (.115*value, .151*value, .158*value, 1)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(62), island_margin=.025)
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()
    mesh.calc_loop_triangles()
    areas = [((mesh.vertices[t.vertices[1]].co-mesh.vertices[t.vertices[0]].co).cross(
        mesh.vertices[t.vertices[2]].co-mesh.vertices[t.vertices[0]].co)).length*.5 for t in mesh.loop_triangles]
    assert len(areas) <= 2000, f"{obj.name} exceeds terrain triangle budget"
    assert min(areas) > .00005, f"{obj.name} has degenerate triangles"
    assert len(mesh.uv_layers) == 1 and all(math.isfinite(c) for uv in mesh.uv_layers[0].data for c in uv.uv)
    assert all(abs(obj.dimensions[i]-100) < .001 for i in range(3))
    path = OUT/"FBX"/(obj.name+".fbx")
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
        global_scale=1, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        use_space_transform=True, bake_space_transform=False, axis_forward="-Y", axis_up="Z",
        mesh_smooth_type="FACE", use_mesh_modifiers=True, use_triangles=True,
        add_leaf_bones=False, bake_anim=False, path_mode="AUTO")
    return obj, {"name": obj.name, "seed": seed, "dimensions_cm": [100,100,100],
        "bounds_min_cm": [-50,-50,0], "bounds_max_cm": [50,50,100],
        "vertices": len(mesh.vertices), "triangles": len(areas), "minimum_triangle_area_cm2": min(areas),
        "material_slots": ["Basalt"], "uv_channels": 1, "vertex_color": "RockTint",
        "fbx": str(path.relative_to(ROOT)), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


assets, records = [], []
for spec in SPECS:
    obj, record = create_variant(*spec)
    assets.append(obj)
    records.append(record)
    print("CINDER_TERRAIN "+json.dumps(record), flush=True)

roundtrip = []
for original, record in zip(assets, records):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.import_scene.fbx(filepath=str(ROOT/record["fbx"]), use_custom_normals=True)
    imported = [obj for obj in bpy.context.selected_objects if obj.type == "MESH"]
    assert len(imported) == 1, f"{record['name']} did not roundtrip as one mesh"
    obj = imported[0]
    coords = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    lo = Vector(tuple(min(v[i] for v in coords) for i in range(3)))
    hi = Vector(tuple(max(v[i] for v in coords) for i in range(3)))
    assert max(abs((hi-lo)[i]-100) for i in range(3)) < .01
    assert max(abs(lo[i]-(-50,-50,0)[i]) for i in range(3)) < .01
    assert [m.name.split(".")[0] for m in obj.data.materials] == ["Basalt"]
    assert len(obj.data.uv_layers) == 1
    obj.data.calc_loop_triangles()
    assert len(obj.data.loop_triangles) == record["triangles"]
    assert obj.data.color_attributes.get("RockTint") is not None
    roundtrip.append({"name": record["name"], "passed": True, "dimensions_cm": [round(v,5) for v in hi-lo],
        "minimum_cm": [round(v,5) for v in lo], "triangles": len(obj.data.loop_triangles), "uv_channels": 1})
    bpy.data.objects.remove(obj, do_unlink=True)

manifest = {"generator": "scripts/create_terrain_assets.py", "blender_version": bpy.app.version_string,
    "original_geometry": True, "unit": "centimeter", "origin": "bottom center of bounds",
    "forward_axis": "+X", "up_axis": "+Z", "collision": "none; portable simulation owns obstacle rectangles",
    "material_slots": ["Basalt"], "assets": records}
(OUT/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
(EVIDENCE/"fbx-roundtrip-validation.json").write_text(json.dumps({"passed": True, "checks": roundtrip}, indent=2)+"\n")
for i, obj in enumerate(assets):
    obj.location = ((i%2)*160, (i//2)*160, 0)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"CinderBasaltTerrain.blend"))

# Actual Blender contact sheet. Presentation objects never enter delivery FBXs.
for obj in assets:
    obj.hide_render = True
camera_position = Vector((6,-9,8)).normalized()*1000
rotation = (-camera_position).to_track_quat("-Z", "Y")
right, up, toward = [rotation @ Vector(axis) for axis in ((1,0,0),(0,1,0),(0,0,1))]
camera_data = bpy.data.cameras.new("Terrain contact camera")
camera = bpy.data.objects.new("Terrain contact camera", camera_data)
scene.collection.objects.link(camera)
camera.location = camera_position
camera.rotation_euler = rotation.to_euler()
camera_data.type = "ORTHO"
camera_data.ortho_scale = 680
camera_data.clip_end = 2500
scene.camera = camera
for i, original in enumerate(assets):
    copy = original.copy()
    copy.data = original.data.copy()
    copy.hide_render = False
    copy.name = "Preview "+original.name
    scene.collection.objects.link(copy)
    center = right*((i%2-.5)*268)+up*((.5-i//2)*270-12)
    copy.location = center-up*18
    copy.scale = (1.45,)*3

text_mat = bpy.data.materials.new("Preview lettering")
text_mat.diffuse_color = (.62,.76,.78,1)
text_mat.use_nodes = True
text_shader = text_mat.node_tree.nodes.get("Principled BSDF")
text_shader.inputs["Base Color"].default_value = (.62,.76,.78,1)
text_shader.inputs["Emission Color"].default_value = (.62,.76,.78,1)
text_shader.inputs["Emission Strength"].default_value = .7
def label(text, position, size):
    curve = bpy.data.curves.new(text, "FONT")
    curve.body = text
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    curve.size = size
    obj = bpy.data.objects.new(text, curve)
    scene.collection.objects.link(obj)
    obj.location = position
    obj.rotation_euler = rotation.to_euler()
    curve.materials.append(text_mat)
for i, record in enumerate(records):
    center = right*((i%2-.5)*268)+up*((.5-i//2)*270-12)
    label(record["name"].replace("SM_", "").replace("_", " ").upper(), center-up*92+toward*125, 10)
    label(f"{record['triangles']:,} triangles  /  100 x 100 x 100 cm", center-up*108+toward*125, 6.1)
label("CINDERLINE / BASALT TERRAIN", up*303+toward*140, 15)
label("ORIGINAL FRACTURED OUTCROPS / ONE MATERIAL / NORMALIZED FOOTPRINT", up*283+toward*140, 5.6)
world = scene.world or bpy.data.worlds.new("Terrain studio")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (.07,.09,.11,1)
world.node_tree.nodes["Background"].inputs[1].default_value = .45
for name, position, energy, light_color, size in [
    ("Cool key", (350,-450,700), 7000000, (.76,.87,1), 500),
    ("Warm rim", (-450,300,450), 4500000, (1,.75,.52), 350),
    ("Soft fill", (400,450,250), 1600000, (.55,.80,1), 500)]:
    data = bpy.data.lights.new(name, "AREA")
    data.energy, data.color, data.shape, data.size = energy, light_color, "DISK", size
    light = bpy.data.objects.new(name, data)
    scene.collection.objects.link(light)
    light.location = position
    light.rotation_euler = (-Vector(position)).to_track_quat("-Z", "Y").to_euler()
scene.render.engine = "CYCLES"
scene.cycles.samples = 40
scene.cycles.use_denoising = True
scene.cycles.device = "CPU"
scene.render.resolution_x = 1800
scene.render.resolution_y = 1800
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(EVIDENCE/"basalt-terrain-contact-sheet.png")
scene.view_settings.view_transform = "AgX"
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"CinderBasaltTerrain_ContactSheet.blend"))
bpy.ops.render.render(write_still=True)
print("CINDER_TERRAIN_READY "+str(OUT), flush=True)
