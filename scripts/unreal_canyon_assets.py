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
from contextlib import contextmanager
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
VERSION = "cinder-canyon-v3"
ROCK_PALETTE = {
    "CanyonShadow": (0.065, 0.082, 0.078),
    "CanyonSandstone": (0.135, 0.150, 0.120),
    "CanyonCap": (0.160, 0.170, 0.135),
    "IronOxide": (0.105, 0.090, 0.065),
}
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
    color_texture = unreal.load_asset(color_path)
    require(isinstance(color_texture, unreal.Texture2D), "missing licensed ground color " + color_path)
    require(metadata(color_texture, "SourceLicense") == "CC0-1.0",
            "canyon detail texture must retain its CC0 source metadata")

    # Fractured faces, not painted horizontal strata, supply the major forms.
    # The kit's R is a non-periodic face tone, G is sparse weathering, and B is
    # cavity visibility. Preserve this variation through the cap as well: the
    # previous up-facing lerp replaced every top with one flat cream color.
    authored = graph.node("Authored face tone, weathering, and cavity", "VertexColor")
    shadow = graph.color("CanyonShadow", ROCK_PALETTE["CanyonShadow"])
    stone = graph.color("CanyonSandstone", ROCK_PALETTE["CanyonSandstone"])
    face_color = graph.node("Broad authored rock face tone", "LinearInterpolate")
    graph.link(shadow, face_color, "A", "RGB")
    graph.link(stone, face_color, "B", "RGB")
    graph.link(authored, face_color, "Alpha", "R")

    # Blend three ALBEDO samples, never world coordinates. The former X/Y
    # coordinate lerp stretched the texture on diagonal faces: equal |Nx|/|Ny|
    # does not imply equal world X/Y. Full triplanar color now costs three
    # fetches, while removing both mismatched tangent-normal fetches saves two.
    world = graph.node("Canyon world position", "WorldPosition")
    normal_ws = graph.node("World vertex normal", "VertexNormalWS")
    absolute_normal = graph.node("Absolute world normal", "Abs")
    graph.link(normal_ws, absolute_normal, "Input")
    normal_power = graph.node("Sharp rock projection weights", "Power", const_exponent=4.0)
    graph.link(absolute_normal, normal_power, "Base")
    weights = {}
    for axis, channel in (("X", "R"), ("Y", "G"), ("Z", "B")):
        weight = graph.node(axis + " projection magnitude", "ComponentMask",
                            r=channel == "R", g=channel == "G", b=channel == "B", a=False)
        graph.link(normal_power, weight, "Input")
        weights[axis] = weight
    xy_sum = graph.node("Horizontal projection weight sum", "Add")
    graph.link(weights["X"], xy_sum, "A")
    graph.link(weights["Y"], xy_sum, "B")
    weight_sum = graph.node("Rock projection weight sum", "Add")
    graph.link(xy_sum, weight_sum, "A")
    graph.link(weights["Z"], weight_sum, "B")
    safe_sum = graph.node("Nonzero projection weight sum", "Max", const_b=0.0001)
    graph.link(weight_sum, safe_sum, "A")
    projected = []
    for axis, mask in (("X", (False, True, True)),
                       ("Y", (True, False, True)),
                       ("Z", (True, True, False))):
        coordinates = graph.node(axis + " facing world coordinates", "ComponentMask",
                                 r=mask[0], g=mask[1], b=mask[2], a=False)
        graph.link(world, coordinates, "Input")
        uv = graph.node(axis + " rock projection at 340 cm", "Multiply", const_b=1.0 / 340.0)
        graph.link(coordinates, uv, "A")
        sample = graph.sample("Rock grain " + axis, color_texture, uv, "COLOR",
                              sampler_source=helper.WRAP_SAMPLER_GROUP)
        weight = graph.node(axis + " normalized projection weight", "Divide")
        graph.link(weights[axis], weight, "A")
        graph.link(safe_sum, weight, "B")
        weighted = graph.node(axis + " weighted rock grain", "Multiply")
        graph.link(sample, weighted, "A", "RGB")
        graph.link(weight, weighted, "B")
        projected.append(weighted)
    side_grain = graph.node("Combined rock side grain", "Add")
    graph.link(projected[0], side_grain, "A")
    graph.link(projected[1], side_grain, "B")
    grain = graph.node("Triplanar rock grain", "Add")
    graph.link(side_grain, grain, "A")
    graph.link(projected[2], grain, "B")

    up = graph.node("Signed rock up facing", "ComponentMask", r=False, g=False, b=True, a=False)
    graph.link(normal_ws, up, "Input")
    up_clamped = graph.node("Up facing rock surfaces", "Clamp", min_default=0.0, max_default=1.0)
    graph.link(up, up_clamped, "Input")
    top_weight = graph.node("Rock dust facing weight", "Power", const_exponent=4.0)
    graph.link(up_clamped, top_weight, "Base")
    side_weight = graph.node("Rock weathered side weight", "OneMinus")
    graph.link(top_weight, side_weight, "Input")
    side_weathering = graph.node("Authored side weathering", "Multiply")
    graph.link(authored, side_weathering, "A", "G")
    graph.link(side_weight, side_weathering, "B")
    stain_weight = graph.node("Restrained side staining", "Multiply", const_b=0.12)
    graph.link(side_weathering, stain_weight, "A")
    stained = graph.node("Sparse warm rock staining", "LinearInterpolate")
    graph.link(face_color, stained, "A")
    graph.link(graph.color("IronOxide", ROCK_PALETTE["IronOxide"]), stained, "B", "RGB")
    graph.link(stain_weight, stained, "Alpha")
    cap_weathering = graph.node("Authored cap weathering", "Multiply")
    graph.link(authored, cap_weathering, "A", "G")
    graph.link(top_weight, cap_weathering, "B")
    cap_weight = graph.node("Restrained cap dust", "Multiply")
    graph.link(cap_weathering, cap_weight, "A")
    graph.link(graph.scalar("WeatheringStrength", 0.22), cap_weight, "B")
    weathered = graph.node("Rock with sparse cap dust", "LinearInterpolate")
    graph.link(stained, weathered, "A")
    graph.link(graph.color("CanyonCap", ROCK_PALETTE["CanyonCap"]), weathered, "B", "RGB")
    graph.link(cap_weight, weathered, "Alpha")

    # Photographic grain is only a small modulation of authored color. A zero
    # RockDetailAmount provides a clean material blockout without graph edits.
    grayscale = graph.node("Neutral rock grain luminance", "Desaturation")
    graph.link(grain, grayscale, "Input")
    graph.link(graph.node("Full rock grain desaturation", "Constant", r=1.0), grayscale, "Fraction")
    centered_grain = graph.node("Centered rock grain", "Subtract", const_b=0.5)
    graph.link(grayscale, centered_grain, "A")
    detail_gain = graph.node("Restrained rock detail contrast", "Multiply")
    graph.link(centered_grain, detail_gain, "A")
    graph.link(graph.scalar("RockDetailAmount", 0.14), detail_gain, "B")
    detail_bias = graph.node("Rock detail around unity", "Add", const_b=1.0)
    graph.link(detail_gain, detail_bias, "A")
    bounded_grain = graph.node("Bounded rock detail modulation", "Clamp",
                               min_default=0.94, max_default=1.06)
    graph.link(detail_bias, bounded_grain, "Input")
    detailed_color = graph.node("Authored rock with quiet grain", "Multiply")
    graph.link(weathered, detailed_color, "A")
    graph.link(bounded_grain, detailed_color, "B")

    # Elevated rocks retain the exact per-pixel fog contract. This map-sized
    # texture keeps its own clamped sampler; only grain uses shared wrap.
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
    rock_fog_color = graph.node("Unknown and explored rock fog", "LinearInterpolate")
    graph.link(graph.color("UnknownFog", (0.010, 0.017, 0.027)), rock_fog_color, "A", "RGB")
    graph.link(graph.color("ExploredFog", (0.027, 0.041, 0.049)), rock_fog_color, "B", "RGB")
    graph.link(fog_sample, rock_fog_color, "Alpha", "G")
    rock_fog_emission = graph.node("Rock fog independent of lighting", "Multiply")
    graph.link(rock_fog_color, rock_fog_emission, "A")
    graph.link(fog_sample, rock_fog_emission, "B", "R")
    graph.output(rock_fog_emission, "EMISSIVE_COLOR")

    # A partly discovered cliff may be instantiated before its center is known.
    # Following the ground's vertex fog mask prevents its still-unexplored
    # sections from standing above the fog plane and occluding visible terrain.
    # Sink beneath the ground's -1 cm unknown plane: equal-depth flattened
    # triangles fight for the same pixels and expose a crushed mesh silhouette.
    # This fetch keeps the map's clamped sampler; explicit mip 0 is required in
    # the mobile Metal vertex shader. G records exploration, not current sight.
    height_fog = graph.sample("RockHeightFog", fog_texture, fog_uv, "LINEAR_COLOR")
    height_fog.set_editor_property("parameter_name", "FogMask")
    height_fog.set_editor_property("mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
    height_fog.set_editor_property("const_mip_value", 0)
    unknown = graph.node("Unexplored rock height fraction", "OneMinus")
    graph.link(height_fog, unknown, "Input", "G")
    rock_z = graph.node("Rock height above baseline", "ComponentMask",
                        r=False, g=False, b=True, a=False)
    graph.link(world, rock_z, "Input")
    above_baseline = graph.node("Rock hidden baseline below terrain", "Add", const_b=32.0)
    graph.link(rock_z, above_baseline, "A")
    hidden_height = graph.node("Unknown rock relief", "Multiply")
    graph.link(above_baseline, hidden_height, "A")
    graph.link(unknown, hidden_height, "B")
    flatten = graph.node("Flatten unexplored rock vertices", "Multiply")
    graph.link(hidden_height, flatten, "A")
    graph.link(graph.node("Rock downward offset", "Constant3Vector",
                          constant=unreal.LinearColor(0.0, 0.0, -1.0, 0.0)), flatten, "B")
    graph.output(flatten, "WORLD_POSITION_OFFSET")

    # Use imported fracture normals. Projected tangent-space normal textures
    # have no matching basis on these arbitrarily oriented faces and hide the
    # deliberate broad planes. The flat tangent vector preserves geometry.
    graph.output(graph.node("Authored fracture normals", "Constant3Vector",
                            constant=unreal.LinearColor(0, 0, 1, 0)), "NORMAL")
    rough = graph.node("Matte rock roughness", "LinearInterpolate", const_a=0.94, const_b=0.88)
    graph.link(authored, rough, "Alpha", "R")
    graph.output(rough, "ROUGHNESS")
    cavity = graph.node("Bounded authored rock cavity", "Clamp", min_default=0.78, max_default=1.0)
    graph.link(authored, cavity, "Input", "B")
    graph.output(cavity, "AMBIENT_OCCLUSION")
    graph.output(graph.node("Nonmetal fractured rock", "Constant", r=0.0), "METALLIC")
    graph.output(graph.node("Restrained rock specular", "Constant", r=0.20), "SPECULAR")
    result = graph.finish()
    result["rock_palette_linear_rgb"] = ROCK_PALETTE
    result["licensed_surface_detail"] = {
        "source": "Poly Haven gravel_floor_04", "license": "CC0-1.0",
        "textures": [color_path], "texture_samples": 3, "total_pixel_texture_samples": 4,
        "vertex_texture_samples": 1,
        "mapping": "three world albedo projections; normalized abs(vertex normal)^4 weights",
        "projection": {"tile_width_cm": 340.0, "blended": "samples, never coordinates",
                       "stable_under_nonuniform_instance_scale": True},
        "albedo_modulation": [0.94, 1.06], "rock_detail_amount": 0.14,
        "normal_strength": 0.0, "normals": "authored geometric fracture normals",
        "weathering_strength": 0.22, "side_stain_strength": 0.12,
        "specular": 0.20, "roughness_bounds": [0.88, 0.94],
        "vertex_channels": {"R": "non-periodic broad face tone", "G": "sparse weathering",
                            "B": "cavity visibility, clamped 0.78 to 1.0"},
        "unknown_height": {"source": "clamped FogMask G, explicit mip 0",
                           "baseline_world_z": -32.0,
                           "offset": "-(worldZ + 32) * (1 - explored G)"},
    }
    material = graph.material
    tag(material, "CanyonOwner", OWNER)
    tag(material, "SourceLicense", "original Cinderline procedural material")
    tag(material, "MaterialContract",
        "vertex R non-periodic face tone, G sparse weathering, B cavity; geometric normals; "
        "quiet triplanar CC0 albedo; matte nonmetal; clamped per-pixel fog and unknown vertex flatten")
    require(unreal.EditorAssetLibrary.save_loaded_asset(material), "could not save " + MATERIAL_PATH)
    return material, result


def configure_static_mesh_import_data(data):
    for key, value in (
        ("combine_meshes", True), ("convert_scene", True), ("convert_scene_unit", True),
        ("force_front_x_axis", False), ("transform_vertex_to_absolute", True),
        ("bake_pivot_in_vertex", False), ("reorder_material_to_fbx_order", True),
        ("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS),
        # M_CinderCanyonRock reads non-periodic face tone (R), sparse weathering
        # (G) and cavity visibility (B), so importing without them silently produces
        # flat rock. Left unset this followed the factory default, which on a
        # REIMPORT preserves whatever the asset already had instead of taking the
        # file's — the path that only runs with CINDER_REIMPORT_CANYON=1 and so had
        # never been exercised. State it explicitly for both paths.
        ("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE),
        ("auto_generate_collision", False), ("generate_lightmap_u_vs", False),
        ("build_nanite", False), ("remove_degenerates", True)):
        data.set_editor_property(key, value)
    require(data.get_editor_property("vertex_color_import_option") == unreal.VertexColorImportOption.REPLACE,
            "FBX vertex color replacement did not persist")
    return data


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
    configure_static_mesh_import_data(data)
    return options


@contextmanager
def legacy_fbx_import(report):
    # UE 5.8 otherwise routes both an explicit FbxFactory reimport and the
    # StaticMeshEditorSubsystem LOD importer through Interchange. Its stored
    # pipeline does not consume this script's FbxImportUI. Scope the verified
    # legacy route to this owned kit, then restore the process setting.
    variable = "Interchange.FeatureFlags.Import.FBX"
    require(unreal.SystemLibrary.get_console_variable_string_value(variable) != "",
            "engine does not expose the FBX importer selection variable")
    previous = unreal.SystemLibrary.get_console_variable_int_value(variable)
    details = {"route": "legacy FBX", "variable": variable, "previous": previous,
               "restored": False, "vertex_color_option": "REPLACE"}
    report["import_contract"] = details
    try:
        unreal.SystemLibrary.execute_console_command(None, variable + " 0")
        require(unreal.SystemLibrary.get_console_variable_int_value(variable) == 0,
                "could not select the legacy FBX importer")
        unreal.log("CINDERLINE_CANYON_LEGACY_FBX_IMPORT")
        yield
    finally:
        unreal.SystemLibrary.execute_console_command(None, variable + " " + str(previous))
        details["restored"] = unreal.SystemLibrary.get_console_variable_int_value(variable) == previous
        require(details["restored"], "could not restore the FBX importer selection variable")


def import_lod0(record, replace):
    lod = record["lods"][0]
    if replace:
        existing = unreal.load_asset(MESHES + "/" + record["name"])
        require(isinstance(existing, unreal.StaticMesh) and metadata(existing, "CanyonOwner") == OWNER,
                "refusing to change import settings on an unrelated mesh")
        # Legacy unattended reimport also replaces task options with the mesh's
        # persisted import data (EditorFactories.cpp). Set that data explicitly
        # before import; LOD1/2 subsequently inherit these same settings.
        data = existing.get_editor_property("asset_import_data")
        if not isinstance(data, unreal.FbxStaticMeshImportData):
            data = unreal.FbxStaticMeshImportData(outer=existing)
            existing.set_editor_property("asset_import_data", data)
        configure_static_mesh_import_data(data)
        data.scripted_add_filename(str(lod["_source"]), 0, "")
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
    mesh = meshes[0]
    data = mesh.get_editor_property("asset_import_data")
    require(isinstance(data, unreal.FbxStaticMeshImportData),
            record["name"] + " did not retain legacy FBX import settings")
    require(data.get_editor_property("vertex_color_import_option") == unreal.VertexColorImportOption.REPLACE,
            record["name"] + " did not retain vertex color replacement for source LODs")
    return mesh


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


def verify_lod_colors(mesh, record):
    # HasVertexColors returns true if ANY source LOD has one non-white corner.
    # Inspect every source and rendering LOD instead, so a colored LOD0 cannot
    # conceal white LOD1/2. Geometry Script is loaded by the editor, and its
    # corner query preserves split face colors rather than averaging vertices.
    require(hasattr(unreal, "GeometryScript_AssetUtils") and hasattr(unreal, "GeometryScript_MeshQueries"),
            "Geometry Script is required to verify source and rendering LOD colors")
    asset_utils = unreal.GeometryScript_AssetUtils
    queries = unreal.GeometryScript_MeshQueries
    options = unreal.GeometryScriptCopyMeshFromAssetOptions(
        apply_build_settings=False, request_tangents=False, use_build_scale=True)
    reports = []
    for lod in record["lods"]:
        entry = {"lod": lod["lod"]}
        for label, kind in (("source", unreal.GeometryScriptLODType.SOURCE_MODEL),
                            ("render", unreal.GeometryScriptLODType.RENDER_DATA)):
            requested = unreal.GeometryScriptMeshReadLOD(lod_type=kind, lod_index=lod["lod"])
            # The Python binding consumes the C++ bool return as its success
            # sentinel and exposes only the out enum (or None), not a tuple.
            availability = asset_utils.check_static_mesh_has_available_lod(mesh, requested)
            require(availability == unreal.GeometryScriptSearchOutcomePins.FOUND,
                    record["name"] + " lacks " + label + " LOD" + str(lod["lod"]))
            dynamic, outcome = asset_utils.copy_mesh_from_static_mesh(mesh, unreal.DynamicMesh(), options, requested)
            require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS,
                    record["name"] + " could not read " + label + " LOD" + str(lod["lod"]))
            require(queries.get_has_vertex_colors(dynamic),
                    record["name"] + " " + label + " LOD" + str(lod["lod"]) + " has no vertex color layer")
            minimum, maximum = [float("inf")] * 4, [float("-inf")] * 4
            triangles = 0
            # UE's generated Python spelling splits the plural IDs acronym.
            for triangle in range(queries.get_num_triangle_i_ds(dynamic)):
                if not queries.is_valid_triangle_id(dynamic, triangle):
                    continue
                _, color1, color2, color3, valid = queries.get_triangle_vertex_colors(dynamic, triangle)
                require(valid, record["name"] + " has a triangle without complete corner colors")
                triangles += 1
                for color in (color1, color2, color3):
                    for channel, value in enumerate((color.r, color.g, color.b, color.a)):
                        require(math.isfinite(value) and -0.001 <= value <= 1.001,
                                record["name"] + " has an invalid vertex color")
                        minimum[channel] = min(minimum[channel], value)
                        maximum[channel] = max(maximum[channel], value)
            require(triangles == lod["triangles"],
                    record["name"] + " color readback has the wrong triangle count")
            require(maximum[0] - minimum[0] > 0.02,
                    record["name"] + " " + label + " LOD" + str(lod["lod"]) + " lost authored face-tone variation")
            entry[label] = {"triangles": triangles, "rgba_min": minimum, "rgba_max": maximum}
        # Source conversion exposes sRGB-encoded RGB; render conversion exposes
        # normalized FColor bytes. They should agree within byte quantization.
        for bound in ("rgba_min", "rgba_max"):
            require(all(abs(a - b) <= 2.0 / 255.0
                        for a, b in zip(entry["source"][bound], entry["render"][bound])),
                    record["name"] + " LOD" + str(lod["lod"]) + " rendering colors differ from source colors")
        reports.append(entry)
    return reports


def configure_lod_screen_sizes(mesh, record, subsystem):
    # Keep the authored bevels and fracture planes at the normal sector camera.
    # UE's automatic thresholds were allowed to reduce these small source meshes
    # before they became small on screen. SetLodScreenSizes disables automatic
    # selection and writes BOTH source-model and render-data thresholds in UE5.8.
    count = len(record["lods"])
    require(count in (1, 3), record["name"] + " has an unsupported authored LOD chain")
    expected = [1.0, 0.10, 0.035][:count]
    require(subsystem.set_lod_screen_sizes(mesh, expected),
            record["name"] + " could not set explicit LOD screen sizes")
    actual = list(subsystem.get_lod_screen_sizes(mesh))
    require(len(actual) == count and all(abs(a - b) <= 0.000001 for a, b in zip(actual, expected)),
            record["name"] + " LOD screen-size readback differs: " + repr(actual))
    # bAutoComputeLODScreenSize is not reflected into Python in this engine.
    # The native setter disables it; verify the public render-data readback.
    return {"screen_sizes": actual, "auto_compute": "disabled by native SetLodScreenSizes"}


def verify_mesh(mesh, record, material, subsystem):
    lod_count = len(record["lods"])
    require(mesh.get_num_lods() == lod_count,
            record["name"] + " must contain exactly {} LODs".format(lod_count))
    lod_screen_report = configure_lod_screen_sizes(mesh, record, subsystem)
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
    require(subsystem.has_vertex_colors(mesh), record["name"] + " lost its authored rock vertex colors")
    color_report = verify_lod_colors(mesh, record)

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
        "lod_selection": lod_screen_report,
        "sections": [mesh.get_num_sections(lod) for lod in range(lod_count)],
        "vertex_colors": True,
        "vertex_colors_per_lod": color_report,
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
        with legacy_fbx_import(report):
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
                # SetLodScreenSizes updates source/render data without marking
                # the package dirty, so force the owned mesh save even on a
                # material-only rerun whose FBX hashes did not change.
                require(unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False),
                        "could not save " + path)
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
