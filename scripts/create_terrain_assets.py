#!/usr/bin/env python3
"""Create Cinderline's original normalized basalt terrain pack in Blender.

Run: /Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/create_terrain_assets.py
"""
from pathlib import Path
import argparse
import bpy
import bmesh
import hashlib
import json
import math
import random
import sys
from mathutils import Vector


def parse_args():
    parser = argparse.ArgumentParser(description="Generate Cinderline basalt terrain assets")
    parser.add_argument("--no-render", action="store_true",
        help="Export and validate delivery assets without rebuilding the contact sheet PNG")
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return parser.parse_args(argv)


ARGS = parse_args()

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
    # Split crown: a narrow central fissure remains readable from the game camera.
    ("A", 74101, [(-18, 3, 19, 27, 92), (13, 7, 22, 24, 100),
        (26, -22, 15, 17, 51), (-31, -18, 12, 15, 36)]),
    # Descending fan: four independent plates create stepped depth without terraces.
    ("B", 74102, [(-13, 15, 25, 21, 100), (17, -9, 22, 20, 73),
        (-31, -17, 14, 17, 45), (30, 19, 12, 14, 34)]),
    # Blade pair: thin, offset profiles expose a deep diagonal crack.
    ("C", 74103, [(-23, 5, 17, 31, 100), (14, -4, 18, 26, 84),
        (-4, -31, 14, 12, 42), (29, 22, 12, 15, 35)]),
    # Broken crown with two lower shoulders and a detached rear plate.
    ("D", 74104, [(-6, 0, 25, 27, 100), (-30, 19, 15, 18, 61),
        (25, -19, 18, 18, 48), (31, 20, 11, 14, 33)]),
]


def rock(vertices, faces, face_tints, rng, x, y, rx, ry, height):
    count = rng.choice((8, 9, 10))
    phase = rng.uniform(-math.pi, math.pi)
    outline = [rng.uniform(0.82, 1.16) for _ in range(count)]
    # Six uneven rings describe long fault faces rather than stacked cylinders.
    # The last transition pulls sharply inward to form a broken, sloped crown.
    levels = [(0, 1.05), (.12, 1.0), (.35, .94), (.60, .85), (.80, .73), (.92, .49)]
    base = len(vertices)
    tilt_x, tilt_y = rng.uniform(-.18, .18), rng.uniform(-.18, .18)
    drift_x, drift_y = rng.uniform(-rx*.18, rx*.18), rng.uniform(-ry*.18, ry*.18)
    crown = [rng.uniform(-.13, .07) for _ in range(count)]
    # A localized shelf on only part of the perimeter reads as a sheared ledge.
    ledge_side = rng.randrange(count)
    for layer, (z_ratio, radius_ratio) in enumerate(levels):
        for side in range(count):
            angle = phase + side * math.tau / count
            shelf_distance = min((side-ledge_side) % count, (ledge_side-side) % count)
            shelf = (0.16 if shelf_distance == 0 else 0.08 if shelf_distance == 1 else 0)
            shelf *= math.sin(math.pi * z_ratio) if layer in (2, 3, 4) else 0
            rr = outline[side] * (radius_ratio+shelf) * rng.uniform(.95, 1.05)
            px = math.cos(angle) * rx * rr
            py = math.sin(angle) * ry * rr
            fracture = crown[side] * height * (z_ratio ** 2.4)
            z = 0 if layer == 0 else max(.3, height*z_ratio +
                (px*tilt_x + py*tilt_y)*z_ratio + fracture)
            # Ring drift follows the fault direction but wobbles enough to avoid
            # vertically aligned, extruded-looking edges.
            wobble = math.sin(side*2.31 + layer*1.73) * .025
            vertices.append((x+px+drift_x*z_ratio+rx*wobble,
                y+py+drift_y*z_ratio+ry*wobble, z))
    faces.append(tuple(base+i for i in reversed(range(count))))
    face_tints.append(rng.uniform(.72, .88))
    for layer in range(len(levels)-1):
        for side in range(count):
            a = base+layer*count+side
            b = base+layer*count+(side+1)%count
            faces.append((a, b, b+count, a+count))
            # Subtle intrinsic fault-face colors, not a baked light direction.
            face_tints.append(rng.uniform(.78, 1.07) * (.93 if layer in (1, 4) else 1))
    top = base+(len(levels)-1)*count
    center = len(vertices)
    ridge_angle = phase + rng.randrange(count)*math.tau/count
    ridge_offset = rng.uniform(.08, .24)
    vertices.append((x+drift_x+math.cos(ridge_angle)*rx*ridge_offset,
        y+drift_y+math.sin(ridge_angle)*ry*ridge_offset,
        height*rng.uniform(.86, 1.02)))
    for side in range(count):
        faces.append((top+side, top+(side+1)%count, center))
        face_tints.append(rng.uniform(.86, 1.13))


def scree(vertices, faces, face_tints, rng, x, y, rx, ry, height, slab=False):
    """Add one low angular wedge; scree never inherits the main cliff profile."""
    count = rng.choice((5, 6, 7))
    phase = rng.uniform(-math.pi, math.pi)
    base = len(vertices)
    lean_x, lean_y = rng.uniform(-rx*.22, rx*.22), rng.uniform(-ry*.22, ry*.22)
    top_scale = rng.uniform(.48, .72) if slab else rng.uniform(.32, .58)
    for side in range(count):
        angle = phase+side*math.tau/count
        radius = rng.uniform(.83, 1.14)
        vertices.append((x+math.cos(angle)*rx*radius,
            y+math.sin(angle)*ry*radius, 0))
    for side in range(count):
        angle = phase+side*math.tau/count
        top_z = height*(rng.uniform(.48, .66) if slab else rng.uniform(.60, .92))
        vertices.append((x+lean_x+math.cos(angle)*rx*top_scale,
            y+lean_y+math.sin(angle)*ry*top_scale, top_z))
    faces.append(tuple(base+i for i in reversed(range(count))))
    face_tints.append(rng.uniform(.70, .86))
    for side in range(count):
        faces.append((base+side, base+(side+1)%count,
            base+count+(side+1)%count, base+count+side))
        face_tints.append(rng.uniform(.76, 1.04))
    top_center = len(vertices)
    vertices.append((x+lean_x*1.2, y+lean_y*1.2,
        height*rng.uniform(.58, .76) if slab else height*rng.uniform(.78, 1.0)))
    for side in range(count):
        faces.append((base+count+side, base+count+(side+1)%count, top_center))
        face_tints.append(rng.uniform(.86, 1.08))


def create_variant(letter, seed, masses):
    rng = random.Random(seed)
    vertices, faces, tints = [], [], []
    for values in masses:
        rock(vertices, faces, tints, rng, *values)
    # Irregular low wedges and fallen plates stay within the normalized blocked
    # footprint. Deliberate angular gaps keep the main cluster cracks visible.
    scree_count = rng.choice((15, 16, 17))
    gap_angle = rng.uniform(-math.pi, math.pi)
    for i in range(scree_count):
        angle = i*math.tau/scree_count + rng.uniform(-.15, .15)
        if abs(math.atan2(math.sin(angle-gap_angle), math.cos(angle-gap_angle))) < .22:
            angle += .34
        radius = rng.uniform(34, 47)
        x, y = math.cos(angle)*radius, math.sin(angle)*rng.uniform(34, 47)
        scree(vertices, faces, tints, rng, x, y, rng.uniform(4.5, 10.5),
            rng.uniform(4.5, 10.0), rng.uniform(3.5, 13.0), slab=(i % 4 == 0))
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
    assert all(len(edge.link_faces) == 2 for edge in bm.edges), \
        f"{obj.name} contains an open or non-manifold component"
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
    "geometry_pass": "fractured crowns, asymmetric fault ledges, cluster fissures, low scree",
    "triangle_budget_per_asset": 2000, "material_slots": ["Basalt"], "assets": records}
(OUT/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
(EVIDENCE/"fbx-roundtrip-validation.json").write_text(json.dumps({"passed": True, "checks": roundtrip}, indent=2)+"\n")
for i, obj in enumerate(assets):
    obj.location = ((i%2)*160, (i//2)*160, 0)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"CinderBasaltTerrain.blend"))

if ARGS.no_render:
    print("CINDER_TERRAIN_READY_NO_RENDER "+str(OUT), flush=True)
    raise SystemExit(0)

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
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 1800
scene.render.resolution_y = 1800
scene.render.resolution_percentage = 100
scene.render.threads_mode = "FIXED"
scene.render.threads = 2
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(EVIDENCE/"basalt-terrain-contact-sheet.png")
scene.view_settings.view_transform = "AgX"
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"CinderBasaltTerrain_ContactSheet.blend"))
bpy.ops.render.render(write_still=True)
print("CINDER_TERRAIN_READY "+str(OUT), flush=True)
