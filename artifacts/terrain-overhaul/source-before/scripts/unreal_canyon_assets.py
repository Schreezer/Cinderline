"""Import and verify Cinderline's authored canyon module kit in Unreal.

Run from UnrealEditor-Cmd only after scripts/create_canyon_assets.py succeeds.
Set CINDER_REIMPORT_CANYON=1 to replace assets previously owned by this script.
This importer creates presentation assets only: it does not edit maps, actors,
simulation obstacles, or collision.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import math
import os
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "RawAssets/Canyon"
MANIFEST = SOURCE / "manifest.json"
MESHES = "/Game/Art/Canyon/Meshes"
MATERIALS = "/Game/Art/Canyon/Materials"
MATERIAL_PATH = MATERIALS + "/M_CinderCanyonRock"
REPORT = SOURCE / "unreal-import-results.json"
SUCCESS = SOURCE / "unreal-import.success"
OWNER = "scripts/unreal_canyon_assets.py"
VERSION = "cinder-canyon-v1"
REQUIRED = {f"SM_CinderCanyon_Rock_{letter}" for letter in "ABCDEF"} | {
    "SM_CinderCanyon_CliffMass", "SM_CinderCanyon_Debris"
}
# Ambient scatter chips are OPTIONAL, and deliberately so. They ship a single LOD
# because at an 8-30 cm footprint they cull long before a second level could pay
# for itself, which is why the LOD chain below is validated against each record's
# own length rather than a constant 3. They are optional because the runtime
# already degrades per role to the debris mesh when a chip is absent, so a
# manifest authored before the chips existed must still import cleanly instead of
# failing the whole canyon kit over a prop variant.
OPTIONAL = {f"SM_CinderCanyon_Chip_{letter}" for letter in "ABCD"}
EXPECTED = REQUIRED | OPTIONAL


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline canyon: " + message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(asset, key):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + key))


def tag(asset, key, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Cinderline." + key, str(value))


def load_manifest():
    require(MANIFEST.is_file(), "missing " + str(MANIFEST))
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("unit") == "centimeter", "manifest units changed")
    require(manifest.get("forward_axis") == "+X" and manifest.get("up_axis") == "+Z",
            "manifest axes changed")
    require(manifest.get("origin") == "bottom center; bounds_min_z_cm is exactly 0",
            "manifest origin contract changed")
    require(manifest.get("collision") == "none; authoritative simulation obstacles remain unchanged",
            "manifest collision contract changed")
    require(manifest.get("nanite") is False, "manifest must disable Nanite")
    require(manifest.get("material", {}).get("unreal_path") == MATERIAL_PATH,
            "manifest material path changed")
    records = manifest.get("assets", [])
    names = [record.get("name") for record in records]
    require(set(names) >= REQUIRED and set(names) <= EXPECTED and len(names) == len(set(names)),
            "manifest asset set changed")
    for record in records:
        name = record["name"]
        require(record.get("material_slot") == "M_CinderCanyonRock",
                name + " must use the shared canyon material slot")
        lods = record.get("lods", [])
        require([row.get("lod") for row in lods] == ([0] if "_Chip_" in name else [0, 1, 2]),
                name + " must have explicit LODs")
        triangles = []
        base_size = lods[0].get("size_cm", [])
        for lod in lods:
            relative = Path(lod.get("file", ""))
            source = (ROOT / relative).resolve()
            require(source.parent == (SOURCE / "FBX").resolve() and source.suffix.lower() == ".fbx",
                    name + " LOD source escaped the pack")
            require(source.is_file() and sha256(source) == lod.get("sha256"),
                    name + " LOD{} source hash differs".format(lod["lod"]))
            bounds_min, bounds_max = lod.get("bounds_min_cm", []), lod.get("bounds_max_cm", [])
            size = lod.get("size_cm", [])
            require(len(bounds_min) == len(bounds_max) == len(size) == 3
                    and all(math.isfinite(value) for value in bounds_min + bounds_max + size)
                    and all(value > 0 for value in size), name + " has invalid bounds")
            require(abs(bounds_min[2]) <= 0.001
                    and abs(bounds_min[0] + bounds_max[0]) <= 0.002
                    and abs(bounds_min[1] + bounds_max[1]) <= 0.002,
                    name + " is not bottom centered")
            require(max(size[0], size[1]) <= 100.002,
                    name + " exceeds the normalized horizontal box")
            require(all(abs(size[axis] - base_size[axis]) <= 0.002 for axis in range(3)),
                    name + " LOD bounds drifted")
            require(lod.get("material_sections") == 1 and lod.get("uv_channels", 0) >= 1,
                    name + " must have one material section and UV0")
            roundtrip = lod.get("fbx_roundtrip", {})
            require(roundtrip.get("verified") is True
                    and roundtrip.get("triangles") == lod.get("triangles")
                    and roundtrip.get("material_sections") == 1
                    and roundtrip.get("uv_channels", 0) >= 1,
                    name + " lacks FBX round-trip verification")
            triangles.append(lod.get("triangles"))
            lod["_source"] = source
        require(all(isinstance(value, int) and value > 0 for value in triangles)
                and (len(triangles) == 1 or triangles[0] > triangles[1] > triangles[2]),
                name + " LOD triangle order is invalid")
        if "_Rock_" in name:
            require(800 <= triangles[0] <= 2000, name + " LOD0 exceeds the cliff budget")
        if "_Chip_" in name:
            require(triangles[0] <= 96, name + " LOD0 exceeds the scatter chip budget")
    return manifest, records


def create_material():
    helper_path = ROOT / "scripts/unreal_visual_upgrade.py"
    require(helper_path.is_file(), "missing material graph helper " + str(helper_path))
    spec = importlib.util.spec_from_file_location("cinder_canyon_material_graph", helper_path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.OWNER = OWNER
    helper.VERSION = VERSION
    helper.MATERIALS = MATERIALS
    graph = helper.Graph("M_CinderCanyonRock")

    color_path = "/Game/Art/Textures/VisualUpgrade/T_CinderGroundV2_Color"
    normal_path = "/Game/Art/Textures/VisualUpgrade/T_CinderGroundV2_Normal"
    color_texture = unreal.load_asset(color_path)
    normal_texture = unreal.load_asset(normal_path)
    require(isinstance(color_texture, unreal.Texture2D), "missing licensed ground color " + color_path)
    require(isinstance(normal_texture, unreal.Texture2D), "missing licensed ground normal " + normal_path)
    require(metadata(color_texture, "SourceLicense") == "CC0-1.0"
            and metadata(normal_texture, "SourceLicense") == "CC0-1.0",
            "canyon detail textures must retain their CC0 source metadata")
    # Two-projection world mapping, replacing authored UV0 at 2x tiling. The kit
    # meshes are normalized into a 100 cm box and then instanced at non-uniform
    # scale, so an authored UV stretched texel density differently on every rock
    # and the same texture read coarse on a tall cliff and fine on a low boulder.
    # M_VT_SceneryRock fixes this with three full projections at twelve fetches,
    # which is twice the mobile budget. On a canyon wall only two projections
    # carry information: world XY across the top face, and world Z strata up the
    # sides. Four fetches total - albedo and normal, once per projection - and no
    # new sampler slots, because both projections read through the shared world
    # wrap group. Everything between here and the samples is ALU.
    world = graph.node("Canyon world position", "WorldPosition")
    top_xy = graph.node("Top facing world XY", "ComponentMask",
                        r=True, g=True, b=False, a=False)
    graph.link(world, top_xy, "Input")
    top_uv = graph.node("Top projection at 246 cm", "Multiply", const_b=1.0 / helper.TILE_CM)
    graph.link(top_xy, top_uv, "A")
    world_x = graph.node("World X coordinate", "ComponentMask",
                         r=True, g=False, b=False, a=False)
    world_y = graph.node("World Y coordinate", "ComponentMask",
                         r=False, g=True, b=False, a=False)
    world_z = graph.node("World Z coordinate", "ComponentMask",
                         r=False, g=False, b=True, a=False)
    graph.link(world, world_x, "Input")
    graph.link(world, world_y, "Input")
    graph.link(world, world_z, "Input")

    normal_ws = graph.node("World vertex normal", "VertexNormalWS")
    absolute_normal = graph.node("Absolute world normal", "Abs")
    graph.link(normal_ws, absolute_normal, "Input")
    normal_x = graph.node("Normal X magnitude", "ComponentMask",
                          r=True, g=False, b=False, a=False)
    normal_y = graph.node("Normal Y magnitude", "ComponentMask",
                          r=False, g=True, b=False, a=False)
    normal_z = graph.node("Normal Z magnitude", "ComponentMask",
                          r=False, g=False, b=True, a=False)
    graph.link(absolute_normal, normal_x, "Input")
    graph.link(absolute_normal, normal_y, "Input")
    graph.link(absolute_normal, normal_z, "Input")

    # Which horizontal axis runs along the wall: an X-facing wall is read along
    # world Y, and the reverse. Gain 10 around the 45 degree crossover makes the
    # switch effectively hard - the interpolated band is about six degrees wide,
    # and at the crossover the two coordinates are equal anyway, so the smear has
    # nothing to smear. Branch-free on purpose: no shader permutation, no
    # dynamic flow control on a mobile pixel shader.
    facing_difference = graph.node("X over Y facing difference", "Subtract")
    graph.link(normal_x, facing_difference, "A")
    graph.link(normal_y, facing_difference, "B")
    facing_gain = graph.node("Sharpened facing difference", "Multiply", const_b=10.0)
    graph.link(facing_difference, facing_gain, "A")
    facing_centered = graph.node("Centered facing selector", "Add", const_b=0.5)
    graph.link(facing_gain, facing_centered, "A")
    facing_select = graph.node("Saturated facing selector", "Clamp",
                               min_default=0.0, max_default=1.0)
    graph.link(facing_centered, facing_select, "Input")
    side_horizontal = graph.node("Side horizontal coordinate", "LinearInterpolate")
    graph.link(world_x, side_horizontal, "A")
    graph.link(world_y, side_horizontal, "B")
    graph.link(facing_select, side_horizontal, "Alpha")
    side_xz = graph.node("Side facing world coordinates", "AppendVector")
    graph.link(side_horizontal, side_xz, "A")
    graph.link(world_z, side_xz, "B")
    side_uv = graph.node("Side projection at 246 cm", "Multiply", const_b=1.0 / helper.TILE_CM)
    graph.link(side_xz, side_uv, "A")

    # Top-projection weight. Cubing the up component holds a wall fully on the
    # strata projection until it is well past 45 degrees, so the cross-faded band
    # lands on the roundovers between face and cap, where a blend is invisible.
    # Clamped because interpolated vertex normals are not exactly unit length.
    top_cubed = graph.node("Cubed up facing", "Power", const_exponent=3.0)
    graph.link(normal_z, top_cubed, "Base")
    top_weight = graph.node("Saturated top weight", "Clamp", min_default=0.0, max_default=1.0)
    graph.link(top_cubed, top_weight, "Input")

    authored = graph.node("Authored strata, iron, and cavity masks", "VertexColor")
    # Rock is the frame's mid-tone mass, not its subject. The previous palette ran
    # 0.23 -> 0.58 in red at roughly 0.87 saturation, which was tuned against a
    # 5.0-intensity sun and a near-neutral grade; under the 8.5 key and the wider
    # saturation/contrast grade it resolved as neon orange traffic cones, and the
    # 5:1 shadow-to-sandstone ratio turned the authored strata into hard stripes
    # rather than bedding. These sit about 45% darker at roughly 0.55 saturation
    # and a 2.7:1 strata ratio, which keeps the cliffs reading as mass, holds
    # them below the sand they stand on, and leaves the blue-steel hulls and cyan
    # cores as the only saturated things on screen.
    shadow = graph.color("CanyonShadow", (0.178, 0.127, 0.099))
    sandstone = graph.color("CanyonSandstone", (0.315, 0.205, 0.140))
    strata = graph.node("Stratified sandstone color", "LinearInterpolate")
    graph.link(shadow, strata, "A")
    graph.link(sandstone, strata, "B")
    graph.link(authored, strata, "Alpha", "R")
    iron = graph.color("IronOxide", (0.255, 0.130, 0.080))
    stained = graph.node("Warm iron staining", "LinearInterpolate")
    graph.link(strata, stained, "A")
    graph.link(iron, stained, "B")
    graph.link(authored, stained, "Alpha", "G")

    # Sun-bleached caps. The authored strata mask puts its darkest band on the
    # crown, which was authored as a dark coronet and reads as one on a low
    # boulder — but on the taller mesas the heightfield now produces, the cap is
    # both the largest visible face and the one most square to a -46 degree key,
    # so leaving it at the bottom of the strata ramp punched black holes in the
    # skyline. Weathering agrees with the lighting here: horizontal rock bleaches
    # and collects dust while vertical faces keep their bedding. Reuses the
    # top_weight already computed for the projection blend, so this is one lerp
    # of ALU, no extra fetch and no extra sampler, and it hands every cliff a
    # light top edge against its own dark sides — free silhouette separation that
    # survives the thermal ladder zeroing shadows. Kept only about a tenth above
    # the lit strata rather than the half-stop that first read well in isolation:
    # the fog plane sits low, so a mesa tall enough to clear it shows only its cap
    # over unexplored ground, and a brighter crown made those caps glow out of the
    # dark instead of reading as remembered terrain beyond the front.
    cap = graph.color("CanyonCap", (0.352, 0.268, 0.199))
    crowned = graph.node("Sun bleached cap", "LinearInterpolate")
    graph.link(stained, crowned, "A")
    graph.link(cap, crowned, "B")
    graph.link(top_weight, crowned, "Alpha")

    # Reuse the existing licensed gravel photograph as restrained grayscale
    # sediment grain. The authored vertex masks remain the dominant strata and
    # iron color, while this modulation breaks up broad low-poly faces.
    grain_side = graph.sample("Licensed sediment grain", color_texture, side_uv, "COLOR",
                              sampler_source=helper.WRAP_SAMPLER_GROUP)
    grain_top = graph.sample("Licensed sediment grain top", color_texture, top_uv, "COLOR",
                             sampler_source=helper.WRAP_SAMPLER_GROUP)
    # Blend the two samples, not the two UVs. Cross-fading coordinates instead
    # would drag one lookup across a large fraction of a tile inside the blend
    # band and smear the grain into streaks.
    grain = graph.node("Projection blended sediment grain", "LinearInterpolate")
    graph.link(grain_side, grain, "A", "RGB")
    graph.link(grain_top, grain, "B", "RGB")
    graph.link(top_weight, grain, "Alpha")
    grayscale = graph.node("Neutral sediment luminance", "Desaturation")
    graph.link(grain, grayscale, "Input")
    graph.link(graph.node("Full grain desaturation", "Constant", r=1.0), grayscale, "Fraction")
    grain_gain = graph.node("Restrained grain contrast", "Multiply", const_b=0.72)
    graph.link(grayscale, grain_gain, "A")
    grain_bias = graph.node("Centered grain value", "Add", const_b=0.64)
    graph.link(grain_gain, grain_bias, "A")
    bounded_grain = graph.node("Bounded sediment modulation", "Clamp",
                               min_default=0.78, max_default=1.20)
    graph.link(grain_bias, bounded_grain, "Input")
    detailed_color = graph.node("Strata with sediment grain", "Multiply")
    graph.link(crowned, detailed_color, "A")
    graph.link(bounded_grain, detailed_color, "B")

    # Fog gating, exactly as M_CinderCanyonGround does it, because the flat
    # gameplay fog plane sits a few centimetres above the ground and cannot
    # darken anything taller than itself. That was survivable while the kit
    # topped out around two metres; with the heightfield now reaching 480 cm the
    # cliffs stand well clear of the plane and, unfogged, a remembered mesa lit
    # its own cap out of otherwise black unexplored ground.
    #
    # One extra fetch, taking this material to five against a mobile ground
    # budget of six, and it shares FogMask's own clamped sampler rather than the
    # world wrap group — wrapping this one would tile the fog at the map edge.
    # The default asset reads as fully explored, so a batch whose material never
    # receives the runtime mask renders exactly as it does today.
    fog_texture = unreal.load_asset("/Game/Art/Textures/VisualUpgrade/T_CinderFogDefaultV2")
    require(isinstance(fog_texture, unreal.Texture2D), "missing fog default texture for canyon rock")
    fog_uv = graph.world_uv(prefix="Canyon fog ")
    fog_sample = graph.sample("FogMask", fog_texture, fog_uv, "LINEAR_COLOR")
    visible = graph.node("Known visible rock fraction", "OneMinus")
    graph.link(fog_sample, visible, "Input", "R")
    visible_color = graph.node("Fog gated rock diffuse", "Multiply")
    graph.link(detailed_color, visible_color, "A")
    graph.link(visible, visible_color, "B")
    graph.output(visible_color, "BASE_COLOR")
    # The fog tint is emissive so an unexplored cliff matches the fogged ground
    # under any light, instead of going black and reading as a hole in the map.
    rock_fog_color = graph.node("Unknown and explored rock fog", "LinearInterpolate")
    graph.link(graph.color("UnknownFog", (0.010, 0.017, 0.027)), rock_fog_color, "A", "RGB")
    graph.link(graph.color("ExploredFog", (0.027, 0.041, 0.049)), rock_fog_color, "B", "RGB")
    graph.link(fog_sample, rock_fog_color, "Alpha", "G")
    rock_fog_emission = graph.node("Rock fog independent of lighting", "Multiply")
    graph.link(rock_fog_color, rock_fog_emission, "A")
    graph.link(fog_sample, rock_fog_emission, "B", "R")
    graph.output(rock_fog_emission, "EMISSIVE_COLOR")

    normal_side = graph.sample("Restrained sediment normal", normal_texture, side_uv, "NORMAL",
                               sampler_source=helper.WRAP_SAMPLER_GROUP)
    normal_top = graph.sample("Restrained sediment normal top", normal_texture, top_uv, "NORMAL",
                              sampler_source=helper.WRAP_SAMPLER_GROUP)
    projected_normal = graph.node("Projection blended sediment normal", "LinearInterpolate")
    graph.link(normal_side, projected_normal, "A", "RGB")
    graph.link(normal_top, projected_normal, "B", "RGB")
    graph.link(top_weight, projected_normal, "Alpha")
    # Graph.normal() pulls an RGB pin straight off a texture sample, so it cannot
    # take a blend node; the restrained-detail chain is inlined here under its own
    # labels. A plain lerp of two tangent-space samples rather than a reoriented
    # triplanar blend is correct for what this is: at NormalStrength 0.42 it is
    # surface grain over authored vertex strata, not a physical normal, and
    # whiteout blending would cost ALU the silhouette never shows.
    flat_normal = graph.node("Flat sediment normal", "Constant3Vector",
                             constant=unreal.LinearColor(0, 0, 1, 0))
    restrained_normal = graph.node("Restrained sediment detail", "LinearInterpolate")
    graph.link(flat_normal, restrained_normal, "A")
    graph.link(projected_normal, restrained_normal, "B")
    graph.link(graph.scalar("NormalStrength", 0.42), restrained_normal, "Alpha")
    normalized_normal = graph.node("Normalized sediment normal", "Normalize")
    graph.link(restrained_normal, normalized_normal, "VectorInput")
    graph.output(normalized_normal, "NORMAL")

    rough = graph.node("Layered roughness", "LinearInterpolate", const_a=0.95, const_b=0.82)
    graph.link(authored, rough, "Alpha", "R")
    graph.output(rough, "ROUGHNESS")
    graph.output(authored, "AMBIENT_OCCLUSION", "B")
    graph.output(graph.node("Nonmetal sandstone", "Constant", r=0.0), "METALLIC")
    graph.output(graph.node("Low sandstone specular", "Constant", r=0.08), "SPECULAR")
    result = graph.finish()
    result["licensed_surface_detail"] = {
        "source": "Poly Haven gravel_floor_04",
        "license": "CC0-1.0",
        "textures": [color_path, normal_path],
        "texture_samples": 4,
        "mapping": "two world projections: XY top face, horizontal-plus-Z sides",
        "projection": {
            "tile_width_cm": 246.0,
            "side_axis_selector": "saturate((|N.x| - |N.y|) * 10 + 0.5)",
            "top_blend": "saturate(|N.z| ^ 3)",
            "blended": "texture samples, not coordinates",
            "stable_under_nonuniform_instance_scale": True},
        "albedo_modulation": [0.78, 1.20],
        "normal_strength": 0.42,
        "specular": 0.08,
    }
    material = graph.material
    tag(material, "CanyonOwner", OWNER)
    tag(material, "SourceLicense", "original Cinderline procedural material")
    tag(material, "MaterialContract",
        "vertex R strata, G iron stain, B cavity; CC0 sediment grain and normal on two "
        "world projections; rough low-spec nonmetal")
    require(unreal.EditorAssetLibrary.save_loaded_asset(material), "could not save " + MATERIAL_PATH)
    return material, result


def import_options():
    options = unreal.FbxImportUI()
    for key, value in (
        ("automated_import_should_detect_type", False),
        ("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH),
        ("import_as_skeletal", False), ("import_mesh", True),
        ("import_animations", False), ("import_materials", False),
        ("import_textures", False), ("create_physics_asset", False),
        ("override_full_name", True)):
        options.set_editor_property(key, value)
    data = options.get_editor_property("static_mesh_import_data")
    require(data is not None, "FBX static mesh import settings are unavailable")
    for key, value in (
        ("combine_meshes", True), ("convert_scene", True), ("convert_scene_unit", True),
        ("force_front_x_axis", False), ("transform_vertex_to_absolute", True),
        ("bake_pivot_in_vertex", False), ("reorder_material_to_fbx_order", True),
        ("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS),
        # M_CinderCanyonRock reads the authored colors as strata (R), iron staining
        # (G) and cavity occlusion (B), so importing without them silently produces
        # flat rock. Left unset this followed the factory default, which on a
        # REIMPORT preserves whatever the asset already had instead of taking the
        # file's — the path that only runs with CINDER_REIMPORT_CANYON=1 and so had
        # never been exercised. State it explicitly for both paths.
        ("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE),
        ("auto_generate_collision", False), ("generate_lightmap_u_vs", False),
        ("build_nanite", False), ("remove_degenerates", True)):
        data.set_editor_property(key, value)
    return options


def import_lod0(record, replace):
    lod = record["lods"][0]
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(lod["_source"]), "destination_path": MESHES,
        "destination_name": record["name"], "automated": True, "async_": False,
        "save": False, "replace_existing": replace, "replace_existing_settings": replace,
        "factory": unreal.FbxFactory(), "options": import_options()}.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    expected_path = MESHES + "/" + record["name"] + "." + record["name"]
    meshes = [asset for asset in task.get_objects()
              if isinstance(asset, unreal.StaticMesh) and asset.get_path_name() == expected_path]
    require(len(meshes) == 1, record["name"] + " did not import as one exact static mesh")
    return meshes[0]


def call_lod_api(subsystem, method, mesh, *args):
    candidate = getattr(subsystem, method, None)
    if candidate is not None:
        return candidate(mesh, *args)
    legacy = getattr(unreal, "EditorStaticMeshLibrary", None)
    candidate = getattr(legacy, method, None) if legacy is not None else None
    require(candidate is not None, "engine exposes no " + method + " API")
    return candidate(mesh, *args)


def replace_source_lods(mesh, record, subsystem):
    if mesh.get_num_lods() > 1:
        call_lod_api(subsystem, "remove_lods", mesh)
    require(mesh.get_num_lods() == 1, record["name"] + " could not clear previous source LODs")
    for lod in record["lods"][1:]:
        result = call_lod_api(subsystem, "import_lod", mesh, lod["lod"], str(lod["_source"]))
        require(result is None or result is True or (isinstance(result, int) and result >= 0),
                record["name"] + " LOD{} import API failed".format(lod["lod"]))
    # Each record declares its own chain length: cliffs ship LOD0/1/2, scatter
    # chips ship LOD0 only because they cull long before a second level matters.
    lod_count = len(record["lods"])
    require(mesh.get_num_lods() == lod_count,
            record["name"] + " must contain exactly {} source LODs".format(lod_count))


def verify_mesh(mesh, record, material, subsystem):
    lod_count = len(record["lods"])
    require(mesh.get_num_lods() == lod_count,
            record["name"] + " must contain exactly {} LODs".format(lod_count))
    actual_triangles = [mesh.get_num_triangles(lod) for lod in range(lod_count)]
    expected_triangles = [lod["triangles"] for lod in record["lods"]]
    require(actual_triangles == expected_triangles,
            record["name"] + " imported triangle counts differ: " + repr(actual_triangles))
    require(all(mesh.get_num_sections(lod) == 1 for lod in range(lod_count)),
            record["name"] + " must keep one section in every LOD")
    require(len(mesh.get_editor_property("static_materials")) == 1,
            record["name"] + " must keep one shared material slot")
    mesh.set_material(0, material)
    require(mesh.get_material(0) == material, record["name"] + " material assignment failed")
    require(subsystem.has_vertex_colors(mesh), record["name"] + " lost its authored strata vertex colors")

    subsystem.remove_collisions(mesh)
    for lod in range(lod_count):
        subsystem.enable_section_collision(mesh, False, lod, 0)
    require(all(not subsystem.is_section_collision_enabled(mesh, lod, 0) for lod in range(lod_count)),
            record["name"] + " section collision could not be disabled")

    bounds = mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    dimensions = [extent.x * 2.0, extent.y * 2.0, extent.z * 2.0]
    expected_size = record["lods"][0]["size_cm"]
    require(all(abs(actual - expected) <= 0.5 for actual, expected in zip(dimensions, expected_size)),
            record["name"] + " imported dimensions differ: " + repr(dimensions))
    require(abs(origin.x) <= 0.5 and abs(origin.y) <= 0.5 and abs(origin.z - extent.z) <= 0.5,
            record["name"] + " lost its bottom-center pivot")
    require(subsystem.get_simple_collision_count(mesh) == 0,
            record["name"] + " unexpectedly has simple collision")
    nanite = mesh.get_editor_property("nanite_settings")
    require(not nanite.get_editor_property("enabled"), record["name"] + " unexpectedly enabled Nanite")
    return {
        "path": mesh.get_path_name(),
        "dimensions_cm": dimensions,
        "origin_cm": [origin.x, origin.y, origin.z],
        "triangles": actual_triangles,
        "sections": [mesh.get_num_sections(lod) for lod in range(lod_count)],
        "vertex_colors": True,
        "section_collision": [False] * lod_count,
        "simple_collisions": 0,
        "nanite": False,
        "material": material.get_path_name(),
    }


def run():
    SUCCESS.unlink(missing_ok=True)
    report = {"success": False, "utc": datetime.now(timezone.utc).isoformat(),
              "owner": OWNER, "version": VERSION, "destination": MESHES,
              "manifest_sha256": sha256(MANIFEST) if MANIFEST.is_file() else None,
              "assets": []}
    try:
        manifest, records = load_manifest()
        replace = os.environ.get("CINDER_REIMPORT_CANYON") == "1"
        unreal.EditorAssetLibrary.make_directory(MATERIALS)
        unreal.EditorAssetLibrary.make_directory(MESHES)
        material, report["material"] = create_material()
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        require(subsystem is not None, "StaticMeshEditorSubsystem is unavailable")
        for record in records:
            path = MESHES + "/" + record["name"]
            mesh = unreal.load_asset(path)
            if mesh is not None:
                require(isinstance(mesh, unreal.StaticMesh), "unrelated asset occupies " + path)
                require(metadata(mesh, "CanyonOwner") == OWNER,
                        "refusing to replace asset owned by " + metadata(mesh, "CanyonOwner"))
                hashes_match = all(metadata(mesh, "SourceSha256LOD" + str(lod["lod"])) == lod["sha256"]
                                   for lod in record["lods"])
                require(replace or hashes_match,
                        record["name"] + " sources changed; set CINDER_REIMPORT_CANYON=1")
            if mesh is None or replace:
                mesh = import_lod0(record, mesh is not None)
                replace_source_lods(mesh, record, subsystem)
            result = verify_mesh(mesh, record, material, subsystem)
            tag(mesh, "CanyonOwner", OWNER)
            tag(mesh, "VisualVersion", VERSION)
            tag(mesh, "SourceLicense", manifest["license"])
            tag(mesh, "GeometryContract", "cm,+X,+Z,bottom-center,box-contained,three-source-LODs")
            tag(mesh, "Collision", "none")
            tag(mesh, "Nanite", "false")
            for lod in record["lods"]:
                tag(mesh, "SourceSha256LOD" + str(lod["lod"]), lod["sha256"])
            require(unreal.EditorAssetLibrary.save_loaded_asset(mesh), "could not save " + path)
            result["source_sha256"] = [lod["sha256"] for lod in record["lods"]]
            report["assets"].append(result)
        report["success"] = True
        SUCCESS.write_text("ok\n", encoding="utf-8")
        unreal.log("CINDERLINE_CANYON_IMPORT_OK: " + str(REPORT))
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        REPORT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    run()
