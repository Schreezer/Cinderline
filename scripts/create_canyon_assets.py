#!/usr/bin/env python3
"""Generate Cinderline's original fractured frontier rock kit in Blender.

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
import bmesh
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
material.diffuse_color = (0.27, 0.29, 0.26, 1.0)
material.use_nodes = True
shader = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
shader.inputs["Base Color"].default_value = (0.27, 0.29, 0.26, 1.0)
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


def fracture_chunk(center, scale, yaw, seed, lod):
    """A battered jointed slab with a pitched crown and narrow chipped edges."""
    rng = random.Random(seed)
    outline = [(1,.57),(.63,1),(-.63,1),(-1,.53),
               (-1,-.59),(-.61,-1),(.67,-1),(1,-.54)]
    # The same corners and crown survive every LOD. Only bevels and one broad
    # shoulder fold disappear; lower detail never turns the rock into a cylinder.
    jitter = [(rng.uniform(.88,1.08),rng.uniform(.9,1.08)) for _ in outline]
    pitch_x, pitch_y = rng.uniform(-.22,.22), rng.uniform(-.2,.2)
    lean_x, lean_y = rng.uniform(-.14,.14),rng.uniform(-.12,.12)
    levels = [(0,1.02),(.12,1.0),(.64,.94),(1,.78)] if lod == 0 else [(0,1.02),(.64,.94),(1,.78)]
    vertices=[]; faces=[]
    for level,(z,radius) in enumerate(levels):
        for k,((x,y),(jx,jy)) in enumerate(zip(outline,jitter)):
            px=x*jx*radius+lean_x*z
            py=y*jy*radius+lean_y*z
            # Tilted crowns, never a horizontal fan/lid. Mid-height cuts are
            # broad diagonal planes rather than repeated sediment rings.
            pz=z*(1+pitch_x*x+pitch_y*y)
            vertices.append((px*scale[0],py*scale[1],pz*scale[2]))
    for ring in range(len(levels)-1):
        for k in range(8):
            n=(k+1)%8
            faces.append((ring*8+k,ring*8+n,(ring+1)*8+n,(ring+1)*8+k))
    faces.append(tuple(reversed(range(8))))
    top=(len(levels)-1)*8
    ridge=len(vertices)
    vertices.extend([(scale[0]*(-.23+lean_x),scale[1]*(.12+lean_y),scale[2]*1.12),
                     (scale[0]*(.31+lean_x),scale[1]*(-.09+lean_y),scale[2]*1.06)])
    faces.extend([(top,top+1,ridge+1),(top+1,top+2,ridge,ridge+1),
                  (top+2,top+3,ridge),(top+3,top+4,ridge),
                  (top+4,top+5,ridge),(top+5,top+6,ridge+1,ridge),
                  (top+6,top+7,ridge+1),(top+7,top,ridge+1)])
    bm=bmesh.new()
    for point in vertices: bm.verts.new(point)
    bm.verts.ensure_lookup_table()
    for face in faces: bm.faces.new([bm.verts[i] for i in face])
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    if lod < 2:
        # Small bevel facets catch a thin natural edge light. They are geometry,
        # not a pale rim painted around every silhouette.
        bevel = min(scale)*(.055 if lod == 0 else .038)
        if scale[2] < 16.0:
            bevel *= .55  # shallow foundations need smaller chips than upright slabs
        bmesh.ops.bevel(bm,geom=list(bm.edges),offset=bevel,
                        segments=1,affect='EDGES',clamp_overlap=True)
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bm.verts.index_update()
    c,s=math.cos(yaw),math.sin(yaw)
    result_vertices=[(center[0]+v.co.x*c-v.co.y*s,
                      center[1]+v.co.x*s+v.co.y*c,center[2]+v.co.z) for v in bm.verts]
    result_faces=[tuple(v.index for v in face.verts) for face in bm.faces]
    bm.free()
    return result_vertices,result_faces


def rock_mesh(letter, variant, lod):
    other_xy,height,lean,phase=ROCK_SPECS[variant][1:]
    rng=random.Random(8128+variant*311)
    # Three interlocking primary slabs establish the fracture direction. Two
    # fallen shoulders make the base wider and give each piece an asymmetric end.
    slabs=[((-.19,.08,0),(.51,.48,.86),-.08),
           ((.39,.17,.015),(.36,.41,.69),.14),
           ((-.38,-.43,0),(.40,.29,.49),-.24),
           ((.29,-.46,0),(.38,.28,.32),.34),
           ((-.64,.37,0),(.25,.31,.39),-.36)]
    vertices=[];faces=[]
    for index,(center,scale,yaw) in enumerate(slabs):
        skew=1+rng.uniform(-.13,.13)
        # Variant-specific offsets keep the six kits from sharing one outline.
        cx=(center[0]+rng.uniform(-.07,.07))*50
        cy=(center[1]+rng.uniform(-.06,.06))*other_xy*.5
        cz=center[2]*height
        sx=scale[0]*50*skew;sy=scale[1]*other_xy*.5;sz=scale[2]*height*(1+rng.uniform(-.12,.12))
        extra=fracture_chunk((cx,cy,cz),(sx,sy,sz),yaw+lean*2,variant*97+index*619+32,lod)
        append_geometry(vertices,faces,*extra)
    vertices=normalize_bounds(vertices,(100.0,other_xy,height))
    name=f"SM_CinderCanyon_Rock_{letter}"+("" if lod == 0 else f"_LOD{lod}")
    return make_object(name,vertices,faces,variant)


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
            # One coherent tone per sculpted face, with broad nonperiodic variation.
            center = polygon.center
            strata = max(.28,min(.85,.56 + .13*math.sin(center.x*.047+variant*1.31)
                                   + .09*math.sin(center.y*.031+center.z*.016+variant*.43)))
            rust = max(0.0,math.sin(center.x*.028-center.y*.041+variant*.91)*.30)
            cavity = .80 + .19*max(0.0,normal.z)
            colors.data[loop_index].color = (strata, rust, cavity, 1.0)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.location = (0, 0, 0)
    return obj


def skirt_mesh(lod):
    # A broad fractured foot closes the blocked footprint under the taller slabs.
    # It stays low and has no smooth concentric rings or continuous bright lip.
    vertices=[];faces=[]
    for i,(x,y,sx,sy,z) in enumerate([(-.47,0,.55,.88,11),(.39,.02,.59,.87,13),
                                     (-.1,-.65,.73,.30,7),(.15,.65,.66,.30,9)]):
        append_geometry(vertices,faces,*fracture_chunk((x*50,y*45,0),
            (sx*50,sy*45,z),(-.07 if i%2 else .09),814+i*313,lod))
    vertices=normalize_bounds(vertices,(100.,90.,12.))
    name="SM_CinderCanyon_CliffMass"+("" if lod == 0 else f"_LOD{lod}")
    return make_object(name,vertices,faces,7)


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
        "vertex_color": "R broad face tone, G sparse weathering, B broad cavity response",
    }


def verify_fbx_roundtrip(record):
    """Reimport one exported FBX and prove its portable geometry contract."""
    path = ROOT / record["file"]
    before = set(bpy.data.objects)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.import_scene.fbx(filepath=str(path), use_custom_normals=True, colors_type="LINEAR")
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
    color = obj.data.color_attributes.active_color
    assert color is not None and len(color.data), path.name + " lost vertex colors on FBX round trip"
    color_ranges = [[min(item.color[channel] for item in color.data),
                     max(item.color[channel] for item in color.data)] for channel in range(3)]
    assert color_ranges[0][1] - color_ranges[0][0] > 0.01, path.name + " lost authored face variation"
    record["fbx_roundtrip"] = {
        "vertex_color_ranges": color_ranges,
        "vertex_colors": True,
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
    "base_colors_linear": {"shadow": [0.16, 0.17, 0.15], "sandstone": [0.30, 0.29, 0.23],
                           "iron": [0.24, 0.22, 0.17]},
    "vertex_color_channels": {"R": "broad face tone", "G": "sparse weathering", "B": "ambient occlusion"},
    "roughness": [0.82, 0.95],
    "metallic": 0.0,
    "texture_coordinates": "authored dominant-axis UV0 at 40 cm per tile; material color uses broad face tone and sparse weathering masks",
}
(OUT / "M_CinderCanyonRock.json").write_text(json.dumps(material_contract, indent=2) + "\n")
manifest = {
    "generator": "scripts/create_canyon_assets.py",
    "blender_version": bpy.app.version_string,
    "license": "original Cinderline fractured slab geometry and material",
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
