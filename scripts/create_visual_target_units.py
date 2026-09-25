#!/usr/bin/env python3
"""Generate Cinderline's richer eight-unit visual target pack and motion parts.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 \
    --python scripts/create_visual_target_units.py
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "RawAssets/VisualTarget/Units"
EVIDENCE = ROOT / "artifacts/visual-target/units"
SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")
UNITS = {
    "Drudge": ("Worker", (32.0, 19.907, 25.0)),
    "Ember": ("Striker", (40.0, 30.854, 48.0)),
    "Needle": ("Lancer", (42.0, 23.807, 47.0)),
    "Skim": ("Scout", (36.0, 17.294, 14.0)),
    "Anvil": ("Bastion", (68.0, 59.691, 38.0)),
    "Cinderthrow": ("Mortar", (60.0, 41.846, 38.0)),
    "Mend": ("Mender", (38.0, 34.244, 39.0)),
    "Veil": ("Kite", (49.695, 52.0, 18.0)),
}
MOTION_PARTS = {
    "Drudge": ("Body", "LegFL", "LegFR", "LegRL", "LegRR", "Tools"),
    "Ember": ("Body", "LegL", "LegR", "Weapon"),
    "Needle": ("Body", "LegL", "LegR", "Weapon"),
    "Anvil": ("Body", "Weapon"),
    "Cinderthrow": ("Body", "Weapon"),
}


def reset_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablock in list(bpy.data.materials):
        bpy.data.materials.remove(datablock)
    scene = bpy.context.scene
    bpy.context.preferences.filepaths.save_version = 0
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = .01


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


def source_point(p):
    return (p[0], -p[1], p[2])


def finish(obj, slot, part, bevel=.35, smooth=False):
    obj["cinder_part"] = part
    obj.name = "{}_{}_{}".format(part, SLOTS[slot], obj.name.replace(" ", ""))
    obj.data.materials.append(MATS[slot])
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = obj.modifiers.new("Machined edge", "BEVEL")
        mod.width = bevel
        mod.segments = 2
        mod.affect = "EDGES"
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for polygon in obj.data.polygons:
        polygon.use_smooth = smooth
    if bevel or smooth:
        normals = obj.modifiers.new("Weighted normals", "WEIGHTED_NORMAL")
        normals.keep_sharp = True
        bpy.ops.object.modifier_apply(modifier=normals.name)
    return obj


def box(part, pos, size, slot=0, bevel=.35, yaw=0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=source_point(pos), rotation=(0, 0, -yaw))
    obj = bpy.context.object
    obj.scale = size
    return finish(obj, slot, part, bevel)


def cylinder(part, pos, radius, depth, slot=2, vertices=16, axis="Z", bevel=.18):
    rot = (0, math.pi / 2, 0) if axis == "X" else (math.pi / 2, 0, 0) if axis == "Y" else (0, 0, 0)
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth,
                                       location=source_point(pos), rotation=rot)
    return finish(bpy.context.object, slot, part, bevel, True)


def sphere(part, pos, radius, slot=4):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=radius, location=source_point(pos))
    return finish(bpy.context.object, slot, part, 0, True)


def beam(part, a, b, radius, slot=2, vertices=12):
    a, b = Vector(source_point(a)), Vector(source_point(b))
    delta = b - a
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=delta.length,
                                       location=(a + b) / 2)
    obj = bpy.context.object
    obj.rotation_euler = delta.to_track_quat("Z", "Y").to_euler()
    return finish(obj, slot, part, .12, True)


def wedge(part, pos, size, slot=0, taper=.72, bevel=.3):
    sx, sy, sz = size
    verts = [(sx/2, -sy/2, -sz/2), (sx/2, sy/2, -sz/2), (-sx/2, sy/2, -sz/2), (-sx/2, -sy/2, -sz/2),
             (sx*.42, -sy*taper/2, sz/2), (sx*.42, sy*taper/2, sz/2), (-sx*.42, sy*taper/2, sz/2), (-sx*.42, -sy*taper/2, sz/2)]
    verts = [source_point((x + pos[0], y + pos[1], z + pos[2])) for x, y, z in verts]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    mesh = bpy.data.meshes.new("Tapered armor")
    mesh.from_pydata(verts, [], faces)
    obj = bpy.data.objects.new("Tapered armor", mesh)
    bpy.context.collection.objects.link(obj)
    return finish(obj, slot, part, bevel)


def wing(part, root, tip, root_chord, tip_chord, thickness, slot=0):
    """Create a swept, tapered lifting surface in the XY plane."""
    rx, ry, z = root; tx, ty, _ = tip; half = thickness*.5
    outline = ((rx+root_chord*.5, ry), (tx+tip_chord*.5, ty),
               (tx-tip_chord*.5, ty), (rx-root_chord*.5, ry))
    verts = [source_point((x, y, z-half)) for x, y in outline]
    verts += [source_point((x, y, z+half)) for x, y in outline]
    faces = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
    mesh = bpy.data.meshes.new("Swept lifting surface"); mesh.from_pydata(verts, [], faces)
    obj = bpy.data.objects.new("Swept lifting surface", mesh); bpy.context.collection.objects.link(obj)
    return finish(obj, slot, part, .22)


def track(part, y, length, width, height):
    box(part, (0, y, height*.48), (length, width, height*.72), 0, .65)
    box(part, (0, y, height*.68), (length*.92, width*1.04, height*.42), 2, .45)
    for x in (-length*.34, 0, length*.34):
        cylinder(part, (x, y + math.copysign(width*.51, y), height*.48), height*.31, .65, 2, 16, "Y", .1)
        cylinder(part, (x, y + math.copysign(width*.55, y), height*.48), height*.14, .72, 4, 12, "Y", .05)
    for index in range(7):
        x = (index / 6 - .5) * length * .88
        box(part, (x, y, height*.94), (length*.105, width*.94, height*.18), 2, 0)
        box(part, (x, y + math.copysign(width*.53, y), height*.45),
            (length*.105, width*.09, height*.62), 2, 0)


def panel_fasteners(part, x_values, y, z, slot=2):
    for x in x_values:
        cylinder(part, (x, y, z), .42, .32, slot, 8, "Y", .05)


def build_drudge():
    p = {name: [] for name in MOTION_PARTS["Drudge"]}
    legs = (("LegFL", (1.886, -4.676, 14.398), (7.0, -7.0)),
            ("LegFR", (1.886, 4.676, 14.398), (7.0, 7.0)),
            ("LegRL", (-5.614, -4.676, 14.398), (-8.0, -7.0)),
            ("LegRR", (-5.614, 4.676, 14.398), (-8.0, 7.0)))
    for part, hip, foot in legs:
        hx, hy, hz = hip; fx, fy = foot; knee = ((hx+fx)*.5+1.2, fy*.92, 8.0)
        p[part] += [cylinder(part, hip, 1.65, 1.7, 3, 14, "Y", .15),
                    beam(part, hip, knee, 1.02, 2, 14), sphere(part, knee, 1.34, 2),
                    beam(part, knee, (fx, fy, 3.5), 0.86, 2, 14),
                    wedge(part, (fx+1.1, fy, 2.0), (7.0, 4.2, 4.0), 0, .55),
                    box(part, (fx+2.8, fy, .65), (4.4, 4.4, 1.3), 2, .28)]
    part = "Body"
    p[part] += [wedge(part, (0, 0, 13.7), (27, 16.5, 7.0), 0),
                wedge(part, (-3, 0, 18.2), (19, 13.0, 5.5), 1),
                wedge(part, (-8, 0, 21.0), (9, 11.5, 5.0), 0, .55),
                box(part, (-4, 0, 21.8), (8, 10.5, 1.8), 3, .4),
                sphere(part, (-7.4, 0, 18.5), 2.5, 4)]
    for y in (-5.8, 5.8):
        p[part].append(box(part, (-1, y, 17.5), (12, 2.0, 3.0), 1, .35))
        panel_fasteners(part, (-5, 0, 5), y + math.copysign(1.1, y), 17.5)
    part = "Tools"
    for y in (-3.2, 3.2):
        p[part] += [beam(part, (6.7, y*.55, 17.0), (13.5, y, 8.0), 1.0, 2),
                    beam(part, (7.7, y*.75, 15.3), (12.7, y, 9.0), .48, 3, 10),
                    box(part, (13.8, y, 6.8), (4.2, 2.4, 5.6), 1, .35),
                    cylinder(part, (15.0, y, 3.4), 1.45, 2.4, 2, 14, "Y", 0),
                    wedge(part, (15.2, y+math.copysign(1.35, y), 2.0), (3.2, 1.8, 3.8), 2, .3, 0)]
    p[part] += [box(part, (11.8, 0, 11.8), (5.0, 8.2, 2.4), 3, .3),
                cylinder(part, (14.8, 0, 8.0), 2.3, 2.0, 4, 16, "X", .15)]
    return p


def infantry(name, heavy=False):
    pivots = {"Ember": ((-6.411, -7.386, 20.706), (-6.411, 7.386, 20.706), (0.377, 8.639, 33.882)),
              "Needle": ((-7.217, -5.699, 19.049), (-7.217, 5.699, 19.049), (-3.959, 0, 36.367))}[name]
    p = {part: [] for part in MOTION_PARTS[name]}
    for part, pivot in zip(("LegL", "LegR"), pivots[:2]):
        x, y, z = pivot
        knee = (x+2.0, y, z*.56); ankle = (x+3.8, y, 4.2)
        p[part] += [cylinder(part, (x, y, z), 2.1, 2.0, 3, 14, "Y", .15),
                    beam(part, (x, y, z), knee, 1.12, 2, 14),
                    wedge(part, (x+.9, y, z-4.7), (5.2, 5.3, 7.4), 1, .52),
                    sphere(part, knee, 1.62, 3),
                    beam(part, knee, ankle, 0.98, 2, 14),
                    wedge(part, (x+3.0, y, 7.3), (5.2, 5.0, 8.0), 0, .58),
                    wedge(part, (x+6.2, y, 2.25), (11.0, 6.0, 4.5), 2, .46),
                    box(part, (x+9.0, y, .7), (5.0, 6.2, 1.4), 0, .25)]
    body = "Body"
    width = 20 if heavy else 23
    p[body] += [wedge(body, (-6, 0, 22.5), (15, width*.72, 6.5), 0, .58),
                cylinder(body, (-4.5, 0, 27.0), 4.0, 3.2, 2, 16, "Z", .2),
                wedge(body, (-3, 0, 30.5), (13, width*.56, 7.0), 1, .5),
                wedge(body, (-1, 0, 36.0), (21, width*.86, 10.5), 0, .56),
                box(body, (-10, 0, 35.5), (6.0, width*.66, 9.5), 1, .5),
                cylinder(body, (-4, 0, 41.0), 2.2, 2.6, 2, 14, "Z", .12),
                wedge(body, (-2, 0, 44.2), (10.5, 11.5, 6.5), 0, .62),
                box(body, (2.0, 0, 44.2), (2.0, 9.2, 2.4), 4, .22),
                box(body, (-5.8, 0, 47.0), (5.5, 6.0, 1.2), 3, .18)]
    shoulder_y = width*.48
    for y in (-shoulder_y, shoulder_y):
        p[body] += [sphere(body, (-1, y, 37.5), 3.4, 1),
                    wedge(body, (-1.5, y, 38.0), (8.0, 5.8, 5.5), 0, .52),
                    beam(body, (-1, y, 36.5), (2.5, y*.78, 31.0), 1.35, 2, 14),
                    sphere(body, (2.5, y*.78, 31.0), 1.55, 3)]
    # A visible off-hand braces the rifle beneath the fore-end.
    support_y = 4.2 if heavy else 6.2
    p[body] += [beam(body, (2.5, -shoulder_y*.78, 31), (7.0, support_y, 32.5), 1.15, 2, 14),
                box(body, (7.5, support_y, 32.7), (3.4, 2.5, 2.2), 3, .3)]
    weapon = "Weapon"
    wx, wy, wz = pivots[2]
    barrels = 2 if heavy else 1
    for index in range(barrels):
        oy = (index-(barrels-1)/2)*3.6
        end_x = 19.0 if heavy else 17.0
        p[weapon] += [cylinder(weapon, (wx+.8, wy+oy, wz), 2.0, 3.5, 0, 16, "X", .18),
                      beam(weapon, (wx+1.8, wy+oy, wz), (end_x, wy+oy, wz+1.0), 1.55 if heavy else 1.95, 2, 14),
                      cylinder(weapon, (end_x+.6, wy+oy, wz+1), 2.55, 3.4, 1, 14, "X", .18),
                      cylinder(weapon, (end_x+2.1, wy+oy, wz+1), 1.75, 1.2, 4, 12, "X", .1)]
    p[weapon] += [wedge(weapon, (wx+3.0, wy, wz), (9.6, 8.4 if heavy else 7.5, 6.9), 0, .55),
                  box(weapon, (wx+6.8, wy, wz-.4), (6.2, 5.6, 4.1), 1, .32),
                  box(weapon, (wx+2, wy, wz+3.4), (4.5, 4.5, 1.3), 3, .2)]
    return p


def build_skim():
    p = {"Body": []}; b = p["Body"]
    b += [wedge("Body", (1, 0, 6), (31, 8.5, 6.5), 0, .42),
          wedge("Body", (-4, 0, 10), (17, 7.5, 4.8), 1, .58),
          wedge("Body", (11, 0, 6.0), (9, 5.0, 3.6), 2, .35),
          sphere("Body", (-4, 0, 10.8), 2.2, 4)]
    for sign in (-1, 1):
        y = sign*5.0
        b += [wing("Body", (4, sign*2.5, 6.8), (-7, sign*8.4, 5.8), 12, 5, 1.5, 0),
              cylinder("Body", (-3, y, 5.0), 2.25, 12.0, 2, 18, "X", .15),
              cylinder("Body", (3.2, y, 5.0), 1.55, 1.5, 4, 14, "X", .1),
              wedge("Body", (-9, sign*7.3, 7.0), (8, 2.4, 5.8), 1, .45),
              box("Body", (-2, sign*7.4, 8.2), (8.0, 1.2, 1.2), 3, .15)]
    # Four exposed lift vanes read as a stable hover platform without forming a slab.
    for x in (-8, -2, 4, 10):
        b += [wing("Body", (x+2, -2.5, 2.5), (x-1, -6.4, 2.0), 3.8, 2.0, .75, 2),
              wing("Body", (x+2, 2.5, 2.5), (x-1, 6.4, 2.0), 3.8, 2.0, .75, 2)]
    b += [cylinder("Body", (1, 0, 10.5), 3.0, 2.4, 1, 18, "Z", .2),
          sphere("Body", (3.0, 0, 11.5), 1.65, 4)]
    return p


def tracked(name, artillery=False):
    p = {part: [] for part in MOTION_PARTS[name]}
    body = "Body"; length = 54 if not artillery else 49; width = 11
    for y in (-22 if not artillery else -15.5, 22 if not artillery else 15.5):
        track(body, y, length, width, 9)
        p[body].extend([])
    # Track helper objects need collecting by tag after construction.
    p[body] = [o for o in bpy.context.scene.objects if o.type == "MESH" and o.get("cinder_part") == body]
    hull_width = 33 if not artillery else 28
    p[body] += [wedge(body, (-2, 0, 13), (58 if not artillery else 51, hull_width, 12), 0),
                wedge(body, (-6, 0, 20), (39, hull_width*.82, 8), 1),
                box(body, (-20, 0, 21), (8, hull_width*.72, 8), 0, .5),
                cylinder(body, (-5, 0, 26.5), 8.0, 3.4, 2, 20, "Z", .3),
                cylinder(body, (-7, 0, 31.0), 4.2, 2.0, 1, 18, "Z", .2),
                box(body, (-3.5, 0, 32.2), (5.5, 2.0, 1.0), 4, .15)]
    side_y = hull_width*.47
    for y in (-side_y, side_y):
        p[body] += [box(body, (-8, y, 21), (20, 2.6, 4.5), 3, 0),
                    wedge(body, (4, y*.92, 22.5), (14, 4.2, 5.5), 1, .5)]
        panel_fasteners(body, (-13, 1), y+math.copysign(1.5, y), 21)
    # Rear engine grille and paired exhausts stay large enough to read at RTS zoom.
    for y in (-8, -4, 0, 4, 8):
        p[body].append(box(body, (-25.0 if not artillery else -21.5, y, 19.0), (1.0, 2.3, 6.0), 2, 0))
    for y in (-7.0, 7.0):
        p[body] += [cylinder(body, (-18, y, 27), 1.8, 7.0, 2, 14, "Z", .15),
                    cylinder(body, (-18, y, 30.5), 2.2, 1.2, 0, 14, "Z", .1)]
    weapon = "Weapon"
    if artillery:
        p[weapon] += [wedge(weapon, (-8, 0, 29), (19, 20, 9), 0, .62),
                      wedge(weapon, (-7, -8, 30), (13, 5, 7), 1, .45),
                      wedge(weapon, (-7, 8, 30), (13, 5, 7), 1, .45),
                      cylinder(weapon, (-10.5, 0, 27.5), 4.5, 5.0, 2, 18, "X", .2),
                      beam(weapon, (-10, -4.5, 29), (27, -4.5, 37), 3.05, 2, 18),
                      beam(weapon, (-10, 4.5, 29), (27, 4.5, 37), 3.05, 2, 18),
                      cylinder(weapon, (-4, -4.5, 30.3), 3.95, 4.4, 0, 16, "X", .18),
                      cylinder(weapon, (-4, 4.5, 30.3), 3.95, 4.4, 0, 16, "X", .18),
                      box(weapon, (24, 0, 36.5), (7, 13.5, 5.2), 1, .45),
                      cylinder(weapon, (28, 0, 37), 4.0, 3.0, 3, 16, "X", .2)]
    else:
        p[weapon] += [wedge(weapon, (5, 0, 29), (25, 23, 10), 1),
                      wedge(weapon, (3, -10, 29), (16, 5, 7), 0, .48),
                      wedge(weapon, (3, 10, 29), (16, 5, 7), 0, .48),
                      cylinder(weapon, (7.5, 0, 30.5), 5.0, 4.5, 0, 18, "X", .22),
                      beam(weapon, (9, -3.8, 31), (31, -3.8, 32), 2.95, 2, 18),
                      beam(weapon, (9, 3.8, 31), (31, 3.8, 32), 2.95, 2, 18),
                      cylinder(weapon, (14, -3.8, 31.2), 3.7, 3.9, 0, 16, "X", .16),
                      cylinder(weapon, (14, 3.8, 31.2), 3.7, 3.9, 0, 16, "X", .16),
                      box(weapon, (29, 0, 32), (7, 12, 5), 0, .45),
                      cylinder(weapon, (1, 0, 35), 3.0, 1.8, 1, 16, "Z", .15),
                      sphere(weapon, (1, 0, 36.5), 2.1, 4)]
    # Recover newly added body and weapon objects by their semantic tags.
    return {part: [o for o in bpy.context.scene.objects if o.type == "MESH" and o.get("cinder_part") == part]
            for part in p}


def build_mend():
    p = {"Body": []}; b = p["Body"]
    for y in (-11, 11):
        b += [box("Body", (-2, y, 5), (24, 9, 8), 2, .7),
              cylinder("Body", (-8, y, 5), 3.6, 2.2, 4, 16, "Y", .15)]
    b += [wedge("Body", (-2, 0, 16), (31, 25, 16), 0),
          cylinder("Body", (-3, 0, 27), 8.5, 5, 1, 20, "Z", .3),
          sphere("Body", (-3, 0, 31), 4.3, 4)]
    for angle in range(0, 360, 45):
        a = math.radians(angle)
        b.append(beam("Body", (-3, 0, 30), (-3+math.cos(a)*13, math.sin(a)*13, 30), .8, 3, 10))
    for y in (-14, 14): panel_fasteners("Body", (-10, -3, 4), y, 16)
    return p


def build_veil():
    p = {"Body": []}; b = p["Body"]
    b += [wedge("Body", (3, 0, 8), (43, 10, 8), 0, .38),
          wedge("Body", (-4, 0, 13), (22, 11, 7), 1, .55),
          wedge("Body", (17, 0, 7.0), (13, 5.0, 3.5), 2, .36),
          sphere("Body", (-3, 0, 14), 2.7, 4)]
    for sign in (-1, 1):
        b += [wing("Body", (8, sign*4, 8), (-8, sign*25, 6.8), 18, 7, 2.1, 0),
              wing("Body", (-5, sign*4, 9.3), (-17, sign*20, 9.0), 10, 4, 1.1, 3),
              cylinder("Body", (-7, sign*10, 5.8), 3.4, 15.5, 2, 20, "X", .2),
              cylinder("Body", (1, sign*10, 5.8), 2.2, 1.8, 4, 16, "X", .12),
              cylinder("Body", (-15, sign*10, 5.8), 2.45, 1.7, 0, 16, "X", .12),
              wedge("Body", (-14, sign*18, 11.0), (10, 3.0, 8.0), 1, .46),
              box("Body", (0, sign*15, 9.5), (11, 2.0, 1.3), 3, .2)]
    # Ventral sensor and nose rails retain the kite's precise, fast role language.
    b += [cylinder("Body", (-1, 0, 4.0), 3.0, 2.0, 1, 18, "Z", .16),
          sphere("Body", (1, 0, 3.0), 1.8, 4),
          beam("Body", (9, -2.0, 7), (23, -2.0, 7), .65, 2, 12),
          beam("Body", (9, 2.0, 7), (23, 2.0, 7), .65, 2, 12)]
    return p


BUILDERS = {"Drudge": build_drudge, "Ember": lambda: infantry("Ember"),
            "Needle": lambda: infantry("Needle", True), "Skim": build_skim,
            "Anvil": lambda: tracked("Anvil"), "Cinderthrow": lambda: tracked("Cinderthrow", True),
            "Mend": build_mend, "Veil": build_veil}


def all_meshes(parts):
    result, seen = [], set()
    for values in parts.values():
        for obj in values:
            pointer = obj.as_pointer()
            if pointer not in seen:
                seen.add(pointer); result.append(obj)
    return result


def bounds(objects):
    points = [obj.matrix_world @ vertex.co for obj in objects for vertex in obj.data.vertices]
    return (Vector(tuple(min(p[i] for p in points) for i in range(3))),
            Vector(tuple(max(p[i] for p in points) for i in range(3))))


def triangle_areas(obj):
    obj.data.calc_loop_triangles()
    return [((obj.data.vertices[triangle.vertices[1]].co-obj.data.vertices[triangle.vertices[0]].co).cross(
             obj.data.vertices[triangle.vertices[2]].co-obj.data.vertices[triangle.vertices[0]].co).length*.5)
            for triangle in obj.data.loop_triangles]


def triangle_diagnostics(obj, threshold=.00005):
    obj.data.calc_loop_triangles(); rows = []
    for triangle in obj.data.loop_triangles:
        points = [obj.data.vertices[index].co for index in triangle.vertices]
        area = (points[1]-points[0]).cross(points[2]-points[0]).length*.5
        if area < threshold:
            polygon = obj.data.polygons[triangle.polygon_index]
            rows.append({"area_cm2": area, "material": obj.data.materials[polygon.material_index].name,
                         "vertices": [vector(point) for point in points]})
    return rows


def normalize(objects, dimensions):
    lo, hi = bounds(objects); span = hi - lo
    for obj in objects:
        bpy.context.view_layer.update()
        for vertex in obj.data.vertices:
            point = obj.matrix_world @ vertex.co
            point = Vector(((point.x-(lo.x+hi.x)/2)*dimensions[0]/span.x,
                            (point.y-(lo.y+hi.y)/2)*dimensions[1]/span.y,
                            (point.z-lo.z)*dimensions[2]/span.z))
            vertex.co = point
        obj.matrix_world.identity()


def joined_copy(objects, name, force_all_slots):
    copies = []
    for original in objects:
        obj = original.copy(); obj.data = original.data.copy(); bpy.context.collection.objects.link(obj); copies.append(obj)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in copies: obj.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join(); obj = bpy.context.object; obj.name = name
    old = list(obj.data.materials); polygon_names = [old[p.material_index].name for p in obj.data.polygons]
    used = SLOTS if force_all_slots else tuple(slot for slot in SLOTS if slot in polygon_names)
    obj.data.materials.clear()
    for slot in used: obj.data.materials.append(MATS[SLOTS.index(slot)])
    for polygon, slot in zip(obj.data.polygons, polygon_names): polygon.material_index = used.index(slot)
    bm = bmesh.new(); bm.from_mesh(obj.data); bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces)); bm.to_mesh(obj.data); bm.free()
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.015)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.calc_loop_triangles()
    return obj, list(used)


def bake_vertex_ao(obj, samples=12):
    """Ray bake gentle cavity occlusion into vertex-color alpha."""
    obj.data.update()
    bvh = BVHTree.FromObject(obj, bpy.context.evaluated_depsgraph_get(), epsilon=0.0)
    scale = max(obj.dimensions); epsilon = max(scale*.00002, .001); distance_limit = scale*.14
    golden_angle = math.pi * (3.0-math.sqrt(5.0)); alpha_by_vertex = []
    for vertex in obj.data.vertices:
        normal = vertex.normal.normalized()
        guide = Vector((0, 0, 1)) if abs(normal.z) < .92 else Vector((0, 1, 0))
        tangent = normal.cross(guide).normalized(); bitangent = normal.cross(tangent).normalized()
        hits = 0; origin = vertex.co + normal*epsilon
        for index in range(samples):
            up = (index+.65)/samples; radial = math.sqrt(max(0.0, 1.0-up*up)); angle = index*golden_angle
            direction = (tangent*(math.cos(angle)*radial) + bitangent*(math.sin(angle)*radial) + normal*up).normalized()
            location, _normal, _face, distance = bvh.ray_cast(origin, direction, distance_limit)
            if location is not None and distance is not None and distance > epsilon*2.0: hits += 1
        alpha_by_vertex.append(max(.28, 1.0-hits/samples*.72))
    colors = obj.data.color_attributes.get("Color")
    if colors: obj.data.color_attributes.remove(colors)
    colors = obj.data.color_attributes.new(name="Color", type="BYTE_COLOR", domain="CORNER")
    for loop_index, loop in enumerate(obj.data.loops):
        colors.data[loop_index].color = (1.0, 1.0, 1.0, alpha_by_vertex[loop.vertex_index])


def copy_vertex_ao(source, target):
    """Transfer the full-model AO bake to a motion part with identical root coordinates."""
    colors = source.data.color_attributes["Color"]
    sums = [0.0]*len(source.data.vertices); counts = [0]*len(source.data.vertices)
    for loop_index, loop in enumerate(source.data.loops):
        sums[loop.vertex_index] += colors.data[loop_index].color[3]; counts[loop.vertex_index] += 1
    alpha = [sums[index]/max(1, counts[index]) for index in range(len(sums))]
    tree = KDTree(len(source.data.vertices))
    for index, vertex in enumerate(source.data.vertices): tree.insert(vertex.co, index)
    tree.balance()
    target_colors = target.data.color_attributes.get("Color")
    if target_colors: target.data.color_attributes.remove(target_colors)
    target_colors = target.data.color_attributes.new(name="Color", type="BYTE_COLOR", domain="CORNER")
    for loop_index, loop in enumerate(target.data.loops):
        _point, nearest, error = tree.find(target.data.vertices[loop.vertex_index].co)
        if error > .001: raise RuntimeError(target.name + " cannot inherit full-model vertex AO")
        target_colors.data[loop_index].color = (1.0, 1.0, 1.0, alpha[nearest])


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT"); obj.select_set(True); bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"}, global_scale=1,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", use_space_transform=True,
        bake_space_transform=False, axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE",
        use_mesh_modifiers=True, use_triangles=True, add_leaf_bones=False, bake_anim=False, path_mode="AUTO")


def vector(v): return [round(float(v[i]), 5) for i in range(3)]


def unreal_bounds(lo, hi):
    """Convert Blender source bounds to the Unreal post-import left-handed frame."""
    return Vector((lo.x, -hi.y, lo.z)), Vector((hi.x, -lo.y, hi.z))


def main():
    reset_scene()
    global MATS
    MATS = [material("HullDark", (.045, .07, .10), .72, .28), material("HullLight", (.34, .42, .47), .62, .25),
            material("Metal", (.12, .16, .19), .9, .2), material("TeamPanel", (.02, .58, .48), .4, .23),
            material("CoreGlow", (.08, 1.0, .76), .12, .18, 3.0)]
    for directory in (OUT / "Models/FBX", OUT / "Motion/FBX", EVIDENCE): directory.mkdir(parents=True, exist_ok=True)
    motion_source = json.loads((ROOT / "RawAssets/Motion/manifest.json").read_text())
    pivot_by_name = {row["name"]: row["joint_pivot_cm"] for row in motion_source["assets"]}
    records, originals, full_models = [], [], []
    unit_parts = {}
    for name, (kind, dimensions) in UNITS.items():
        before = {o.as_pointer() for o in bpy.context.scene.objects}
        parts = BUILDERS[name]()
        objects = [o for o in all_meshes(parts) if o.as_pointer() not in before]
        # Restore semantic groups from tags to include helper-created fasteners.
        part_names = MOTION_PARTS.get(name, ("Body",))
        parts = {part: [o for o in objects if o.get("cinder_part") == part] for part in part_names}
        if any(not values for values in parts.values()): raise RuntimeError(name + " has an empty semantic part")
        normalize(objects, dimensions)
        full, slots = joined_copy(objects, "SM_"+name, True)
        bake_vertex_ao(full)
        full_models.append(full); originals.append(full)
        lo, hi = bounds([full]); full.data.calc_loop_triangles(); triangles = len(full.data.loop_triangles)
        areas = triangle_areas(full)
        if min(areas) < .00005:
            print("DEGENERATE_TRIANGLES", name, json.dumps(triangle_diagnostics(full), sort_keys=True), flush=True)
            raise RuntimeError(f"{name} has a {min(areas):.12g} cm2 triangle below Unreal's threshold")
        print("VISUAL_TARGET_TRIANGLES", name, triangles, flush=True)
        if not 3000 <= triangles <= 8000:
            raise RuntimeError(f"{name} triangle budget {triangles} outside 3000..8000")
        full_path = OUT / "Models/FBX" / (full.name + ".fbx"); export(full, full_path)
        ue_lo, ue_hi = unreal_bounds(lo, hi)
        record = {"name": full.name, "unit": name, "kind": kind, "asset_type": "model",
                  "bounds_min_cm": vector(ue_lo), "bounds_max_cm": vector(ue_hi), "dimensions_cm": vector(ue_hi-ue_lo),
                  "triangles": triangles, "material_slots": slots,
                  "minimum_triangle_area_cm2": min(areas),
                  "unreal_degenerate_triangles": 0,
                  "lod_triangle_percentages": [1.0, .52, .24],
                  "fbx": str(full_path.relative_to(ROOT)), "sha256": hashlib.sha256(full_path.read_bytes()).hexdigest()}
        records.append(record)
        if name in MOTION_PARTS:
            unit_parts[name] = []
            for part_name in MOTION_PARTS[name]:
                part, part_slots = joined_copy(parts[part_name], f"SM_{name}_{part_name}", False)
                copy_vertex_ao(full, part)
                originals.append(part); part_lo, part_hi = bounds([part]); part.data.calc_loop_triangles()
                part_areas = triangle_areas(part)
                if min(part_areas) < .00005:
                    raise RuntimeError(f"{part.name} has a {min(part_areas):.12g} cm2 triangle below Unreal's threshold")
                path = OUT / "Motion/FBX" / (part.name + ".fbx"); export(part, path)
                part_ue_lo, part_ue_hi = unreal_bounds(part_lo, part_hi)
                part_record = {"name": part.name, "unit": name, "kind": kind, "part": part_name,
                    "asset_type": "motion", "bounds_min_cm": vector(part_ue_lo), "bounds_max_cm": vector(part_ue_hi),
                    "dimensions_cm": vector(part_ue_hi-part_ue_lo), "joint_pivot_cm": pivot_by_name[part.name],
                    "triangles": len(part.data.loop_triangles), "material_slots": part_slots,
                    "minimum_triangle_area_cm2": min(part_areas),
                    "unreal_degenerate_triangles": 0,
                    "fbx": str(path.relative_to(ROOT)), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                records.append(part_record); unit_parts[name].append(part_record)
            if sum(row["triangles"] for row in unit_parts[name]) != triangles:
                raise RuntimeError(name + " motion parts do not reconstruct full triangle count")
            combined_lo = Vector(tuple(min(row["bounds_min_cm"][axis] for row in unit_parts[name]) for axis in range(3)))
            combined_hi = Vector(tuple(max(row["bounds_max_cm"][axis] for row in unit_parts[name]) for axis in range(3)))
            if max(abs(combined_lo[i]-ue_lo[i]) for i in range(3)) > .001 or max(abs(combined_hi[i]-ue_hi[i]) for i in range(3)) > .001:
                raise RuntimeError(name + " motion part bounds do not reconstruct the full model")

    checks = []
    for original, record in zip(originals, records):
        bpy.ops.object.select_all(action="DESELECT")
        bpy.ops.import_scene.fbx(filepath=str(ROOT / record["fbx"]), use_custom_normals=True)
        imported = [o for o in bpy.context.selected_objects if o.type == "MESH"]
        if len(imported) != 1: raise RuntimeError(record["name"] + " FBX did not return one mesh")
        obj = imported[0]; obj.data.calc_loop_triangles(); lo, hi = bounds([obj])
        slots = [m.name.split(".")[0] for m in obj.data.materials]
        if slots != record["material_slots"] or len(obj.data.loop_triangles) != record["triangles"]:
            raise RuntimeError(record["name"] + " FBX slots or triangles changed")
        if max(abs((hi-lo)[i]-record["dimensions_cm"][i]) for i in range(3)) > .01:
            raise RuntimeError(record["name"] + " FBX dimensions changed")
        tree = KDTree(len(original.data.vertices))
        for index, vertex in enumerate(original.data.vertices): tree.insert(vertex.co, index)
        tree.balance(); error = max(tree.find(obj.matrix_world @ v.co)[2] for v in obj.data.vertices)
        if error > .001: raise RuntimeError(record["name"] + " FBX coordinates changed")
        colors = obj.data.color_attributes.get("Color")
        if colors is None or len(colors.data) != len(obj.data.loops):
            raise RuntimeError(record["name"] + " FBX vertex-color AO changed")
        alpha_values = [item.color[3] for item in colors.data]
        imported_areas = triangle_areas(obj)
        if min(imported_areas) < .00005:
            raise RuntimeError(record["name"] + " FBX contains a triangle below Unreal's area threshold")
        checks.append({"name": record["name"], "passed": True, "triangles": record["triangles"],
                       "material_slots": slots, "bounds_min_cm": vector(lo), "bounds_max_cm": vector(hi),
                       "maximum_vertex_error_cm": round(error, 7),
                       "minimum_triangle_area_cm2": min(imported_areas),
                       "triangles_below_unreal_threshold": sum(area < .00005 for area in imported_areas),
                       "vertex_color_alpha_range": [round(min(alpha_values), 4), round(max(alpha_values), 4)]})
        bpy.data.objects.remove(obj, do_unlink=True)

    manifest = {"generator": "scripts/create_visual_target_units.py", "blender_version": bpy.app.version_string,
        "reference": "1-Photo-1.jpg mobile RTS composition supplied by user", "original_geometry": True,
        "unit": "centimeter", "forward_axis": "+X", "up_axis": "+Z",
        "fbx_coordinate_conversion": "post-import Unreal position = (source X, -source Y, source Z)",
        "root_origin": "same bottom-centered (0,0,0) complete-model root for full models and every motion part",
        "model_destination": "/Game/Art/VisualTarget/Models", "motion_destination": "/Game/Art/VisualTarget/Motion",
        "collision": "none", "nanite": False, "vertex_color_alpha": "baked ambient occlusion; 1.0 fully lit",
        "material_slot_order": list(SLOTS), "assets": records}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (EVIDENCE / "fbx-roundtrip-validation.json").write_text(json.dumps({"passed": True, "checks": checks}, indent=2)+"\n")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "Cinderline_VisualTarget_Units.blend"))

    # Proportion work needs many regenerations, and the evidence renders below
    # dominate the run. CINDER_UNITS_SKIP_RENDER=1 exports and validates the
    # meshes without them; the pass that records evidence must leave it unset.
    if os.environ.get("CINDER_UNITS_SKIP_RENDER") == "1":
        print("CINDERLINE_VISUAL_TARGET_UNITS_OK", len(records), "assets (renders skipped)", flush=True)
        return

    # Low-cost RTS-angle workbench evidence plus two close silhouette inspections.
    for obj in bpy.context.scene.objects:
        if obj.type == "MESH": obj.hide_render = True
    preview = bpy.data.collections.new("Unit contact sheet"); bpy.context.scene.collection.children.link(preview)
    preview_models, labels = [], []
    for index, original in enumerate(full_models):
        copy = original.copy(); copy.data = original.data.copy(); preview.objects.link(copy)
        unit = list(UNITS)[index]; visual_scale = 88/max(UNITS[unit][1])
        copy.hide_render = False; copy.scale = (visual_scale,)*3; copy.rotation_euler.z = math.radians(-32)
        copy.location = ((index % 4 - 1.5)*145, 135 if index < 4 else -135, 0)
        preview_models.append(copy)
        bpy.ops.object.text_add(location=(copy.location.x, copy.location.y-62, .4))
        label = bpy.context.object; label.data.body = unit
        label.data.align_x = "CENTER"; label.data.size = 9; label.data.extrude = .1; labels.append(label)
    bpy.ops.object.camera_add(location=(0, -720, 650)); camera = bpy.context.object
    camera.rotation_euler = (Vector((0,0,12))-camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"; camera.data.ortho_scale = 690; camera.data.clip_end = 3000
    bpy.context.scene.camera = camera
    scene=bpy.context.scene; scene.render.engine="BLENDER_WORKBENCH"
    scene.display.shading.light="STUDIO"; scene.display.shading.color_type="MATERIAL"
    scene.display.shading.show_shadows=True; scene.display.shading.show_cavity=True; scene.display.shading.cavity_type="WORLD"
    scene.render.resolution_x=1600; scene.render.resolution_y=900; scene.render.resolution_percentage=100
    scene.render.image_settings.file_format="PNG"; scene.render.filepath=str(EVIDENCE/"visual-target-units-contact-sheet.png")
    scene.world.color=(.018,.026,.038); bpy.ops.render.render(write_still=True)

    for obj in preview_models + labels: obj.hide_render = True
    for unit, scale in (("Ember", 64), ("Anvil", 92)):
        index = list(UNITS).index(unit); model = preview_models[index]
        model.hide_render = False; model.location = (0, 0, 0); model.scale = (1, 1, 1)
        model.rotation_euler.z = math.radians(-35)
        target_z = UNITS[unit][1][2]*.42
        camera.location = (85, -145, 105) if unit == "Ember" else (125, -190, 135)
        camera.rotation_euler = (Vector((0,0,target_z))-camera.location).to_track_quat("-Z", "Y").to_euler()
        camera.data.ortho_scale = scale
        scene.render.resolution_x=900; scene.render.resolution_y=900
        scene.render.filepath=str(EVIDENCE/("visual-target-"+unit.lower()+"-closeup.png"))
        bpy.ops.render.render(write_still=True); model.hide_render = True
    print("CINDERLINE_VISUAL_TARGET_UNITS_OK", len(records), "assets", flush=True)


if __name__ == "__main__": main()
