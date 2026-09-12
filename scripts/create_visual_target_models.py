#!/usr/bin/env python3
"""Build Cinderline's detailed visual-target structures and ore in Blender.

Run with Blender 5.2 or newer:
  Blender --background --threads 2 --python scripts/create_visual_target_models.py

The seven FBXs preserve the current runtime mesh bounds, bottom-center origin,
centimeter units, +X forward direction, and five ordered material slots.
"""
from pathlib import Path
import hashlib
import json
import math
import random

import bpy
import bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "RawAssets/VisualTarget/Models"
FBX = OUT / "FBX"
EVIDENCE = ROOT / "artifacts/visual-target/models"
for directory in (OUT, FBX, EVIDENCE):
    directory.mkdir(parents=True, exist_ok=True)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for block in list(bpy.data.materials):
    bpy.data.materials.remove(block)

scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 0.01
bpy.context.preferences.filepaths.save_version = 0

SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")


def make_material(name, color, metallic, roughness, emission=0.0):
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*color, 1.0)
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1.0)
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Emission Color"].default_value = (*color, 1.0)
    shader.inputs["Emission Strength"].default_value = emission
    return material


MATS = (
    make_material("HullDark", (0.027, 0.047, 0.057), 0.70, 0.31),
    make_material("HullLight", (0.48, 0.50, 0.47), 0.57, 0.29),
    make_material("Metal", (0.075, 0.105, 0.11), 0.88, 0.22),
    make_material("TeamPanel", (0.018, 0.55, 0.46), 0.40, 0.25),
    make_material("CoreGlow", (0.12, 1.0, 0.72), 0.10, 0.18, 3.0),
)

PARTS = []


def finish(obj, slot, bevel=0.0, smooth=False, name=None):
    obj.name = name or obj.name
    obj.data.materials.append(MATS[slot])
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        modifier = obj.modifiers.new("Machined edge", "BEVEL")
        modifier.width = bevel
        modifier.segments = 2
        modifier.affect = "EDGES"
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    for face in obj.data.polygons:
        face.use_smooth = smooth
    if smooth:
        for edge in obj.data.edges:
            edge.use_edge_sharp = False
    PARTS.append(obj)
    return obj


def box(pos, size, slot=0, bevel=0.025, rot=(0, 0, 0), name="Armor block"):
    bpy.ops.mesh.primitive_cube_add(size=1, location=pos, rotation=rot)
    obj = bpy.context.object
    obj.scale = size
    return finish(obj, slot, min(bevel, min(size) * 0.20), False, name)


def cylinder(pos, radius, depth, slot=2, vertices=16, rot=(0, 0, 0), bevel=0.012, name="Mechanical cylinder"):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=pos, rotation=rot)
    return finish(bpy.context.object, slot, min(bevel, depth * 0.20), True, name)


def torus(pos, major, minor, slot=2, rot=(0, 0, 0), segments=24, name="Service ring"):
    bpy.ops.mesh.primitive_torus_add(major_segments=segments, minor_segments=8, major_radius=major,
                                     minor_radius=minor, location=pos, rotation=rot)
    return finish(bpy.context.object, slot, 0, True, name)


def beam(a, b, radius, slot=2, vertices=10, name="Support strut"):
    a, b = Vector(a), Vector(b)
    delta = b - a
    obj = cylinder((a + b) * 0.5, radius, delta.length, slot, vertices, bevel=radius * 0.12, name=name)
    obj.rotation_euler = delta.to_track_quat("Z", "Y").to_euler()
    return obj


def pipe(points, radius=0.035, slot=2, name="Utility pipe"):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 1
    curve.bevel_depth = radius
    curve.bevel_resolution = 1
    curve.resolution_u = 1
    spline = curve.splines.new("POLY")
    spline.points.add(len(points) - 1)
    for point, value in zip(spline.points, points):
        point.co = (*value, 1.0)
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(MATS[slot])
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.convert(target="MESH")
    PARTS.append(obj)
    return obj


def prism(points, bottom, top, slot=0, taper=1.0, bevel=0.02, name="Faceted housing"):
    count = len(points)
    verts = [(x, y, bottom) for x, y in points]
    verts += [(x * taper, y * taper, top) for x, y in points]
    faces = [tuple(reversed(range(count))), tuple(range(count, count * 2))]
    faces += [(i, (i + 1) % count, (i + 1) % count + count, i + count) for i in range(count)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return finish(obj, slot, bevel, False, name)


def regular_prism(pos, radius, height, slot=0, sides=8, taper=0.88, rot=0.0, bevel=0.02, name="Machine housing"):
    points = [(math.cos(rot + i * math.tau / sides) * radius,
               math.sin(rot + i * math.tau / sides) * radius) for i in range(sides)]
    obj = prism(points, -height * 0.5, height * 0.5, slot, taper, bevel, name)
    obj.location = pos
    return obj


def armored_block(pos, size, slot=0, taper=0.84, bevel=0.02, chamfer=0.12, name="Sloped armor module"):
    sx, sy, sz = size
    c = min(chamfer, sx * 0.22, sy * 0.22)
    points = [(-sx / 2 + c, -sy / 2), (sx / 2 - c, -sy / 2), (sx / 2, -sy / 2 + c),
              (sx / 2, sy / 2 - c), (sx / 2 - c, sy / 2), (-sx / 2 + c, sy / 2),
              (-sx / 2, sy / 2 - c), (-sx / 2, -sy / 2 + c)]
    obj = prism(points, -sz / 2, sz / 2, slot, taper, bevel, name)
    obj.location = pos
    return obj


def recessed_vent_bank(pos, size, count=5, axis="Y"):
    """Inset black opening, metal surround, and individual cooling vanes."""
    x, y, z = pos
    sx, sy, sz = size
    box(pos, size, 0, 0.006, name="Black machinery recess")
    box((x - sx * 0.10, y, z + sz * 0.55), (sx * 1.15, sy * 1.16, sz * 0.11), 2, 0.006, name="Vent upper frame")
    box((x - sx * 0.10, y, z - sz * 0.55), (sx * 1.15, sy * 1.16, sz * 0.11), 2, 0.006, name="Vent lower frame")
    for index in range(count):
        offset = (index - (count - 1) / 2) / max(count - 1, 1)
        if axis == "Y":
            box((x + sx * 0.53, y + offset * sy * 0.78, z), (sx * 0.10, sy * 0.075, sz * 0.78), 2, 0.004, name="Recessed vent vane")
        else:
            box((x + offset * sx * 0.78, y, z + sz * 0.53), (sx * 0.075, sy * 0.78, sz * 0.10), 2, 0.004, name="Recessed vent vane")


def hazard_stripes(start, spacing, count, size, rot=(0, 0, 0), slots=(3, 2)):
    for index in range(count):
        position = Vector(start) + Vector(spacing) * index
        box(position, size, slots[index % 2], 0.004, rot=rot, name="Hazard deck marking")


def foundation(radius, height=0.13):
    cylinder((0, 0, height * 0.5), radius, height, 2, 16, bevel=0.025, name="Foundation footing")
    cylinder((0, 0, height + 0.025), radius * 0.94, 0.05, 0, 16, bevel=0.01, name="Foundation deck")
    for angle in range(0, 360, 45):
        a = math.radians(angle)
        box((math.cos(a) * radius * 0.80, math.sin(a) * radius * 0.80, height + 0.065),
            (0.18, 0.10, 0.06), 1, 0.012, rot=(0, 0, a), name="Perimeter deck plate")
    for angle in range(0, 360, 22):
        a = math.radians(angle)
        cylinder((math.cos(a) * radius * 0.90, math.sin(a) * radius * 0.90, height + 0.055),
                 0.025, 0.028, 2, 10, bevel=0.004, name="Foundation anchor bolt")


def anchor_foundation(radius, height=0.13):
    regular_prism((0, 0, height * 0.5), radius, height, 2, 8, 0.98, rot=math.pi / 8,
                  bevel=0.025, name="Octagonal headquarters footing")
    regular_prism((0, 0, height + 0.025), radius * 0.94, 0.05, 0, 8, 0.99, rot=math.pi / 8,
                  bevel=0.010, name="Octagonal headquarters deck")
    for index in range(8):
        angle = math.pi / 8 + index * math.tau / 8
        box((math.cos(angle) * radius * 0.78, math.sin(angle) * radius * 0.78, height + 0.060),
            (0.34, 0.13, 0.055), 1 if index % 2 == 0 else 2, 0.010,
            rot=(0, 0, angle), name="Foundation perimeter panel")


def vents(pos, count, span, axis="Y", slot=2):
    for index in range(count):
        shift = (index - (count - 1) / 2) * span / max(count - 1, 1)
        if axis == "Y":
            box((pos[0], pos[1] + shift, pos[2]), (0.035, span / count * 0.55, 0.028), slot, 0.006, name="Cooling grille")
        else:
            box((pos[0] + shift, pos[1], pos[2]), (span / count * 0.55, 0.035, 0.028), slot, 0.006, name="Cooling grille")


def ladder(pos, height, facing="X"):
    x, y, z = pos
    if facing == "X":
        beam((x, y - 0.10, z), (x, y - 0.10, z + height), 0.018, 2, 8, "Ladder rail")
        beam((x, y + 0.10, z), (x, y + 0.10, z + height), 0.018, 2, 8, "Ladder rail")
        for step in range(6):
            beam((x, y - 0.10, z + step * height / 5), (x, y + 0.10, z + step * height / 5), 0.014, 2, 8, "Ladder rung")


def antenna(pos, height=0.42):
    x, y, z = pos
    cylinder((x, y, z + height * 0.5), 0.022, height, 2, 10, bevel=0.005, name="Antenna mast")
    cylinder((x, y, z + height), 0.065, 0.035, 4, 12, bevel=0.006, name="Antenna beacon")


def anchor():
    anchor_foundation(1.62, 0.14)
    # Two armored service wings frame a tall command reactor. Their stacked,
    # tapered shells keep the headquarters readable at gameplay distance.
    for side in (-1, 1):
        y = side * 0.82
        armored_block((-0.20, y, 0.43), (2.30, 0.68, 0.48), 0, 0.78, 0.035, 0.16, "Lower command wing armor")
        for panel_index, (panel_x, panel_z) in enumerate(((-0.88, 0.70), (-0.28, 0.75), (0.32, 0.72))):
            armored_block((panel_x, y, panel_z), (0.54, 0.55, 0.23), 1, 0.72, 0.018, 0.09,
                          "Stepped wing roof panel")
            # Dark separators remain visible between pale roof plates.
            if panel_index < 2:
                box((panel_x + 0.30, y, panel_z + 0.015), (0.045, 0.50, 0.18), 0, 0.004,
                    name="Wing roof expansion seam")
        box((0.44, y - side * 0.30, 0.73), (0.62, 0.055, 0.13), 3, 0.012, name="Command identity panel")
        # Put grille banks directly on the faces seen by the inspection camera.
        face_y = y - 0.35
        for vent_x in (-0.74, 0.02):
            box((vent_x, face_y, 0.48), (0.58, 0.032, 0.25), 0, 0.003, name="Exposed black ventilation recess")
            box((vent_x, face_y - 0.018, 0.625), (0.66, 0.045, 0.045), 2, 0.005, name="Vent bank upper frame")
            box((vent_x, face_y - 0.018, 0.335), (0.66, 0.045, 0.045), 2, 0.005, name="Vent bank lower frame")
            for vane in range(6):
                vane_x = vent_x - 0.245 + vane * 0.098
                box((vane_x, face_y - 0.026, 0.48), (0.035, 0.042, 0.23), 2, 0.004,
                    name="Exposed vertical grille vane")
        for x in (-0.94, -0.42, 0.10, 0.62):
            beam((x, y - side * 0.28, 0.19), (x, y - side * 0.28, 0.76), 0.045, 2, 10, "Wing load strut")
        pipe([(-0.75, y - side * 0.34, 0.34), (-0.55, y - side * 0.34, 0.60),
              (0.55, y - side * 0.34, 0.60), (0.76, y - side * 0.34, 0.34)], 0.035)
        pipe([(-0.98, y + side * 0.25, 0.30), (-1.14, y + side * 0.25, 0.48),
              (-1.14, side * 0.42, 0.70), (-0.73, side * 0.36, 0.83)], 0.052, 2, "Main power cable")
        # A pump and transformer break up each wing without becoming loose greebles.
        regular_prism((0.86, y - side * 0.10, 0.46), 0.21, 0.45, 2, 8, 0.74, bevel=0.016, name="Wing coolant pump")
        cylinder((0.86, y - side * 0.10, 0.70), 0.15, 0.10, 4, 14, bevel=0.010, name="Coolant sight glass")
        armored_block((-1.13, y, 0.86), (0.38, 0.46, 0.54), 1, 0.70, 0.018, 0.07, "Wing transformer")
    regular_prism((-0.38, 0, 0.82), 0.57, 1.20, 0, 10, 0.78, bevel=0.035, name="Command reactor")
    armored_block((-0.38, 0, 0.58), (1.28, 1.02, 0.33), 1, 0.72, 0.025, 0.14, "Reactor shoulder armor")
    cylinder((-0.38, 0, 1.38), 0.235, 0.28, 4, 14, bevel=0.014, name="Compact command energy core")
    torus((-0.38, 0, 1.36), 0.35, 0.045, 2, segments=24, name="Reactor restraint ring")
    for fin_index in range(12):
        angle = fin_index * math.tau / 12
        radius = 0.43
        box((-0.38 + math.cos(angle) * radius, math.sin(angle) * radius, 1.42),
            (0.25, 0.075, 0.16), 1 if fin_index % 2 == 0 else 2, 0.008,
            rot=(0, 0, angle), name="Radial reactor cooling fin")
    for angle in (0, math.pi / 2, math.pi, math.pi * 1.5):
        x, y = -0.38 + math.cos(angle) * 0.55, math.sin(angle) * 0.55
        beam((x, y, 0.42), (x, y, 1.48), 0.055, 2, 10, "Reactor cage")
    box((0.78, 0, 0.31), (0.90, 0.66, 0.14), 2, 0.025, rot=(0, 0.10, 0), name="Vehicle ramp")
    for x in (0.48, 0.68, 0.88, 1.08):
        box((x, 0, 0.40), (0.055, 0.57, 0.025), 3, 0.006, rot=(0, 0.10, 0), name="Ramp guide")
    hazard_stripes((0.48, -0.25, 0.405), (0.16, 0, 0.016), 5, (0.075, 0.46, 0.018),
                   (0, 0.10, 0), slots=(1, 2))
    # The service entrance sits squarely behind the ramp under its own canopy.
    box((0.73, -0.37, 0.60), (0.40, 0.035, 0.52), 0, 0.004, name="Recessed service door")
    for frame_x in (0.49, 0.97):
        box((frame_x, -0.395, 0.60), (0.055, 0.055, 0.60), 2, 0.006, name="Service door side frame")
    box((0.73, -0.395, 0.91), (0.54, 0.055, 0.065), 2, 0.007, name="Service door lintel")
    armored_block((0.73, -0.40, 0.99), (0.70, 0.42, 0.18), 1, 0.66, 0.015, 0.08,
                  "Sloped service entrance canopy")
    box((0.73, -0.425, 0.76), (0.25, 0.04, 0.055), 3, 0.006, name="Service entrance status light")
    for y in (-0.34, 0.34):
        regular_prism((-1.05, y, 1.15), 0.22, 0.88, 1, 8, 0.78, bevel=0.02, name="Command pylon")
        antenna((-1.05, y, 1.55), 0.30)
    ladder((-0.96, 0, 0.45), 0.70)


def kiln():
    foundation(1.50, 0.13)
    # Twin furnace drums feed one exposed casting line.
    for side in (-1, 1):
        y = side * 0.58
        cylinder((-0.35, y, 0.66), 0.43, 0.86, 0, 18, bevel=0.035, name="Furnace drum")
        # These faceted jackets are open visually at the service side. The dark
        # inset beneath them reads as furnace depth, not another bright barrel.
        armored_block((-0.37, y, 0.68), (0.82, 0.69, 0.58), 1, 0.73, 0.025, 0.14, "Furnace armor jacket")
        recessed_vent_bank((-0.02, y - side * 0.355, 0.67), (0.40, 0.045, 0.27), 5, "Y")
        torus((-0.35, y, 0.51), 0.43, 0.055, 2, segments=24, name="Furnace band")
        torus((-0.35, y, 0.83), 0.43, 0.055, 2, segments=24, name="Furnace band")
        cylinder((-0.35, y, 1.05), 0.24, 0.22, 1, 16, bevel=0.02, name="Exhaust collar")
        cylinder((-0.35, y, 1.25), 0.14, 0.32, 2, 14, bevel=0.015, name="Exhaust stack")
        cylinder((-0.35, y, 1.43), 0.18, 0.06, 0, 14, bevel=0.01, name="Stack cap")
        box((0.13, y, 0.67), (0.10, 0.40, 0.52), 3, 0.018, name="Furnace status spine")
        pipe([(-0.72, y, 0.45), (-0.90, y, 0.65), (-0.90, side * 0.23, 0.77)], 0.045)
        pipe([(-0.45, y + side * 0.34, 0.38), (-0.10, y + side * 0.34, 0.30),
              (0.31, side * 0.34, 0.30), (0.46, side * 0.34, 0.43)], 0.050, 2, "Furnace feed cable")
    box((-0.88, 0, 0.68), (0.45, 0.60, 0.83), 2, 0.04, name="Heat exchanger")
    vents((-1.115, 0, 0.68), 7, 0.45, "Y")
    cylinder((-0.73, 0, 1.03), 0.18, 0.28, 4, 14, bevel=0.012, name="Molten core")
    box((0.70, 0, 0.27), (1.02, 0.85, 0.14), 2, 0.025, name="Casting conveyor")
    armored_block((0.72, 0, 0.44), (0.92, 0.92, 0.20), 0, 0.72, 0.018, 0.12, "Conveyor side armor")
    for x in (0.30, 0.52, 0.74, 0.96, 1.18):
        cylinder((x, 0, 0.36), 0.055, 0.74, 1, 12, rot=(math.pi / 2, 0, 0), bevel=0.008, name="Conveyor roller")
    for y in (-0.45, 0.45):
        beam((0.27, y, 0.17), (1.18, y, 0.17), 0.04, 0, 8, "Conveyor rail")
    hazard_stripes((0.34, -0.37, 0.55), (0.17, 0, 0), 6, (0.08, 0.13, 0.022))
    for side in (-1, 1):
        y = side * 1.02
        regular_prism((0.37, y, 0.34), 0.23, 0.43, 2, 8, 0.72, bevel=0.016, name="Slag pump")
        cylinder((0.37, y, 0.58), 0.14, 0.08, 4, 14, bevel=0.009, name="Slag pump glow cap")
        pipe([(0.37, y, 0.45), (0.37, side * 0.78, 0.62), (0.06, side * 0.65, 0.72)], 0.038)


def siphon():
    foundation(1.38, 0.14)
    regular_prism((-0.35, 0, 0.58), 0.68, 0.72, 0, 10, 0.82, bevel=0.04, name="Separator vessel")
    cylinder((-0.35, 0, 1.03), 0.30, 0.56, 4, 16, bevel=0.018, name="Separation core")
    torus((-0.35, 0, 0.92), 0.41, 0.055, 3, segments=28, name="Flow monitor")
    for side in (-1, 1):
        y = side * 0.64
        cylinder((-0.20, y, 0.68), 0.28, 0.80, 1, 16, bevel=0.025, name="Filter column")
        torus((-0.20, y, 0.52), 0.29, 0.042, 2, segments=20, name="Filter brace")
        torus((-0.20, y, 0.84), 0.29, 0.042, 2, segments=20, name="Filter brace")
        pipe([(-0.20, y, 0.99), (0.15, y, 1.12), (0.56, side * 0.38, 1.12)], 0.050, 2, "Intake manifold")
        box((-0.20, y - side * 0.285, 0.68), (0.34, 0.05, 0.32), 3, 0.012, name="Filter readout")
    regular_prism((0.63, 0, 0.69), 0.49, 0.84, 0, 8, 0.76, bevel=0.035, name="Compressor housing")
    cylinder((0.63, 0, 1.13), 0.39, 0.11, 1, 16, bevel=0.014, name="Compressor crown")
    for angle in (math.pi / 4, 3 * math.pi / 4, 5 * math.pi / 4, 7 * math.pi / 4):
        x, y = math.cos(angle) * 0.96, math.sin(angle) * 0.96
        beam((x, y, 0.17), (x * 0.72, y * 0.72, 0.58), 0.052, 2, 10, "Processor buttress")


def crucible():
    foundation(1.62, 0.14)
    # Gantry encloses an open vehicle assembly bay along +X.
    armored_block((-0.75, 0, 0.62), (0.72, 1.34, 0.88), 0, 0.72, 0.04, 0.15, "Motor pool service block")
    armored_block((-0.73, 0, 1.12), (0.88, 1.44, 0.24), 1, 0.68, 0.025, 0.15, "Service block cap")
    recessed_vent_bank((-1.12, 0, 0.65), (0.055, 0.82, 0.38), 9, "Y")
    cylinder((-0.72, 0, 1.45), 0.22, 0.25, 4, 16, bevel=0.018, name="Power core")
    box((0.42, 0, 0.25), (1.75, 1.02, 0.13), 2, 0.022, name="Assembly bay floor")
    for x in (-0.10, 0.22, 0.54, 0.86, 1.18):
        box((x, 0, 0.34), (0.07, 0.94, 0.035), 1, 0.006, name="Assembly floor rail")
    hazard_stripes((0.00, -0.42, 0.405), (0.20, 0, 0), 6, (0.10, 0.10, 0.018))
    for side in (-1, 1):
        y = side * 0.72
        for x in (-0.25, 0.55):
            box((x, y, 0.82), (0.22, 0.20, 1.30), 2, 0.025, name="Gantry column")
            armored_block((x, y, 0.87), (0.36, 0.34, 0.72), 1, 0.68, 0.020, 0.07, "Gantry column armor")
            box((x, y - side * 0.11, 0.86), (0.24, 0.07, 0.52), 3, 0.012, name="Gantry light strip")
            beam((x, y, 0.20), (x + 0.28, y, 0.56), 0.050, 0, 10, "Gantry knee brace")
        beam((-0.25, y, 1.50), (0.55, y, 1.50), 0.075, 1, 12, "Gantry shoulder")
        pipe([(-0.22, y, 1.22), (0.02, y, 1.34), (0.50, y, 1.34)], 0.035)
    beam((-0.25, -0.72, 1.50), (-0.25, 0.72, 1.50), 0.075, 1, 12, "Overhead crane bridge")
    box((-0.25, 0, 1.42), (0.32, 0.36, 0.19), 0, 0.02, name="Crane trolley")
    beam((-0.25, 0, 1.35), (-0.25, 0, 0.88), 0.025, 2, 8, "Crane cable")
    box((-0.25, 0, 0.84), (0.21, 0.10, 0.10), 3, 0.012, name="Crane hook block")
    # Bay-side welding and hydraulic units give the empty work floor purpose.
    for side in (-1, 1):
        y = side * 0.47
        regular_prism((0.72, y, 0.52), 0.20, 0.45, 0, 8, 0.70, bevel=0.018, name="Assembly tool pedestal")
        beam((0.72, y, 0.69), (0.43, y * 0.72, 0.87), 0.045, 2, 10, "Assembly manipulator")
        cylinder((0.40, y * 0.70, 0.89), 0.075, 0.11, 4, 12, rot=(math.pi / 2, 0, 0), bevel=0.008, name="Welding head")
        pipe([(0.72, y, 0.43), (0.92, y, 0.34), (0.92, side * 0.72, 0.28)], 0.030, 2, "Tool hydraulic hose")


def resonator():
    foundation(1.42, 0.14)
    cylinder((0, 0, 0.43), 0.76, 0.55, 0, 18, bevel=0.035, name="Research drum")
    torus((0, 0, 0.70), 0.70, 0.065, 3, segments=32, name="Lower field coil")
    cylinder((0, 0, 1.02), 0.29, 0.86, 4, 18, bevel=0.016, name="Resonance core")
    for z, radius in ((0.91, 0.46), (1.22, 0.52), (1.48, 0.60)):
        torus((0, 0, z), radius, 0.055, 1 if z < 1.4 else 4, segments=32, name="Field coil")
    for angle in (0, math.tau / 3, math.tau * 2 / 3):
        x, y = math.cos(angle) * 0.95, math.sin(angle) * 0.95
        regular_prism((x, y, 0.64), 0.31, 0.83, 0, 8, 0.72, bevel=0.025, name="Field generator")
        box((x, y, 0.92), (0.24, 0.18, 0.08), 3, 0.012, rot=(0, 0, angle), name="Generator readout")
        beam((x, y, 0.74), (math.cos(angle) * 0.52, math.sin(angle) * 0.52, 1.46), 0.065, 2, 10, "Coil support")
        pipe([(x, y, 0.38), (x * 0.78, y * 0.78, 0.29), (0.32 * math.cos(angle), 0.32 * math.sin(angle), 0.29)], 0.035)
        antenna((x, y, 0.97), 0.22)
    for angle in (math.pi / 3, math.pi, math.pi * 5 / 3):
        x, y = math.cos(angle) * 1.15, math.sin(angle) * 1.15
        beam((x, y, 0.16), (x * 0.77, y * 0.77, 0.59), 0.06, 2, 10, "Laboratory buttress")


def ward():
    foundation(0.95, 0.13)
    for angle in (math.pi / 4, 3 * math.pi / 4, 5 * math.pi / 4, 7 * math.pi / 4):
        x, y = math.cos(angle) * 0.59, math.sin(angle) * 0.59
        regular_prism((x, y, 0.28), 0.25, 0.34, 0, 6, 0.75, bevel=0.025, name="Turret outrigger")
        beam((x, y, 0.32), (x * 0.48, y * 0.48, 0.67), 0.055, 2, 10, "Turret brace")
        box((x, y, 0.49), (0.18, 0.16, 0.07), 3, 0.012, rot=(0, 0, angle), name="Defense indicator")
    cylinder((-0.12, 0, 0.65), 0.36, 0.78, 2, 16, bevel=0.025, name="Turret pedestal")
    torus((-0.12, 0, 0.93), 0.39, 0.05, 1, segments=24, name="Turret traverse ring")
    regular_prism((0.0, 0, 1.12), 0.49, 0.46, 0, 8, 0.72, bevel=0.03, name="Turret head")
    for side in (-1, 1):
        y = side * 0.29
        box((0.18, y, 1.21), (0.60, 0.18, 0.18), 1, 0.022, name="Weapon shroud")
        beam((0.29, y, 1.20), (1.05, y, 1.20), 0.075, 2, 12, "Defense barrel")
        cylinder((1.08, y, 1.20), 0.105, 0.17, 0, 12, rot=(0, math.pi / 2, 0), bevel=0.014, name="Muzzle brake")
        box((-0.27, y, 1.34), (0.25, 0.14, 0.055), 3, 0.010, name="Targeting vane")
    cylinder((-0.25, 0, 1.38), 0.15, 0.23, 4, 14, bevel=0.012, name="Targeting core")
    antenna((-0.35, 0, 1.47), 0.24)


def crystal(pos, radius, height, tilt, slot, sides, name):
    bpy.ops.mesh.primitive_cone_add(vertices=sides, radius1=radius, radius2=0,
                                    depth=height, location=(pos[0], pos[1], pos[2] + height * 0.5),
                                    rotation=tilt)
    return finish(bpy.context.object, slot, 0, False, name)


def ore():
    rng = random.Random(120926)
    # Broken, irregular basalt base.
    for index in range(14):
        angle = index * 2.399963
        radius = 0.12 if index == 0 else rng.uniform(0.25, 0.86)
        x, y = math.cos(angle) * radius, math.sin(angle) * radius
        regular_prism((x, y, rng.uniform(0.07, 0.14)), rng.uniform(0.20, 0.38), rng.uniform(0.14, 0.28),
                      0 if index % 3 else 1, rng.choice((5, 6, 7)), rng.uniform(0.68, 0.90),
                      rot=rng.uniform(0, math.tau), bevel=0.008, name="Broken basalt")
    # Amber clusters have a dominant spine and several tilted child crystals.
    cluster_specs = [((0.0, 0.0), 0.20, 1.32, 0.0), ((-0.38, 0.16), 0.15, 0.91, -0.22),
                     ((0.34, -0.12), 0.16, 0.81, 0.20), ((0.42, 0.28), 0.12, 0.60, -0.18),
                     ((-0.16, -0.38), 0.13, 0.66, 0.16), ((-0.55, -0.18), 0.10, 0.48, 0.25)]
    for index, ((x, y), radius, height, tilt) in enumerate(cluster_specs):
        crystal((x, y, 0.17), radius, height, (tilt, -tilt * 0.7, index * 0.67), 4, 6, "Amber crystal core")
        torus((x, y, 0.22 + index * 0.003), radius * 0.92, radius * 0.09, 3,
              rot=(0, 0, index * 0.67), segments=12, name="Copper mineral seam")
    for angle in (0.2, 1.7, 3.2, 4.8):
        x, y = math.cos(angle) * 0.68, math.sin(angle) * 0.68
        box((x, y, 0.23), (0.33, 0.055, 0.035), 3, 0.006, rot=(0, 0, angle), name="Exposed mineral vein")
    # Slot Metal remains populated through dense stone clamps around the center.
    for angle in (0, math.pi * 2 / 3, math.pi * 4 / 3):
        x, y = math.cos(angle) * 0.29, math.sin(angle) * 0.29
        regular_prism((x, y, 0.25), 0.075, 0.16, 2, 6, 0.75, bevel=0.006, name="Dark mineral inclusion")


SPECS = (
    ("Anchor", "Headquarters", (250.0, 250.0, 185.0), anchor),
    ("Kiln", "Foundry", (180.0, 180.0, 120.0), kiln),
    ("Siphon", "Processor", (152.0, 152.0, 112.0), siphon),
    ("Crucible", "MotorPool", (208.0, 208.0, 155.0), crucible),
    ("Resonator", "Laboratory", (170.0, 170.0, 160.0), resonator),
    ("Ward", "Turret", (96.0, 83.17, 120.0), ward),
    ("Ore", "Resource", (88.729, 90.0, 65.0), ore),
)


def mesh_bounds(obj):
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    lo = Vector(tuple(min(point[i] for point in points) for i in range(3)))
    hi = Vector(tuple(max(point[i] for point in points) for i in range(3)))
    return lo, hi


def bake_vertex_ao(obj, samples=16):
    """Store small-scale cavity AO in vertex-color alpha for M_CinderModelV3."""
    obj.data.update()
    bvh = BVHTree.FromObject(obj, bpy.context.evaluated_depsgraph_get(), epsilon=0.0)
    scale = max(obj.dimensions)
    epsilon = max(scale * 0.00002, 0.001)
    max_distance = scale * 0.14
    vertex_alpha = []
    golden_angle = math.pi * (3.0 - math.sqrt(5.0))
    for vertex in obj.data.vertices:
        normal = vertex.normal.normalized()
        guide = Vector((0, 0, 1)) if abs(normal.z) < 0.92 else Vector((0, 1, 0))
        tangent = normal.cross(guide).normalized()
        bitangent = normal.cross(tangent).normalized()
        hits = 0
        origin = vertex.co + normal * epsilon
        for index in range(samples):
            up = (index + 0.65) / samples
            radial = math.sqrt(max(0.0, 1.0 - up * up))
            angle = index * golden_angle
            direction = (tangent * (math.cos(angle) * radial)
                         + bitangent * (math.sin(angle) * radial) + normal * up).normalized()
            location, _normal, _face, distance = bvh.ray_cast(origin, direction, max_distance)
            if location is not None and distance is not None and distance > epsilon * 2.0:
                hits += 1
        vertex_alpha.append(max(0.28, 1.0 - hits / samples * 0.72))
    attribute = obj.data.color_attributes.get("AO")
    if attribute:
        obj.data.color_attributes.remove(attribute)
    attribute = obj.data.color_attributes.new(name="AO", type="BYTE_COLOR", domain="CORNER")
    for loop_index, loop in enumerate(obj.data.loops):
        attribute.data[loop_index].color = (1.0, 1.0, 1.0, vertex_alpha[loop.vertex_index])
    obj.data.color_attributes.active_color = attribute
    obj.data.color_attributes.render_color_index = obj.data.color_attributes.find("AO")
    return min(vertex_alpha), sum(vertex_alpha) / len(vertex_alpha)


def finalize(name, target_dimensions):
    bpy.ops.object.select_all(action="DESELECT")
    for part in PARTS:
        part.select_set(True)
    bpy.context.view_layer.objects.active = PARTS[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = "SM_" + name
    old_materials = list(obj.data.materials)
    remap = [MATS.index(material) for material in old_materials]
    material_indices = [remap[face.material_index] for face in obj.data.polygons]
    obj.data.materials.clear()
    for material in MATS:
        obj.data.materials.append(material)
    for face, index in zip(obj.data.polygons, material_indices):
        face.material_index = index
    if set(material_indices) != set(range(5)):
        raise RuntimeError(name + " does not populate all five material slots")
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    for vertex in obj.data.vertices:
        vertex.co = obj.matrix_world @ vertex.co
    obj.matrix_world.identity()
    lo, hi = mesh_bounds(obj)
    span = hi - lo
    center = (lo + hi) * 0.5
    for vertex in obj.data.vertices:
        vertex.co.x = (vertex.co.x - center.x) * target_dimensions[0] / span.x
        vertex.co.y = (vertex.co.y - center.y) * target_dimensions[1] / span.y
        vertex.co.z = (vertex.co.z - lo.z) * target_dimensions[2] / span.z
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(obj.data)
    bm.free()
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.015)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.calc_loop_triangles()
    return obj, bake_vertex_ao(obj)


manifest = {
    "generator": "scripts/create_visual_target_models.py",
    "blender_version": bpy.app.version_string,
    "unit": "centimeter", "forward_axis": "+X", "up_axis": "+Z",
    "origin": "bottom center of bounds", "material_slots": list(SLOTS),
    "destination": "/Game/Art/VisualTarget/Models",
    "fbx_export": {"global_scale": 1, "scene_unit_scale_length": 0.01, "apply_unit_scale": True,
                   "apply_scale_options": "FBX_SCALE_UNITS", "axis_forward": "-Y", "axis_up": "Z",
                   "use_space_transform": True, "bake_space_transform": False, "triangulated": True},
    "assets": [],
}
assets = []
for name, kind, target_dimensions, builder in SPECS:
    PARTS.clear()
    builder()
    obj, ao_stats = finalize(name, target_dimensions)
    obj.select_set(True)
    export_path = FBX / (obj.name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=str(export_path), use_selection=True, object_types={"MESH"},
        global_scale=1, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        use_space_transform=True, bake_space_transform=False, axis_forward="-Y", axis_up="Z",
        mesh_smooth_type="FACE", use_mesh_modifiers=True, use_triangles=True,
        add_leaf_bones=False, bake_anim=False, path_mode="AUTO")
    obj.data.calc_loop_triangles()
    lo, hi = mesh_bounds(obj)
    triangles = len(obj.data.loop_triangles)
    triangle_areas = []
    for triangle in obj.data.loop_triangles:
        points = [obj.data.vertices[index].co for index in triangle.vertices]
        triangle_areas.append((points[1] - points[0]).cross(points[2] - points[0]).length * 0.5)
    if min(triangle_areas) < 0.00005:
        raise RuntimeError(f"{name}: a triangle falls below Unreal's import area threshold")
    triangle_ceiling = 24000 if name == "Anchor" else 20000
    if name != "Ore" and not 8000 <= triangles <= triangle_ceiling:
        raise RuntimeError(f"{name}: {triangles} triangles falls outside its 8k-{triangle_ceiling // 1000}k target")
    if name == "Ore" and triangles > 10000:
        raise RuntimeError(f"Ore: {triangles} triangles exceeds 10k")
    entry = {"name": obj.name, "kind": kind, "definition_radius_cm": max(target_dimensions[:2]) * 0.5,
             "dimensions_cm": [round(value, 3) for value in (hi - lo)],
             "vertices": len(obj.data.vertices), "triangles": triangles,
             "minimum_triangle_area_cm2": min(triangle_areas), "unreal_degenerate_triangles": 0,
             "vertex_ao": {"attribute": "AO", "channel": "alpha", "fully_lit": 1.0,
                           "minimum": round(ao_stats[0], 4), "mean": round(ao_stats[1], 4), "samples": 16},
             "material_slots": [material.name for material in obj.data.materials],
             "fbx": str(export_path.relative_to(ROOT)),
             "sha256": hashlib.sha256(export_path.read_bytes()).hexdigest(),
             "lod_triangle_percentages": [1.0, 0.52, 0.24]}
    manifest["assets"].append(entry)
    assets.append(obj)
    print("CINDER_VISUAL_TARGET", json.dumps(entry, sort_keys=True), flush=True)

(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

# FBX roundtrip checks catch exporter scale, pivot, slot, and section failures.
checks = []
for original, entry in zip(assets, manifest["assets"]):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.import_scene.fbx(filepath=str(ROOT / entry["fbx"]), use_custom_normals=True)
    imported = [obj for obj in bpy.context.selected_objects if obj.type == "MESH"]
    if len(imported) != 1:
        raise RuntimeError(entry["name"] + " did not roundtrip as one mesh")
    obj = imported[0]
    lo, hi = mesh_bounds(obj)
    dimensions = hi - lo
    slots = [material.name.split(".")[0] for material in obj.data.materials]
    ao_attribute = obj.data.color_attributes.get("AO")
    obj.data.calc_loop_triangles()
    triangle_areas = []
    for triangle in obj.data.loop_triangles:
        points = [obj.data.vertices[index].co for index in triangle.vertices]
        triangle_areas.append((points[1] - points[0]).cross(points[2] - points[0]).length * 0.5)
    if max(abs(dimensions[i] - entry["dimensions_cm"][i]) for i in range(3)) >= 0.01:
        raise RuntimeError(entry["name"] + " dimensions changed during FBX roundtrip")
    if abs(lo.z) >= 0.01 or abs(lo.x + hi.x) >= 0.01 or abs(lo.y + hi.y) >= 0.01:
        raise RuntimeError(entry["name"] + " origin changed during FBX roundtrip")
    if slots != list(SLOTS):
        raise RuntimeError(entry["name"] + " slots changed during FBX roundtrip")
    if ao_attribute is None or ao_attribute.domain != "CORNER":
        raise RuntimeError(entry["name"] + " vertex AO did not survive FBX roundtrip")
    ao_values = [value.color[3] for value in ao_attribute.data]
    if min(ao_values) >= 0.995:
        raise RuntimeError(entry["name"] + " vertex AO alpha became uniformly white")
    if len(obj.data.loop_triangles) != entry["triangles"]:
        raise RuntimeError(entry["name"] + " triangle count changed during FBX roundtrip")
    if min(triangle_areas) < 0.00005:
        raise RuntimeError(entry["name"] + " gained degenerate geometry during FBX roundtrip")
    checks.append({"name": entry["name"], "dimensions_cm": [round(v, 3) for v in dimensions],
                   "bottom_z_cm": round(lo.z, 6), "triangles": len(obj.data.loop_triangles),
                   "minimum_triangle_area_cm2": min(triangle_areas),
                   "vertex_ao_alpha": {"minimum": round(min(ao_values), 4), "maximum": round(max(ao_values), 4)},
                   "material_slots": slots, "passed": True})
    bpy.data.objects.remove(obj, do_unlink=True)
(EVIDENCE / "fbx-roundtrip-validation.json").write_text(json.dumps({"passed": True, "checks": checks}, indent=2) + "\n")

# Preserve a source scene with the real-scale assets separated for later edits.
for index, obj in enumerate(assets):
    obj.location = ((index % 4) * 310, (index // 4) * 310, 0)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "Cinderline_VisualTarget_Models.blend"))

# A low-cost Workbench contact sheet is enough to verify silhouettes and density.
for index, obj in enumerate(assets):
    obj.location = ((index % 4 - 1.5) * 260, (0.5 - index // 4) * 280, 0)
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.studio_light = "paint.sl"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "BOTH"
camera_data = bpy.data.cameras.new("Visual target contact camera")
camera = bpy.data.objects.new("Visual target contact camera", camera_data)
scene.collection.objects.link(camera)
camera.location = (760, -1020, 980)
camera.rotation_euler = ((Vector((0, 0, 80)) - camera.location).to_track_quat("-Z", "Y").to_euler())
camera_data.type = "ORTHO"
camera_data.ortho_scale = 1200
camera_data.clip_end = 5000
scene.camera = camera
scene.render.resolution_x = 1200
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(EVIDENCE / "visual-target-models-contact-sheet.png")
scene.world.color = (0.018, 0.025, 0.03)
bpy.ops.render.render(write_still=True)

# Keep a close inspection render alongside the sheet. The overview deliberately
# compares silhouettes, so small recesses and cable runs need their own frame.
anchor_object = assets[0]
for obj in assets[1:]:
    obj.hide_render = True
anchor_target = anchor_object.location + Vector((0, 0, 72))
camera.location = anchor_target + Vector((310, -390, 300))
camera.rotation_euler = ((anchor_target - camera.location).to_track_quat("-Z", "Y").to_euler())
camera_data.ortho_scale = 330
scene.render.resolution_x = 1100
scene.render.resolution_y = 900
scene.render.filepath = str(EVIDENCE / "visual-target-anchor-closeup.png")
bpy.context.view_layer.update()
bpy.ops.render.render(write_still=True)
for obj in assets[1:]:
    obj.hide_render = False
print("CINDER_VISUAL_TARGET_READY", str(OUT), flush=True)
