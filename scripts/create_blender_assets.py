#!/usr/bin/env python3
"""Build Cinderline's original static mesh pack in Blender 5.2.

Run: Blender --background --python scripts/create_blender_assets.py
Geometry uses centimeters, +X forward and +Z up. All final transforms are baked.
"""
from pathlib import Path
import bpy
import bmesh
import json
import hashlib
import argparse
import math
import random
import sys
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "RawAssets/Models"
EVIDENCE = ROOT / "artifacts/models"
for directory in (OUT / "FBX", EVIDENCE):
    directory.mkdir(parents=True, exist_ok=True)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for datablock in list(bpy.data.materials):
    bpy.data.materials.remove(datablock)
scene = bpy.context.scene
bpy.context.preferences.filepaths.save_version = 0
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 0.01

SLOTS = ["HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow"]

def material(name, color, metallic, roughness, emission=0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Emission Color"].default_value = (*color, 1)
    shader.inputs["Emission Strength"].default_value = emission
    return mat

MATS = [
    material("HullDark", (0.065, 0.098, 0.13), .65, .36),
    material("HullLight", (.29, .37, .40), .55, .30),
    material("Metal", (.14, .19, .22), .83, .26),
    material("TeamPanel", (.018, .53, .43), .38, .30),
    material("CoreGlow", (.10, .96, .72), .1, .22, 2.2),
]
PARTS = []

def finish(obj, slot, bevel=0, smooth=False):
    obj.data.materials.append(MATS[slot])
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = obj.modifiers.new("Armor edge chamfer", "BEVEL")
        mod.width = bevel
        mod.segments = 1
        mod.affect = "EDGES"
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for polygon in obj.data.polygons:
        polygon.use_smooth = smooth
    if bevel or smooth:
        normals = obj.modifiers.new("Weighted surface normals", "WEIGHTED_NORMAL")
        normals.keep_sharp = True
        normals.weight = 50
        bpy.ops.object.modifier_apply(modifier=normals.name)
    PARTS.append(obj)
    return obj

def box(pos, size, slot=0, bevel=.035, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=pos, rotation=rot)
    obj = bpy.context.object
    obj.scale = size
    return finish(obj, slot, bevel)

def cylinder(pos, radius, depth, slot=2, vertices=12, rot=(0, 0, 0), bevel=.015):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=pos, rotation=rot)
    return finish(bpy.context.object, slot, bevel, True)

def beam(a, b, radius, slot=2, vertices=8):
    delta = Vector(b) - Vector(a)
    obj = cylinder((Vector(a) + Vector(b)) / 2, radius, delta.length, slot, vertices, bevel=.006)
    obj.rotation_euler = delta.to_track_quat("Z", "Y").to_euler()
    return obj

def hull(points, bottom, top, slot=0, bevel=.025, taper=.85):
    n = len(points)
    verts = [(x, y, bottom) for x, y in points] + [(x * taper, y * taper, top) for x, y in points]
    faces = [tuple(reversed(range(n))), tuple(range(n, 2*n))]
    faces += [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
    mesh = bpy.data.meshes.new("Machined hull")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("Armor shell", mesh)
    bpy.context.collection.objects.link(obj)
    return finish(obj, slot, bevel)

def diamond(pos, size, slot=0, bevel=.025):
    x, y, z = pos
    sx, sy, sz = size
    obj = hull([(sx/2, 0), (sx*.22, sy/2), (-sx*.38, sy/2), (-sx/2, 0), (-sx*.38, -sy/2), (sx*.22, -sy/2)], -sz/2, sz/2, slot, bevel)
    obj.location = (x,y,z)
    return obj

def torus(pos, major, minor, slot=2, rot=(0,0,0), segments=20):
    bpy.ops.mesh.primitive_torus_add(major_segments=segments, minor_segments=6, location=pos, rotation=rot, major_radius=major, minor_radius=minor)
    return finish(bpy.context.object, slot, 0, True)

def core(pos, radius=.17, height=.3):
    cylinder(pos, radius, height, 4, 8, bevel=.01)
    for z in (pos[2]-height*.55, pos[2]+height*.55):
        cylinder((pos[0],pos[1],z), radius*1.14, .06, 2, 8)

def vents(pos, count=4, length=.32, spacing=.10, axis="Y"):
    for i in range(count):
        shift = (i-(count-1)/2)*spacing
        p = (pos[0]+(shift if axis=="X" else 0), pos[1]+(shift if axis=="Y" else 0), pos[2])
        box(p, ((.035,length,.025) if axis=="X" else (length,.035,.025)), 2, .007)

def bolts(points, radius=.035):
    for p in points:
        cylinder(p, radius, .035, 2, 6, bevel=.005)

def feet(scale=1, four=True):
    for y in (-.38,.38):
        for x in ((-.32,.32) if four else (0,)):
            box((x*scale,y*scale,.12), (.34*scale,.24*scale,.22), 2, .035)
            beam((x*.6*scale,y*.63*scale,.56),(x*scale,y*scale,.21),.07,2)
            box((x*scale,y*scale,.28),(.20,.20,.10),3,.02)

def track(y, length=1.8, width=.39, z=.27):
    diamond((0,y,z),(length,width,.45),2,.05)
    for x in (-length*.32,0,length*.32):
        cylinder((x,y+(width*.49 if y>0 else -width*.49),z),.14,.035,0,10,(math.pi/2,0,0))
    for i in range(8):
        box(((i/7-.5)*length*.84,y,z+.235),(.10,width*.9,.05),2,.007)

def barrel(a, length=.8, radius=.085, pair=False):
    for off in ((-.14,.14) if pair else (0,)):
        x,y,z=a
        beam((x,y+off,z),(x+length,y+off,z),radius,2)
        box((x+length-.03,y+off,z),(.17,radius*2.4,radius*2.4),1,.02)
        box((x+length+.06,y+off,z),(.012,radius*1.2,radius*1.2),0,.0)

def drudge():
    feet(.82)
    diamond((-.02,0,.55),(1.18,.80,.26),0)
    for y in (-.29,.29):
        diamond((-.04,y,.75),(1.0,.26,.23),1)
        box((-.08,y,.88),(.41,.17,.045),3,.01)
    core((-.16,0,.77),.18,.30)
    box((.36,0,.66),(.30,.24,.16),2)
    for y in (-.14,.14):
        beam((.42,y,.69),(.69,y*1.6,.41),.045)
        box((.69,y*1.6,.35),(.12,.10,.22),1,.01)
    vents((-.41,0,.70),3,.19,.08,"X")

def ember():
    feet(1,False)
    cylinder((-.04,0,.55),.23,.24,2)
    diamond((0,0,.95),(.90,.74,.65),0)
    for y in (-.30,.30):
        diamond((0,y,1.13),(.76,.28,.24),1)
        box((.13,y,1.25),(.29,.19,.06),3,.012)
    box((.15,0,1.20),(.24,.24,.17),0)
    box((.282,0,1.22),(.025,.21,.055),4,.005)
    core((-.24,0,.95),.14,.27)
    barrel((.22,-.28,.91),.57,.065)
    vents((-.26,.28,1.26),3,.16,.07,"X")

def needle():
    feet(.95,False)
    diamond((-.14,0,.86),(.74,.73,.63),0)
    for y in (-.26,.26):
        diamond((-.11,y,1.18),(.76,.25,.22),1)
        box((-.28,y,1.32),(.26,.17,.05),3,.01)
        barrel((.13,y,1.06),.93,.07)
        beam((.12,y,1.01),(.76,y,.95),.055,3)
    core((-.13,0,1.15),.14,.34)
    box((-.45,0,1.05),(.17,.47,.37),2)
    vents((-.49,0,1.26),4,.12,.095)

def skim():
    diamond((0,0,.31),(2.0,.42,.35),0)
    diamond((.10,0,.55),(1.78,.31,.16),1)
    diamond((.36,0,.65),(.72,.15,.075),3,.01)
    for y in (-.36,.36):
        diamond((-.40,y,.28),(.94,.29,.35),0)
        box((-.49,y,.49),(.42,.17,.035),3,.01)
        # Keep the exhaust-cap chamfer below half its thickness. A .015 bevel
        # on this .03 cylinder collapses 32 triangles per cap in FBX import.
        cylinder((-.88,y,.30),.105,.03,4,8,(0,math.pi/2,0),bevel=.006)
        beam((-.38,0,.32),(-.4,y,.33),.09)
    core((-.4,0,.57),.12,.20)
    barrel((.55,0,.38),.51,.045)

def anvil():
    for y in (-.65,.65):
        track(y,1.8,.44)
        diamond((-.08,y,.67),(1.88,.49,.43),0,.045)
        diamond((.04,y,.92),(1.39,.36,.12),1)
        box((.10,y,.999),(.51,.27,.04),3,.015)
        vents((-.65,y,.905),4,.24,.08,"X")
    diamond((-.12,0,.53),(1.30,.77,.44),0)
    cylinder((-.11,0,.88),.37,.22,2,12)
    diamond((-.02,0,1.08),(.77,.68,.33),1)
    core((-.27,0,1.25),.15,.17)
    barrel((.25,0,1.12),.74,.115)
    bolts([(-.75,y,.94) for y in (-.66,.66)])

def cinderthrow():
    for y in (-.60,.60):
        track(y,1.50,.36)
        diamond((-.1,y,.59),(1.35,.39,.30),1)
        box((-.15,y,.77),(.48,.24,.05),3,.01)
    diamond((-.2,0,.54),(1.25,.91,.33),0)
    cylinder((-.3,0,.82),.37,.30,2)
    box((-.25,0,1.11),(.62,.47,.34),0)
    for y in (-.33,.33):
        cylinder((-.25,y,1.10),.21,.13,1,10,(math.pi/2,0,0))
    beam((-.36,0,1.13),(1.28,0,1.49),.13,2,10)
    beam((-.10,0,1.20),(.57,0,1.35),.21,1,8)
    box((1.27,0,1.49),(.25,.35,.29),1,.025,rot=(0,-.22,0))
    box((1.40,0,1.52),(.015,.22,.16),0,.002,rot=(0,-.22,0))
    core((-.71,0,.98),.14,.26)
    for y in (-.49,.49):
        beam((-.66,y,.51),(-.98,y*1.6,.08),.065)
        box((-.98,y*1.6,.07),(.29,.21,.14),2)

def mend():
    for angle in (0,2*math.pi/3,4*math.pi/3):
        x,y=math.cos(angle)*.65,math.sin(angle)*.65
        beam((0,0,.35),(x,y,.14),.09)
        diamond((x,y,.15),(.4,.28,.25),0)
        box((x,y,.30),(.20,.15,.04),3,.01)
    cylinder((0,0,.38),.33,.22,2)
    core((0,0,.73),.22,.55)
    torus((0,0,.84),.59,.085,1,segments=24)
    for angle in (0,math.pi/2,math.pi,3*math.pi/2):
        x,y=math.cos(angle)*.59,math.sin(angle)*.59
        beam((x,y,.45),(x,y,1.02),.062,2)
        diamond((x,y,1.08),(.28,.22,.20),3,.015)
        cylinder((x,y,1.22),.07,.05,4,8)
    torus((0,0,.54),.48,.045,3,segments=20)

def veil():
    diamond((.05,0,.27),(1.75,.38,.41),0)
    diamond((.35,0,.52),(.83,.25,.16),1)
    diamond((.40,0,.63),(.38,.15,.04),3,.008)
    for side in (-1,1):
        wing = hull([(.52,.16*side),(-.68,1.02*side),(-.93,.78*side),(-.47,.16*side)],.22,.35,1,.025,.96)
        plate = hull([(.11,.30*side),(-.60,.84*side),(-.72,.72*side),(-.22,.30*side)],.36,.39,3,.008,.99)
        diamond((-.55,.55*side,.30),(.95,.25,.42),0)
        cylinder((-.995,.55*side,.31),.085,.035,4,8,(0,math.pi/2,0))
        barrel((.14,.30*side,.22),.37,.045)
        box((-.72,.43*side,.57),(.25,.07,.30),2,.01,rot=(.2*side,0,0))
    core((-.27,0,.48),.13,.19)

def foundation(radius, height=.18):
    cylinder((0,0,height/2),radius,height,2,8,bevel=.045)
    cylinder((0,0,height+.025),radius*.94,.06,0,8,bevel=.02)

def anchor():
    foundation(1.7,.2)
    for y in (-.85,.85):
        diamond((-.06,y,.60),(2.75,1.02,.89),0,.06)
        diamond((-.10,y,1.11),(2.35,.83,.24),1,.035)
        box((.40,y,1.27),(.65,.50,.065),3,.02)
        vents((-.64,y,1.255),5,.55,.13,"X")
        bolts([(.86,y-.23,1.245),(.86,y+.23,1.245)],.05)
    cylinder((-.35,0,.79),.62,1.22,0,8,(0,0,0),.04)
    core((-.35,0,1.33),.32,.74)
    for y in (-.44,.44):
        box((-.34,y,1.37),(.59,.12,.97),2,.02)
    torus((-.35,0,1.66),.47,.075,1,segments=16)
    box((.8,0,.35),(1.12,.65,.20),2,.03,rot=(0,.18,0))
    for x in (.40,.61,.82,1.03):
        box((x,0,.49-x*.10),(.06,.54,.025),3,.005)
    box((-.85,0,1.58),(.41,.40,.36),1)
    beam((-.96,.15,1.7),(-.96,.15,2.20),.036)
    box((-.96,.15,2.13),(.12,.24,.06),4,.01)

def siphon():
    foundation(1.43,.20)
    diamond((0,0,.41),(2.25,1.89,.48),0,.05)
    for y in (-.67,.67):
        cylinder((-.25,y,.89),.39,1.01,1,10,bevel=.04)
        cylinder((-.25,y,1.38),.29,.10,2,10)
        torus((-.25,y,1.15),.395,.055,3,segments=16)
        box((.13,y,.77),(.055,.24,.32),3,.01)
    core((-.33,0,1.09),.22,.80)
    diamond((.64,0,.99),(.91,.87,.91),0)
    diamond((.63,0,1.48),(1.1,1.03,.16),1)
    box((.68,0,1.57),(.68,.61,.035),2,.01)
    for y in (-.28,.28):
        beam((.55,y,1.20),(.15,y,1.32),.10)
    for x in (-.78,-.52,-.26):
        box((x,0,.52),(.09,.54,.045),3,.01)

def kiln():
    foundation(1.55,.17)
    for y in (-.65,.65):
        diamond((-.12,y,.69),(2.27,.77,1.02),0,.05)
        diamond((-.2,y,1.23),(2.15,.76,.24),1,.025)
        box((.46,y,1.38),(.62,.48,.06),3,.02)
        vents((-.41,y,1.38),5,.45,.12,"X")
        for x in (-.76,-.40):
            cylinder((x,y,1.55),.12,.32,2,8)
            cylinder((x,y,1.72),.095,.04,0,8)
    box((-.68,0,.77),(.64,.81,1.07),2,.045)
    core((-.58,0,1.29),.21,.36)
    box((.95,0,.29),(.80,.87,.12),2,.03)
    for x in (.57,.76,.95,1.14):
        box((x,0,.37),(.05,.69,.025),3,.004)
    box((.31,0,1.12),(.27,.78,.20),1)
    box((.47,0,1.12),(.035,.58,.07),4,.01)

def crucible():
    foundation(1.7,.20)
    for y in (-.91,.91):
        diamond((-.11,y,.58),(2.68,.68,.79),0,.05)
        diamond((-.10,y,1.04),(2.47,.59,.20),1)
        box((.36,y,1.16),(.80,.40,.055),3,.014)
        vents((-.60,y,1.17),5,.36,.13,"X")
    box((-.84,0,.65),(.64,1.30,.86),0,.05)
    diamond((-.71,0,1.16),(.90,1.47,.20),1)
    core((-.69,0,1.36),.24,.35)
    box((.42,0,.29),(1.91,1.13,.13),2)
    for x in (-.04,.24,.52,.80,1.08):
        box((x,0,.38),(.08,1.06,.03),1,.006)
    for y in (-.67,.67):
        box((.16,y,1.18),(.24,.22,1.72),2)
        box((.17,y,1.16),(.26,.12,.57),3,.012)
    box((.15,0,2.01),(.37,1.59,.25),1)
    box((.15,0,1.85),(.29,.43,.19),0)
    beam((.15,0,1.80),(.15,0,1.34),.035)
    box((.15,0,1.31),(.24,.14,.12),3,.015)

def resonator():
    foundation(1.48,.20)
    cylinder((0,0,.41),.86,.40,0,12,(0,0,0),.03)
    torus((0,0,.62),.77,.085,3,segments=24)
    core((0,0,1.10),.34,.86)
    for angle in (math.pi,math.pi/3,-math.pi/3):
        x,y=math.cos(angle)*1.02,math.sin(angle)*1.02
        diamond((x,y,.63),(.73,.62,.95),0,.035)
        beam((x,y,.75),(x*.62,y*.62,1.75),.105,2)
        diamond((x*.66,y*.66,1.76),(.39,.36,.42),1,.025)
        cylinder((x*.66,y*.66,2.02),.11,.08,4,8)
        box((x,y,1.17),(.31,.29,.08),3,.012)
    torus((0,0,1.72),.73,.085,1,segments=24)
    torus((0,0,1.76),.58,.045,4,segments=24)

def ward():
    foundation(1.0,.20)
    for x in (-.53,.53):
        for y in (-.53,.53):
            diamond((x,y,.35),(.52,.45,.38),0)
            box((x,y,.58),(.24,.21,.07),3,.015)
    cylinder((-.11,0,.61),.39,.73,2,8)
    diamond((-.03,0,1.17),(.91,.89,.51),0)
    for y in (-.35,.35):
        diamond((-.12,y,1.49),(.90,.23,.14),1)
        barrel((.23,y,1.24),.99,.10)
        box((-.22,y,1.59),(.32,.15,.045),3,.01)
    core((-.29,0,1.47),.15,.25)
    cylinder((-.40,0,1.74),.18,.05,2,8)
    beam((-.40,0,1.74),(-.40,0,2.01),.027)
    box((-.40,0,2.0),(.065,.30,.085),1,.01)

def ore():
    rng=random.Random(913)
    cylinder((0,0,.10),1.05,.20,0,9,bevel=.03)
    diamond((-.42,.23,.24),(.62,.34,.18),1,.012)
    diamond((.53,-.27,.20),(.50,.25,.13),2,.008)
    for i in range(9):
        angle=i*2.39996
        r=.0 if i==0 else rng.uniform(.30,.85)
        x,y=math.cos(angle)*r,math.sin(angle)*r
        height=rng.uniform(.55,1.35) if i else 1.60
        width=rng.uniform(.20,.38)
        pts=[(math.cos(j*2*math.pi/5)*width,math.sin(j*2*math.pi/5)*width) for j in range(5)]
        obj=hull(pts,0,height,0,.006,.44)
        obj.location=(x,y,.13)
        obj.rotation_euler=(rng.uniform(-.22,.22),rng.uniform(-.28,.28),angle)
        shard=hull([(xx*.66,yy*.66) for xx,yy in pts],height*.25,height*.91,4,.0,.45)
        shard.location=(x+.035,y-.015,.145)
        shard.rotation_euler=obj.rotation_euler
    # Exposed conductive seams read from the top even at distant RTS zoom.
    for i in range(5):
        a=i*math.tau/5
        box((math.cos(a)*.79,math.sin(a)*.79,.22),(.32,.09,.045),3,.008,rot=(0,0,a))

SPECS = [
    ("Drudge","Worker",16,25,drudge), ("Ember","Striker",20,48,ember),
    ("Needle","Lancer",21,47,needle), ("Skim","Scout",18,14,skim),
    ("Anvil","Bastion",34,38,anvil), ("Cinderthrow","Mortar",30,38,cinderthrow),
    ("Mend","Mender",19,39,mend), ("Veil","Kite",26,18,veil),
    ("Anchor","Headquarters",125,185,anchor), ("Siphon","Processor",76,112,siphon),
    ("Kiln","Foundry",90,120,kiln), ("Crucible","MotorPool",104,155,crucible),
    ("Resonator","Laboratory",85,160,resonator), ("Ward","Turret",48,120,ward),
    ("Ore","Resource",45,65,ore),
]

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--export-only",choices=[spec[0] for spec in SPECS],action="append",
                    help="Rebuild the pack, but retain other existing FBX files unchanged")
arguments=parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])

collection=bpy.data.collections.new("Game meshes, centimeters")
scene.collection.children.link(collection)
assets=[]
manifest={"generator":"scripts/create_blender_assets.py","blender_version":bpy.app.version_string,
          "unit":"centimeter","forward_axis":"+X","up_axis":"+Z","origin":"bottom center of bounds",
          "material_slots":SLOTS,
          "fbx_export":{"global_scale":1,"scene_unit_scale_length":0.01,"apply_unit_scale":True,
                        "apply_scale_options":"FBX_SCALE_UNITS","axis_forward":"-Y","axis_up":"Z",
                        "use_space_transform":True,"bake_space_transform":False,"triangulated":True},
          "assets":[]}

for name,kind,radius,height,build in SPECS:
    PARTS.clear()
    build()
    bpy.ops.object.select_all(action="DESELECT")
    for part in PARTS:
        part.select_set(True)
    bpy.context.view_layer.objects.active=PARTS[0]
    bpy.ops.object.join()
    obj=bpy.context.object
    obj.name="SM_"+name
    # Remap by material identity, then force the same five slots in every mesh.
    old_materials=list(obj.data.materials)
    mapping=[MATS.index(mat) for mat in old_materials]
    indices=[mapping[poly.material_index] for poly in obj.data.polygons]
    obj.data.materials.clear()
    for mat in MATS:
        obj.data.materials.append(mat)
    for poly,index in zip(obj.data.polygons,indices):
        poly.material_index=index
    if set(indices)!=set(range(5)):
        raise RuntimeError(f"{name}: every material slot must have real geometry")
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    # Baking the object matrix makes the origin independent of the first part.
    for vertex in obj.data.vertices:
        vertex.co=obj.matrix_world @ vertex.co
    obj.matrix_world.identity()
    lo=Vector(tuple(min(v.co[i] for v in obj.data.vertices) for i in range(3)))
    hi=Vector(tuple(max(v.co[i] for v in obj.data.vertices) for i in range(3)))
    span=hi-lo
    planar_scale=2*radius/max(span.x,span.y)
    center=(lo+hi)/2
    for vertex in obj.data.vertices:
        vertex.co.x=(vertex.co.x-center.x)*planar_scale
        vertex.co.y=(vertex.co.y-center.y)*planar_scale
        vertex.co.z=(vertex.co.z-lo.z)*height/span.z
    # Mesh normals and simple packed UVs support later material work.
    bm=bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(obj.data)
    bm.free()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    bpy.context.view_layer.update()
    obj.data.calc_loop_triangles()
    triangle_count=len(obj.data.loop_triangles)
    triangle_areas=[]
    coincident_corner_count=0
    for triangle in obj.data.loop_triangles:
        coords=[obj.data.vertices[index].co for index in triangle.vertices]
        triangle_areas.append((coords[1]-coords[0]).cross(coords[2]-coords[0]).length*.5)
        if any(all(abs(coords[a][axis]-coords[b][axis])<=.00002 for axis in range(3))
               for a,b in ((0,1),(1,2),(2,0))):
            coincident_corner_count+=1
    # UE's FBX normal calculation drops area < .00005 cm²; its mesh builder
    # can also weld triangle corners within .00002 cm component by component.
    if min(triangle_areas)<.00005 or coincident_corner_count:
        raise RuntimeError(f"{name}: degenerate geometry would be removed by Unreal")
    budget=10000 if radius>=48 and kind!="Resource" else 5000
    if triangle_count>budget:
        raise RuntimeError(f"{name}: {triangle_count} triangles exceeds {budget}")
    export_path=OUT/"FBX"/(obj.name+".fbx")
    if not arguments.export_only or name in arguments.export_only:
        bpy.ops.export_scene.fbx(filepath=str(export_path),use_selection=True,object_types={"MESH"},
            global_scale=1,apply_unit_scale=True,apply_scale_options="FBX_SCALE_UNITS",
            use_space_transform=True,bake_space_transform=False,axis_forward="-Y",axis_up="Z",
            mesh_smooth_type="FACE",use_mesh_modifiers=True,use_triangles=True,
            add_leaf_bones=False,bake_anim=False,path_mode="AUTO")
    elif not export_path.exists():
        raise RuntimeError(f"Cannot retain missing FBX {export_path}")
    entry={"name":obj.name,"kind":kind,"definition_radius_cm":radius,
           "dimensions_cm":[round(v,3) for v in obj.dimensions],
           "vertices":len(obj.data.vertices),"triangles":triangle_count,
           "minimum_triangle_area_cm2":min(triangle_areas),
           "unreal_degenerate_triangles":0,
           "material_slots":[mat.name for mat in obj.data.materials],
           "fbx":str(export_path.relative_to(ROOT)),
           "sha256":hashlib.sha256(export_path.read_bytes()).hexdigest()}
    manifest["assets"].append(entry)
    assets.append(obj)
    print("CINDER_MODEL",json.dumps(entry),flush=True)

# Import the exact delivery files into this same centimeter scene and check the
# FBX unit conversion, pivots, slot order and dimensions. Unreal still needs its
# own import/runtime acceptance; this catches exporter mistakes before handoff.
roundtrip=[]
for original,entry in zip(assets,manifest["assets"]):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.import_scene.fbx(filepath=str(ROOT/entry["fbx"]),use_custom_normals=True)
    imported=[obj for obj in bpy.context.selected_objects if obj.type=="MESH"]
    assert len(imported)==1, f"{entry['name']} did not import as one mesh"
    obj=imported[0]
    points=[obj.matrix_world@vertex.co for vertex in obj.data.vertices]
    lo=Vector(tuple(min(p[i] for p in points) for i in range(3)))
    hi=Vector(tuple(max(p[i] for p in points) for i in range(3)))
    dims=hi-lo
    assert max(abs(dims[i]-entry["dimensions_cm"][i]) for i in range(3))<.01, f"{entry['name']} FBX scale changed"
    assert abs(lo.z)<.01 and abs(lo.x+hi.x)<.01 and abs(lo.y+hi.y)<.01, f"{entry['name']} pivot shifted"
    slots=[mat.name.split(".")[0] for mat in obj.data.materials]
    assert slots==SLOTS, f"{entry['name']} slot order changed: {slots}"
    tree=KDTree(len(original.data.vertices))
    for i,vertex in enumerate(original.data.vertices): tree.insert(vertex.co,i)
    tree.balance()
    maximum_vertex_error=max(tree.find(point)[2] for point in points)
    assert maximum_vertex_error<.001, f"{entry['name']} FBX geometry axes changed"
    roundtrip.append({"name":entry["name"],"dimensions_cm":[round(v,3) for v in dims],"bottom_z_cm":round(lo.z,5),
                      "maximum_vertex_error_cm":round(maximum_vertex_error,7),"material_slots":slots,"passed":True})
    bpy.data.objects.remove(obj,do_unlink=True)
(EVIDENCE/"fbx-roundtrip-validation.json").write_text(json.dumps({"blender_version":bpy.app.version_string,"passed":True,"checks":roundtrip},indent=2)+"\n")

# Save an editable source pack with all meshes at true game scale, arranged apart.
for i,obj in enumerate(assets):
    obj.location=((i%5)*340,(i//5)*340,0)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"Cinderline_Cairn_Assembly.blend"))

# Render a genuine Blender contact sheet. Preview copies have uniform display
# size; the exported models retain the physical dimensions recorded above.
collection.hide_render=True
preview=bpy.data.collections.new("Contact sheet presentation")
scene.collection.children.link(preview)
target=Vector((0,0,0))
camera_position=Vector((8,-11,13)).normalized()*2000
rotation=(-camera_position).to_track_quat("-Z","Y")
right=rotation @ Vector((1,0,0))
up=rotation @ Vector((0,1,0))
toward=rotation @ Vector((0,0,1))
camera_data=bpy.data.cameras.new("Isometric contact camera")
camera=bpy.data.objects.new("Isometric contact camera",camera_data)
scene.collection.objects.link(camera)
camera.location=camera_position
camera.rotation_euler=rotation.to_euler()
camera_data.type="ORTHO"
camera_data.ortho_scale=1180
camera_data.clip_end=5000
scene.camera=camera
plinth_mat=material("Preview platform",(.048,.069,.087),.5,.5)
text_mat=material("Preview lettering",(.64,.80,.82),.0,.6,.65)
subtitle_mat=material("Preview accent",(.06,.65,.51),.0,.5,.7)

def text_label(body,pos,size,mat):
    curve=bpy.data.curves.new(body,"FONT")
    curve.body=body
    curve.align_x="CENTER"
    curve.align_y="CENTER"
    curve.size=size
    curve.extrude=0
    label=bpy.data.objects.new(body,curve)
    preview.objects.link(label)
    label.location=pos
    label.rotation_euler=rotation.to_euler()
    curve.materials.append(mat)
    return label

for i,(original,spec) in enumerate(zip(assets,SPECS)):
    name,kind,radius,height,_=spec
    center=right*((i%5-2)*222)+up*((1-i//5)*230-14)
    copy=original.copy()
    copy.data=original.data.copy()
    copy.name="Preview "+name
    preview.objects.link(copy)
    scale=135/max(original.dimensions)
    copy.scale=(scale,)*3
    copy.location=center-up*19
    if name=="Ore":
        ore_panel=material("Preview ore seam",(.65,.24,.035),.5,.4)
        ore_core=material("Preview ore core",(1,.31,.045),.1,.32,1.5)
        copy.data.materials[3]=ore_panel
        copy.data.materials[4]=ore_core
    bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=80,depth=4,location=copy.location-Vector((0,0,3)))
    plinth=bpy.context.object
    plinth.name="Preview footing "+name
    plinth.data.materials.append(plinth_mat)
    for owner in list(plinth.users_collection): owner.objects.unlink(plinth)
    preview.objects.link(plinth)
    text_label(name.upper(),center-up*80+toward*110,13,text_mat)
    text_label(f"{kind}  /  {manifest['assets'][i]['triangles']:,} tris",center-up*100+toward*110,8.1,subtitle_mat)

text_label("C I N D E R L I N E",up*362+toward*100,28,text_mat)
text_label("CAIRN ASSEMBLY   /   ORIGINAL STATIC MESH STUDIES",up*332+toward*100,9.5,subtitle_mat)
text_label("15 GAME MESHES  /  +X FORWARD  /  FIVE MATERIAL SLOTS  /  DISPLAY SCALE VARIES",up*-376+toward*100,8.5,text_mat)

world=bpy.data.worlds.new("Studio world") if not scene.world else scene.world
scene.world=world
world.use_nodes=True
world.node_tree.nodes["Background"].inputs[0].default_value=(.075,.11,.15,1)
world.node_tree.nodes["Background"].inputs[1].default_value=.65

def area(name,location,energy,color,size):
    data=bpy.data.lights.new(name,"AREA")
    data.energy=energy
    data.shape="DISK"
    data.size=size
    data.color=color
    obj=bpy.data.objects.new(name,data)
    scene.collection.objects.link(obj)
    obj.location=location
    obj.rotation_euler=(-Vector(location)).to_track_quat("-Z","Y").to_euler()

area("Large cool key",(500,-650,1250),65000000,(.75,.88,1),1300)
area("Warm rim",(-700,500,700),45000000,(1,.67,.39),1100)
area("Soft fill",(700,900,600),33000000,(.43,.85,1),1000)
scene.render.engine="CYCLES"
scene.cycles.samples=32
scene.cycles.use_denoising=True
scene.cycles.device="CPU"
scene.render.resolution_x=2400
scene.render.resolution_y=1640
scene.render.resolution_percentage=100
scene.render.image_settings.file_format="PNG"
scene.render.film_transparent=False
scene.render.filepath=str(EVIDENCE/"cairn-assembly-contact-sheet.png")
scene.view_settings.view_transform="AgX"
scene.view_settings.look="AgX - Medium High Contrast"
scene.render.image_settings.color_mode="RGBA"
scene.render.film_transparent=False
scene.world.color=(.02,.03,.04)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"Cinderline_Cairn_Contact_Sheet.blend"))
(OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
bpy.ops.render.render(write_still=True)
print("CINDERLINE_MODELS_COMPLETE",str(EVIDENCE/"cairn-assembly-contact-sheet.png"),flush=True)
