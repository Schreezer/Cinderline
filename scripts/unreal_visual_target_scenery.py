"""Import the authored visual-target scenery pack into Unreal.

Run with UnrealEditor-Cmd -ExecutePythonScript after generating the Blender pack.
Set CINDER_REIMPORT_SCENERY=1 to replace assets previously owned by this script.
The importer never changes maps, actors, gameplay collision, or simulation data.
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
SOURCE = ROOT / "RawAssets/VisualTarget/Scenery"
DESTINATION = "/Game/Art/VisualTarget/Scenery"
MATERIAL_ROOT = "/Game/Art/VisualTarget/Materials"
PHOTO_TEXTURES = DESTINATION + "/Textures"
PHOTO_MATERIALS = DESTINATION + "/Materials"
PHOTO_SOURCE = SOURCE / "External/namaqualand_boulders_01/textures"
REPORT = ROOT / "artifacts/visual-target/scenery/unreal-import-results.json"
SUCCESS = ROOT / "artifacts/visual-target/scenery/unreal-import.success"
OWNER = "scripts/unreal_visual_target_scenery.py"
EXPECTED = {f"SM_CinderScenery_Rock_{letter}" for letter in "ABCDEF"} | {
    "SM_CinderScenery_CliffMass",
    "SM_CinderScenery_Pad", "SM_CinderScenery_Road", "SM_CinderScenery_Pipe",
    "SM_CinderScenery_Crate", "SM_CinderScenery_Debris"}


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline scenery: " + message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(asset, key):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + key))


def tag(asset, key, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Cinderline." + key, str(value))


def set_checked(asset, key, value):
    asset.set_editor_property(key, value)
    require(asset.get_editor_property(key) == value,
            asset.get_path_name() + " did not retain " + key)


def import_photo_texture(filename, asset_name, role, replace):
    source = PHOTO_SOURCE / filename
    require(source.is_file(), "missing CC0 photogrammetry texture " + str(source))
    digest = sha256(source)
    path = PHOTO_TEXTURES + "/" + asset_name
    asset = unreal.load_asset(path)
    if asset is not None:
        require(isinstance(asset, unreal.Texture2D) and metadata(asset, "SceneryOwner") == OWNER,
                "refusing to replace unrelated texture " + path)
        require(replace or metadata(asset, "SourceSha256") == digest,
                asset_name + " source changed; set CINDER_REIMPORT_SCENERY=1")
    if asset is None or replace:
        task = unreal.AssetImportTask()
        for key, value in {"filename": str(source), "destination_path": PHOTO_TEXTURES,
                           "destination_name": asset_name, "automated": True, "async_": False,
                           "save": False, "replace_existing": asset is not None,
                           "replace_existing_settings": asset is not None}.items():
            task.set_editor_property(key, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        asset = unreal.load_asset(path)
        require(isinstance(asset, unreal.Texture2D), "texture import failed for " + path)
    compression = (unreal.TextureCompressionSettings.TC_DEFAULT if role == "Color"
                   else unreal.TextureCompressionSettings.TC_NORMALMAP if role == "Normal"
                   else unreal.TextureCompressionSettings.TC_MASKS)
    settings = {"compression_settings": compression, "srgb": role == "Color",
                "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP if role == "Normal"
                else unreal.TextureGroup.TEXTUREGROUP_WORLD,
                "max_texture_size": 2048,
                "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP,
                "never_stream": False, "virtual_texture_streaming": False,
                "address_x": unreal.TextureAddress.TA_WRAP, "address_y": unreal.TextureAddress.TA_WRAP,
                "filter": unreal.TextureFilter.TF_DEFAULT}
    if role == "Normal":
        settings["flip_green_channel"] = True  # Poly Haven source is OpenGL normal convention.
    for key, value in settings.items():
        set_checked(asset, key, value)
    tag(asset, "SceneryOwner", OWNER)
    tag(asset, "SourceSha256", digest)
    tag(asset, "SourceLicense", "CC0-1.0")
    tag(asset, "SourceURL", "https://polyhaven.com/a/namaqualand_boulders_01")
    tag(asset, "TextureRole", role)
    require(asset.blueprint_get_size_x() == 2048 and asset.blueprint_get_size_y() == 2048,
            asset_name + " must import at 2048x2048")
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset), "could not save " + path)
    return asset, {"path": path, "source": str(source.relative_to(ROOT)), "sha256": digest,
                   "role": role, "dimensions": [2048, 2048], "compression": str(compression)}


def create_photo_material(replace):
    unreal.EditorAssetLibrary.make_directory(PHOTO_TEXTURES)
    unreal.EditorAssetLibrary.make_directory(PHOTO_MATERIALS)
    color, color_report = import_photo_texture(
        "namaqualand_boulders_01_diff_2k.jpg", "T_NamaqualandBoulders_Diffuse", "Color", replace)
    normal, normal_report = import_photo_texture(
        "namaqualand_boulders_01_nor_gl_2k.jpg", "T_NamaqualandBoulders_Normal", "Normal", replace)
    arm, arm_report = import_photo_texture(
        "namaqualand_boulders_01_arm_2k.jpg", "T_NamaqualandBoulders_ARM", "Masks", replace)

    helper_path = ROOT / "scripts/unreal_visual_upgrade.py"
    spec = importlib.util.spec_from_file_location("cinder_scenery_photo_graph", helper_path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.OWNER = OWNER
    helper.VERSION = "cinder-scenery-polyhaven-v1"
    helper.MATERIALS = PHOTO_MATERIALS
    graph = helper.Graph("M_CinderSceneryPhotogrammetry")
    uv = graph.uv0(1.0)
    diffuse = graph.sample("PhotogrammetryDiffuse", color, uv, "COLOR",
                           sampler_source=helper.WRAP_SAMPLER_GROUP)
    # The raw Namaqualand albedo used to reach BASE_COLOR untouched, and this
    # material wins the load order over MI_CinderSceneryRock, which is the only
    # rock asset that had a tint parameter. The map therefore shipped as beige
    # photogrammetry boulders standing on orange ground. Two ALU nodes on a
    # material that already takes three fetches pull the photograph towards the
    # canyon's own rock: partial desaturation first so the boulders keep their
    # scanned value contrast, then a tint that can be retuned without a reimport.
    neutral = graph.node("Neutral photogrammetry grain", "Desaturation")
    graph.link(diffuse, neutral, "Input", "RGB")
    graph.link(graph.scalar("Desaturation", 0.55), neutral, "Fraction")
    tinted = graph.node("Canyon rock albedo", "Multiply")
    graph.link(neutral, tinted, "A")
    graph.link(graph.color("RockTint", (0.47, 0.33, 0.24)), tinted, "B", "RGB")
    graph.output(tinted, "BASE_COLOR")
    sampled_normal = graph.sample("PhotogrammetryNormal", normal, uv, "NORMAL",
                                  sampler_source=helper.WRAP_SAMPLER_GROUP)
    graph.normal(sampled_normal, 0.82)
    packed = graph.sample("PhotogrammetryARM", arm, uv, "MASKS",
                          sampler_source=helper.WRAP_SAMPLER_GROUP)
    graph.output(packed, "AMBIENT_OCCLUSION", "R")
    graph.output(packed, "ROUGHNESS", "G")
    graph.output(graph.node("Nonmetal rock", "Constant", r=0.0), "METALLIC")
    material_report = graph.finish()
    material = graph.material
    tag(material, "SceneryOwner", OWNER)
    tag(material, "SourceLicense", "CC0-1.0")
    tag(material, "SourceURL", "https://polyhaven.com/a/namaqualand_boulders_01")
    require(unreal.EditorAssetLibrary.save_loaded_asset(material), "could not save photogrammetry material")
    return material, {"textures": [color_report, normal_report, arm_report],
                      "material": material_report,
                      "source": "https://polyhaven.com/a/namaqualand_boulders_01",
                      "license": "CC0-1.0"}


def load_manifest():
    path = SOURCE / "manifest.json"
    require(path.is_file(), "missing " + str(path))
    manifest = json.loads(path.read_text(encoding="utf-8"))
    require(manifest.get("unit") == "centimeter", "manifest unit changed")
    require(manifest.get("forward_axis") == "+X" and manifest.get("up_axis") == "+Z",
            "manifest axes changed")
    require(manifest.get("collision") == "none; presentation only", "collision contract changed")
    external = manifest.get("external_source", {})
    require(external.get("asset_id") == "namaqualand_boulders_01"
            and external.get("license") == "CC0 1.0 Universal",
            "photogrammetry source or CC0 license changed")
    provenance_path = ROOT / external.get("provenance", "")
    require(provenance_path.is_file() and SOURCE.resolve() in provenance_path.resolve().parents,
            "missing in-pack photogrammetry provenance")
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    require(provenance.get("asset_id") == external["asset_id"]
            and provenance.get("license") == external["license"]
            and provenance.get("source_page") == external.get("source_page"),
            "photogrammetry provenance does not match manifest")
    for source_record in provenance.get("files", []):
        source_path = provenance_path.parent / source_record.get("path", "")
        require(source_path.is_file() and provenance_path.parent.resolve() in source_path.resolve().parents
                and sha256(source_path) == source_record.get("sha256"),
                "photogrammetry source hash differs for " + str(source_path))
    records = manifest.get("assets", [])
    names = [record.get("name") for record in records]
    require(set(names) == EXPECTED and len(names) == len(EXPECTED), "manifest asset set changed")
    for record in records:
        name = record["name"]
        source = ROOT / record["file"]
        require(source.parent.resolve() == (SOURCE / "FBX").resolve(), name + " source path escaped the pack")
        require(source.is_file() and sha256(source) == record.get("sha256"), name + " source hash differs")
        require(record.get("material_slot") in ("Rock", "RockPhoto", "Metal"), name + " has an unexpected material slot")
        require(record.get("uv_channels", 0) >= 1 and record.get("uv0_loop_coverage") == 1.0,
                name + " lacks complete UV0 coverage")
        require(record.get("minimum_triangle_area_cm2", 0) >= 0.00005,
                name + " contains geometry below Unreal's triangle area threshold")
        triangles = record.get("triangles")
        require(isinstance(triangles, int) and triangles > 0 and triangles <= 4000,
                name + " exceeds the mobile mesh budget")
        dimensions = record.get("size_cm", [])
        require(len(dimensions) == 3 and all(math.isfinite(value) and value > 0 for value in dimensions),
                name + " has invalid bounds")
        record["_source"] = source
    return records


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


def import_mesh(record, replace):
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(record["_source"]), "destination_path": DESTINATION,
        "destination_name": record["name"], "automated": True, "async_": False,
        "save": False, "replace_existing": replace, "replace_existing_settings": replace,
        "factory": unreal.FbxFactory(), "options": import_options()}.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    expected_path = DESTINATION + "/" + record["name"] + "." + record["name"]
    meshes = [asset for asset in task.get_objects()
              if isinstance(asset, unreal.StaticMesh) and asset.get_path_name() == expected_path]
    require(len(meshes) == 1, record["name"] + " did not import as one static mesh")
    return meshes[0]


def run():
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    SUCCESS.unlink(missing_ok=True)
    report = {"success": False, "utc": datetime.now(timezone.utc).isoformat(),
              "owner": OWNER, "destination": DESTINATION, "assets": []}
    try:
        records = load_manifest()
        replace = os.environ.get("CINDER_REIMPORT_SCENERY") == "1"
        materials = {}
        for slot in ("Rock", "Metal"):
            path = MATERIAL_ROOT + "/MI_CinderScenery" + slot
            material = unreal.load_asset(path)
            require(isinstance(material, unreal.MaterialInterface), "missing material " + path)
            materials[slot] = material
        materials["RockPhoto"], report["photogrammetry"] = create_photo_material(replace)
        unreal.EditorAssetLibrary.make_directory(DESTINATION)
        mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        require(mesh_subsystem is not None, "StaticMeshEditorSubsystem is unavailable")
        for record in records:
            asset_path = DESTINATION + "/" + record["name"]
            mesh = unreal.load_asset(asset_path)
            if mesh is not None:
                require(isinstance(mesh, unreal.StaticMesh), "unrelated asset occupies " + asset_path)
                require(metadata(mesh, "SceneryOwner") == OWNER,
                        "refusing to replace asset owned by " + metadata(mesh, "SceneryOwner"))
                require(replace or metadata(mesh, "SourceSha256") == record["sha256"],
                        record["name"] + " source changed; set CINDER_REIMPORT_SCENERY=1")
            if mesh is None or replace:
                mesh = import_mesh(record, mesh is not None)
            imported_triangles = mesh.get_num_triangles(0)
            require(imported_triangles == record["triangles"],
                    "{} imported {} LOD0 triangles; manifest expects {}".format(
                        record["name"], imported_triangles, record["triangles"]))
            mesh.set_material(0, materials[record["material_slot"]])
            try:
                mesh.set_editor_property("lod_group", unreal.Name("SmallProp"))
            except Exception:
                unreal.log_warning(record["name"] + ": engine did not expose lod_group to Python; runtime culling remains active")
            lod0_after_group = mesh.get_num_triangles(0)
            require(lod0_after_group == imported_triangles,
                    "{} LOD0 changed from {} to {} after SmallProp LOD generation".format(
                        record["name"], imported_triangles, lod0_after_group))
            require(mesh_subsystem.get_simple_collision_count(mesh) == 0,
                    record["name"] + " unexpectedly has simple collision")
            require(len(mesh.get_editor_property("static_materials")) == 1,
                    record["name"] + " must have one material slot")
            tag(mesh, "SceneryOwner", OWNER)
            tag(mesh, "SourceSha256", record["sha256"])
            tag(mesh, "FogContract", "observed-state-only")
            tag(mesh, "Collision", "none")
            require(unreal.EditorAssetLibrary.save_loaded_asset(mesh), "could not save " + asset_path)
            report["assets"].append({"name": record["name"], "asset": mesh.get_path_name(),
                                     "triangles": lod0_after_group,
                                     "triangles_lod0_before_group": imported_triangles,
                                     "triangles_lod0_after_group": lod0_after_group,
                                     "material": materials[record["material_slot"]].get_path_name(),
                                     "simple_collisions": 0, "source_sha256": record["sha256"]})
        report["success"] = True
        SUCCESS.write_text("ok\n", encoding="utf-8")
        unreal.log("CINDERLINE_SCENERY_IMPORT_OK: " + str(REPORT))
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        REPORT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    run()
