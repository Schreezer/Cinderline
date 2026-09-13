#!/usr/bin/env python3
"""Render Cinderline HUD portraits from the authored VisualTarget FBX models.

Render with:
  /Applications/Blender.app/Contents/MacOS/Blender --background --threads 2 \
    --python scripts/create_unit_portraits.py

The script uses Cycles on CPU with two fixed worker threads. It renders each
model serially, writes source hashes and image metadata to the manifest, then
creates a labeled review contact sheet through the host Python/Pillow install.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "RawAssets" / "UI" / "Units"
SIZE = 384
PORTRAITS = (
    ("Worker", "Drudge", "RawAssets/VisualTarget/Units/Models/FBX/SM_Drudge.fbx"),
    ("Striker", "Ember", "RawAssets/VisualTarget/Units/Models/FBX/SM_Ember.fbx"),
    ("Lancer", "Needle", "RawAssets/VisualTarget/Units/Models/FBX/SM_Needle.fbx"),
    ("Scout", "Skim", "RawAssets/VisualTarget/Units/Models/FBX/SM_Skim.fbx"),
    ("Bastion", "Anvil", "RawAssets/VisualTarget/Units/Models/FBX/SM_Anvil.fbx"),
    ("Mortar", "Cinderthrow", "RawAssets/VisualTarget/Units/Models/FBX/SM_Cinderthrow.fbx"),
    ("Mender", "Mend", "RawAssets/VisualTarget/Units/Models/FBX/SM_Mend.fbx"),
    ("Kite", "Veil", "RawAssets/VisualTarget/Units/Models/FBX/SM_Veil.fbx"),
    ("Turret", "Ward", "RawAssets/VisualTarget/Models/FBX/SM_Ward.fbx"),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError("Cinderline unit portraits: " + message)


def create_contact_sheet() -> None:
    from PIL import Image, ImageDraw, ImageFont

    gutter = 12
    label_height = 30
    tile_height = SIZE + label_height
    sheet = Image.new("RGB", (3 * SIZE + 4 * gutter, 3 * tile_height + 4 * gutter), (7, 13, 22))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=17)
    for index, (_, display_name, _) in enumerate(PORTRAITS):
        image_path = OUTPUT / f"T_CinderPortrait_{display_name}.png"
        require(image_path.is_file(), f"missing rendered image {image_path}")
        image = Image.open(image_path).convert("RGB")
        require(image.size == (SIZE, SIZE), f"{image_path.name} is not {SIZE}x{SIZE}")
        column, row = index % 3, index // 3
        x = gutter + column * (SIZE + gutter)
        y = gutter + row * (tile_height + gutter)
        sheet.paste(image, (x, y))
        bounds = draw.textbbox((0, 0), display_name, font=font)
        width = bounds[2] - bounds[0]
        draw.text((x + (SIZE - width) / 2, y + SIZE + 5), display_name,
                  fill=(193, 224, 224), font=font)
    sheet.save(OUTPUT / "CinderPortrait_ContactSheet.png", optimize=True)


def render_portraits(selected_names: set[str] | None = None) -> None:
    import bpy
    from mathutils import Vector

    OUTPUT.mkdir(parents=True, exist_ok=True)

    def make_material(name, color, metallic, roughness, emission=None, emission_strength=0.0):
        material = bpy.data.materials.new(name)
        material.use_nodes = True
        shader = material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Base Color"].default_value = (*color, 1.0)
        shader.inputs["Metallic"].default_value = metallic
        shader.inputs["Roughness"].default_value = roughness
        if emission is not None:
            shader.inputs["Emission Color"].default_value = (*emission, 1.0)
            shader.inputs["Emission Strength"].default_value = emission_strength
        return material

    def point_at(obj, target):
        obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()

    def clear_scene():
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)
        for datablocks in (bpy.data.meshes, bpy.data.curves, bpy.data.cameras, bpy.data.lights,
                           bpy.data.materials, bpy.data.images):
            for datablock in list(datablocks):
                if datablock.users == 0:
                    datablocks.remove(datablock)

    def configure_scene():
        scene = bpy.context.scene
        scene.render.engine = "CYCLES"
        scene.cycles.device = "CPU"
        scene.cycles.samples = 32
        scene.cycles.use_denoising = True
        scene.render.resolution_x = SIZE
        scene.render.resolution_y = SIZE
        scene.render.resolution_percentage = 100
        scene.render.image_settings.file_format = "PNG"
        scene.render.image_settings.color_mode = "RGBA"
        scene.render.image_settings.color_depth = "8"
        scene.render.film_transparent = False
        scene.render.use_file_extension = True
        scene.render.threads_mode = "FIXED"
        scene.render.threads = 2
        scene.render.image_settings.color_management = "FOLLOW_SCENE"
        scene.view_settings.look = "AgX - Medium High Contrast"
        scene.render.resolution_percentage = 100
        scene.world.color = (0.004, 0.009, 0.016)
        world_shader = scene.world.node_tree.nodes.get("Background") if scene.world.use_nodes else None
        if world_shader is None:
            scene.world.use_nodes = True
            world_shader = scene.world.node_tree.nodes.get("Background")
        world_shader.inputs["Color"].default_value = (0.004, 0.009, 0.016, 1.0)
        world_shader.inputs["Strength"].default_value = 0.22
        return scene

    def add_area(name, location, color, energy, size, target=(0, 0, 0.8)):
        data = bpy.data.lights.new(name, "AREA")
        data.color = color
        data.energy = energy
        data.shape = "DISK"
        data.size = size
        obj = bpy.data.objects.new(name, data)
        bpy.context.collection.objects.link(obj)
        obj.location = location
        point_at(obj, target)

    manifest_path = OUTPUT / "manifest.json"
    previous_rows = {}
    if selected_names and manifest_path.is_file():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        previous_rows = {row["display_name"]: row for row in previous.get("assets", [])}
    manifest_rows = []
    for kind, display_name, relative_source in PORTRAITS:
        if selected_names and display_name not in selected_names:
            require(display_name in previous_rows, f"partial render has no existing record for {display_name}")
            manifest_rows.append(previous_rows[display_name])
            continue
        clear_scene()
        scene = configure_scene()
        source = ROOT / relative_source
        require(source.is_file(), f"missing authored model {source}")
        before = set(bpy.context.scene.objects)
        result = bpy.ops.import_scene.fbx(filepath=str(source), use_image_search=False)
        require("FINISHED" in result, f"Blender could not import {source.name}")
        models = [obj for obj in bpy.context.scene.objects if obj not in before and obj.type == "MESH"]
        require(len(models) == 1, f"{source.name} imported as {len(models)} mesh objects instead of one")
        model = models[0]

        palette = {
            "HullDark": make_material("Portrait_HullDark", (0.025, 0.055, 0.075), 0.78, 0.25),
            "HullLight": make_material("Portrait_HullLight", (0.24, 0.39, 0.42), 0.62, 0.22),
            "Metal": make_material("Portrait_Metal", (0.075, 0.13, 0.16), 0.92, 0.18),
            "TeamPanel": make_material("Portrait_TeamPanel", (0.018, 0.48, 0.42), 0.44, 0.20,
                                        (0.02, 0.70, 0.60), 1.4),
            "CoreGlow": make_material("Portrait_CoreGlow", (0.08, 0.78, 0.66), 0.16, 0.18,
                                       (0.08, 1.0, 0.80), 6.0),
        }
        require(tuple(slot.name for slot in model.data.materials) == tuple(palette),
                f"{source.name} material slots changed: {[slot.name for slot in model.data.materials]}")
        for slot_index, slot_name in enumerate(palette):
            model.data.materials[slot_index] = palette[slot_name]

        # Preserve the authored +X front, but normalize card occupancy so light
        # scouts and heavy vehicles read at the same small HUD size.
        dimensions = model.dimensions.copy()
        scale = 2.35 / max(dimensions)
        model.scale *= scale
        bpy.context.view_layer.update()
        corners = [model.matrix_world @ Vector(corner) for corner in model.bound_box]
        minimum = Vector((min(point.x for point in corners), min(point.y for point in corners), min(point.z for point in corners)))
        maximum = Vector((max(point.x for point in corners), max(point.y for point in corners), max(point.z for point in corners)))
        model.location += Vector((-(minimum.x + maximum.x) * 0.5,
                                  -(minimum.y + maximum.y) * 0.5,
                                  0.10 - minimum.z))
        bpy.context.view_layer.update()
        corners = [model.matrix_world @ Vector(corner) for corner in model.bound_box]
        minimum = Vector((min(point.x for point in corners), min(point.y for point in corners), min(point.z for point in corners)))
        maximum = Vector((max(point.x for point in corners), max(point.y for point in corners), max(point.z for point in corners)))
        center = (minimum + maximum) * 0.5

        floor = make_material("Portrait_Backdrop", (0.009, 0.021, 0.034), 0.15, 0.46)
        bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, 0))
        bpy.context.object.data.materials.append(floor)

        bpy.ops.object.camera_add(location=(4.4, -6.6, 3.5))
        camera = bpy.context.object
        camera.data.type = "ORTHO"
        camera.data.lens = 58
        camera.data.dof.use_dof = False
        point_at(camera, center)
        scene.camera = camera

        # Fit every authored vertex, not only the object bounds. Center the
        # projected silhouette precisely before applying an eight-percent safe
        # border on each edge of the limiting dimension.
        bpy.context.view_layer.update()
        vertices = [model.matrix_world @ vertex.co for vertex in model.data.vertices]
        require(vertices, f"{source.name} has no renderable vertices")
        inverse_camera = camera.matrix_world.inverted()
        camera_points = [inverse_camera @ point for point in vertices]
        midpoint_x = (min(point.x for point in camera_points) + max(point.x for point in camera_points)) * 0.5
        midpoint_y = (min(point.y for point in camera_points) + max(point.y for point in camera_points)) * 0.5
        camera.location += camera.matrix_world.to_3x3() @ Vector((midpoint_x, midpoint_y, 0.0))
        bpy.context.view_layer.update()
        inverse_camera = camera.matrix_world.inverted()
        camera_points = [inverse_camera @ point for point in vertices]
        horizontal = max(point.x for point in camera_points) - min(point.x for point in camera_points)
        vertical = max(point.y for point in camera_points) - min(point.y for point in camera_points)
        safe_occupancy = 0.84
        camera.data.ortho_scale = max(horizontal, vertical) / safe_occupancy

        # Screen-space verification is part of generation so later model edits
        # cannot silently crop a crest, weapon, wing, or foot.
        screen_x = [0.5 + point.x / camera.data.ortho_scale for point in camera_points]
        screen_y = [0.5 + point.y / camera.data.ortho_scale for point in camera_points]
        projected_bounds = [min(screen_x), min(screen_y), max(screen_x), max(screen_y)]
        epsilon = 1e-5
        require(all(epsilon <= value <= 1.0 - epsilon for value in projected_bounds),
                f"{display_name} projected vertices leave the image: {projected_bounds}")
        require(projected_bounds[0] >= 0.08 - epsilon and projected_bounds[1] >= 0.08 - epsilon
                and projected_bounds[2] <= 0.92 + epsilon and projected_bounds[3] <= 0.92 + epsilon,
                f"{display_name} does not retain the eight-percent safe border: {projected_bounds}")

        add_area("Warm key", (4.2, -4.2, 6.2), (1.0, 0.43, 0.20), 760, 3.6, center)
        add_area("Cool rim", (-4.0, 3.2, 4.6), (0.04, 0.66, 1.0), 1050, 3.0, center)
        add_area("Soft fill", (0.0, -4.5, 2.1), (0.30, 0.67, 0.78), 310, 4.0, center)

        output = OUTPUT / f"T_CinderPortrait_{display_name}.png"
        scene.render.filepath = str(output)
        bpy.ops.render.render(write_still=True)
        require(output.is_file() and output.stat().st_size > 4096, f"render failed for {display_name}")
        manifest_rows.append({
            "kind": kind,
            "display_name": display_name,
            "source_fbx": relative_source,
            "source_sha256": sha256(source),
            "texture": str(output.relative_to(ROOT)),
            "texture_sha256": sha256(output),
            "size": [SIZE, SIZE],
            "unreal_asset": f"/Game/Art/UI/Units/T_CinderPortrait_{display_name}",
            "army_ribbon": kind not in ("Worker", "Turret"),
            "projected_vertex_bounds": [round(value, 6) for value in projected_bounds],
        })

    manifest = {
        "version": 1,
        "render": {
            "application": "Blender",
            "version": bpy.app.version_string,
            "engine": "CYCLES_CPU",
            "serial": True,
            "thread_limit": 2,
            "resolution": [SIZE, SIZE],
            "composition": "+X-front three-quarter orthographic",
        },
        "assets": manifest_rows,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    host_python = os.environ.get("CINDER_HOST_PYTHON", "/usr/bin/env")
    command = ([host_python, "python3"] if host_python.endswith("env") else [host_python])
    subprocess.run(command + [str(Path(__file__).resolve()), "--contact-sheet"], check=True)
    print(f"CINDERLINE_UNIT_PORTRAITS_COMPLETE count={len(manifest_rows)} output={OUTPUT}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contact-sheet", action="store_true")
    parser.add_argument("--names", nargs="*")
    args, _ = parser.parse_known_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
    if args.contact_sheet:
        create_contact_sheet()
    else:
        selected = set(args.names) if args.names else None
        if selected:
            known = {display_name for _, display_name, _ in PORTRAITS}
            require(selected <= known, "unknown portrait names: " + ", ".join(sorted(selected - known)))
        render_portraits(selected)


if __name__ == "__main__":
    main()
