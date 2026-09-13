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
EXPECTED = {f"SM_CinderCanyon_Rock_{letter}" for letter in "ABCDEF"} | {
    "SM_CinderCanyon_CliffMass", "SM_CinderCanyon_Debris"
}


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
    require(set(names) == EXPECTED and len(names) == len(EXPECTED), "manifest asset set changed")
    for record in records:
        name = record["name"]
        require(record.get("material_slot") == "M_CinderCanyonRock",
                name + " must use the shared canyon material slot")
        lods = record.get("lods", [])
        require([row.get("lod") for row in lods] == [0, 1, 2], name + " must have explicit LOD0/1/2")
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
                and triangles[0] > triangles[1] > triangles[2], name + " LOD triangle order is invalid")
        if "_Rock_" in name:
            require(800 <= triangles[0] <= 2000, name + " LOD0 exceeds the cliff budget")
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
    authored_uv = graph.uv0(2.0)

    authored = graph.node("Authored strata, iron, and cavity masks", "VertexColor")
    shadow = graph.color("CanyonShadow", (0.23, 0.075, 0.026))
    sandstone = graph.color("CanyonSandstone", (0.58, 0.24, 0.072))
    strata = graph.node("Stratified sandstone color", "LinearInterpolate")
    graph.link(shadow, strata, "A")
    graph.link(sandstone, strata, "B")
    graph.link(authored, strata, "Alpha", "R")
    iron = graph.color("IronOxide", (0.48, 0.105, 0.025))
    stained = graph.node("Warm iron staining", "LinearInterpolate")
    graph.link(strata, stained, "A")
    graph.link(iron, stained, "B")
    graph.link(authored, stained, "Alpha", "G")

    # Reuse the existing licensed gravel photograph as restrained grayscale
    # sediment grain. The authored vertex masks remain the dominant strata and
    # iron color, while this modulation breaks up broad low-poly faces.
    grain = graph.sample("Licensed sediment grain", color_texture, authored_uv, "COLOR")
    grayscale = graph.node("Neutral sediment luminance", "Desaturation")
    graph.link(grain, grayscale, "Input", "RGB")
    graph.link(graph.node("Full grain desaturation", "Constant", r=1.0), grayscale, "Fraction")
    grain_gain = graph.node("Restrained grain contrast", "Multiply", const_b=0.72)
    graph.link(grayscale, grain_gain, "A")
    grain_bias = graph.node("Centered grain value", "Add", const_b=0.64)
    graph.link(grain_gain, grain_bias, "A")
    bounded_grain = graph.node("Bounded sediment modulation", "Clamp",
                               min_default=0.78, max_default=1.20)
    graph.link(grain_bias, bounded_grain, "Input")
    detailed_color = graph.node("Strata with sediment grain", "Multiply")
    graph.link(stained, detailed_color, "A")
    graph.link(bounded_grain, detailed_color, "B")
    graph.output(detailed_color, "BASE_COLOR")

    graph.normal(graph.sample("Restrained sediment normal", normal_texture,
                              authored_uv, "NORMAL"), 0.42)

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
        "texture_samples": 2,
        "mapping": "authored UV0 at 2x tiling",
        "albedo_modulation": [0.78, 1.20],
        "normal_strength": 0.42,
        "specular": 0.08,
    }
    material = graph.material
    tag(material, "CanyonOwner", OWNER)
    tag(material, "SourceLicense", "original Cinderline procedural material")
    tag(material, "MaterialContract",
        "vertex R strata, G iron stain, B cavity; CC0 sediment grain and normal; rough low-spec nonmetal")
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
    require(mesh.get_num_lods() == 3, record["name"] + " must contain exactly three source LODs")


def verify_mesh(mesh, record, material, subsystem):
    require(mesh.get_num_lods() == 3, record["name"] + " must contain exactly three LODs")
    actual_triangles = [mesh.get_num_triangles(lod) for lod in range(3)]
    expected_triangles = [lod["triangles"] for lod in record["lods"]]
    require(actual_triangles == expected_triangles,
            record["name"] + " imported triangle counts differ: " + repr(actual_triangles))
    require(all(mesh.get_num_sections(lod) == 1 for lod in range(3)),
            record["name"] + " must keep one section in every LOD")
    require(len(mesh.get_editor_property("static_materials")) == 1,
            record["name"] + " must keep one shared material slot")
    mesh.set_material(0, material)
    require(mesh.get_material(0) == material, record["name"] + " material assignment failed")
    require(subsystem.has_vertex_colors(mesh), record["name"] + " lost its authored strata vertex colors")

    subsystem.remove_collisions(mesh)
    for lod in range(3):
        subsystem.enable_section_collision(mesh, False, lod, 0)
    require(all(not subsystem.is_section_collision_enabled(mesh, lod, 0) for lod in range(3)),
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
        "sections": [mesh.get_num_sections(lod) for lod in range(3)],
        "vertex_colors": True,
        "section_collision": [False, False, False],
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
