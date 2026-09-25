"""Import the original frontier grass and build its opaque fog-aware wind material.

Run in a render-capable UnrealEditor-Cmd after create_frontier_foliage.py.
Set CINDER_REIMPORT_FRONTIER_FOLIAGE=1 to replace this script's existing mesh.
This does not edit maps or actors. WindTime is supplied by the simulation clock;
its default of zero leaves the authored rest shape unchanged.
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
SOURCE = ROOT / "RawAssets/FrontierFoliage"
MANIFEST = SOURCE / "manifest.json"
REPORT = SOURCE / "unreal-import-results.json"
SUCCESS = SOURCE / "unreal-import.success"
OWNER = "scripts/unreal_frontier_foliage.py"
VERSION = "cinder-frontier-foliage-v1"
MESHES = "/Game/Art/FrontierFoliage/Meshes"
MATERIALS = "/Game/Art/FrontierFoliage/Materials"
MATERIAL_NAME = "M_CinderFrontierFoliage"
MATERIAL_PATH = MATERIALS + "/" + MATERIAL_NAME
NAME = "SM_CinderFrontier_Grass_A"


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline frontier foliage: " + message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(asset, key):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + key))


def tag(asset, key, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Cinderline." + key, str(value))


def load_helper(filename, name):
    path = ROOT / "scripts" / filename
    require(path.is_file(), "missing shared helper " + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_manifest():
    require(MANIFEST.is_file(), "missing authoring manifest")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("unit") == "centimeter" and manifest.get("forward_axis") == "+X"
            and manifest.get("up_axis") == "+Z", "units or axis contract changed")
    require(manifest.get("origin") == "bottom center; bounds_min_z_cm is exactly 0",
            "bottom-center origin contract changed")
    require(manifest.get("nanite") is False and manifest.get("alpha_cards") is False,
            "foliage must use opaque geometry without Nanite")
    require(manifest.get("collision") == "none; decorative foliage only", "collision contract changed")
    require(manifest.get("material", {}).get("unreal_path") == MATERIAL_PATH, "material path changed")
    records = manifest.get("assets", [])
    require(len(records) == 1 and records[0].get("name") == NAME, "unexpected asset set")
    record = records[0]
    require(record.get("material_slot") == MATERIAL_NAME, "material slot changed")
    require([lod.get("lod") for lod in record.get("lods", [])] == [0], "one LOD is required")
    lod = record["lods"][0]
    path = (ROOT / lod.get("file", "")).resolve()
    require(path.parent == (SOURCE / "FBX").resolve() and path.suffix.lower() == ".fbx",
            "FBX path escaped the owned source directory")
    require(path.is_file() and sha256(path) == lod.get("sha256"), "FBX hash differs from manifest")
    require(50 <= lod.get("triangles", 0) <= 100, "foliage triangle budget exceeded")
    minimum, maximum, size = (lod.get(key, []) for key in ("bounds_min_cm", "bounds_max_cm", "size_cm"))
    require(len(minimum) == len(maximum) == len(size) == 3
            and all(math.isfinite(value) for value in minimum + maximum + size)
            and all(value > 0 for value in size), "invalid mesh dimensions")
    require(abs(minimum[2]) < .002 and abs(minimum[0] + maximum[0]) < .002
            and abs(minimum[1] + maximum[1]) < .002, "mesh is not bottom-centered")
    require(abs(max(size[0], size[1]) - 40) < .002 and 20 <= size[2] <= 55,
            "native foliage size is invalid")
    roundtrip = lod.get("fbx_roundtrip", {})
    require(roundtrip.get("verified") is True and roundtrip.get("vertex_colors") is True
            and roundtrip.get("triangles") == lod["triangles"]
            and roundtrip.get("material_sections") == 1 and roundtrip.get("uv_channels") == 1,
            "missing FBX round-trip proof")
    lod["_source"] = path
    return manifest, record


def create_material():
    helper = load_helper("unreal_visual_upgrade.py", "cinder_frontier_foliage_graph")
    helper.OWNER, helper.VERSION, helper.MATERIALS = OWNER, VERSION, MATERIALS
    graph = helper.Graph(MATERIAL_NAME)
    helper.set_checked(graph.material, "two_sided", True)
    authored = graph.node("Authored blade height and dry tone", "VertexColor")
    olive = graph.node("Olive blade root to tip", "LinearInterpolate")
    graph.link(graph.color("FoliageRoot", (.08, .105, .040)), olive, "A", "RGB")
    graph.link(graph.color("FoliageTip", (.205, .245, .105)), olive, "B", "RGB")
    graph.link(authored, olive, "Alpha", "R")
    tone = graph.node("Occasional muted ochre stalk", "LinearInterpolate")
    graph.link(olive, tone, "A")
    graph.link(graph.color("FoliageDry", (.31, .25, .115)), tone, "B", "RGB")
    graph.link(authored, tone, "Alpha", "G")

    fog_texture = unreal.load_asset("/Game/Art/Textures/VisualUpgrade/T_CinderFogDefaultV2")
    require(isinstance(fog_texture, unreal.Texture2D), "missing default fog texture")
    require(not fog_texture.get_editor_property("srgb")
            and fog_texture.get_editor_property("address_x") == unreal.TextureAddress.TA_CLAMP
            and fog_texture.get_editor_property("address_y") == unreal.TextureAddress.TA_CLAMP,
            "fog texture must stay linear and clamped")
    fog = graph.sample("FogMask", fog_texture, graph.world_uv(prefix="Foliage fog "), "LINEAR_COLOR")
    visible = graph.node("Visible foliage fraction", "OneMinus")
    graph.link(fog, visible, "Input", "R")
    diffuse = graph.node("Fog gated foliage diffuse", "Multiply")
    graph.link(tone, diffuse, "A")
    graph.link(visible, diffuse, "B")
    graph.output(diffuse, "BASE_COLOR")
    fog_color = graph.node("Unknown and explored foliage fog", "LinearInterpolate")
    graph.link(graph.color("UnknownFog", (.010, .017, .027)), fog_color, "A", "RGB")
    graph.link(graph.color("ExploredFog", (.027, .041, .049)), fog_color, "B", "RGB")
    graph.link(fog, fog_color, "Alpha", "G")
    fog_emission = graph.node("Foliage fog independent of lighting", "Multiply")
    graph.link(fog_color, fog_emission, "A")
    graph.link(fog, fog_emission, "B", "R")
    graph.output(fog_emission, "EMISSIVE_COLOR")

    # No material Time node: paused/replayed games use the same wind state.
    # Subtracting the rest wave keeps WindTime=0 exactly at the authored shape.
    # |sin(a+b)-sin(a)| <= 2, so 1.4 * sqrt(1 + .35^2) * 2 < 3 cm.
    world = graph.node("Foliage wind world position", "WorldPosition")
    phase = graph.node("Per-position wind phase", "DotProduct")
    graph.link(world, phase, "A")
    graph.link(graph.node("Wind spatial frequency", "Constant3Vector",
                          constant=unreal.LinearColor(.008, .011, 0, 0)), phase, "B")
    time = graph.node("Slow simulation wind cycle", "Multiply", const_b=.16)
    graph.link(graph.scalar("WindTime", 0.0), time, "A")
    animated_phase = graph.node("Animated wind phase", "Add")
    graph.link(phase, animated_phase, "A")
    graph.link(time, animated_phase, "B")
    rest_sine = graph.node("Wind at authored rest", "Sine", period=1.0)
    animated_sine = graph.node("Wind at simulation time", "Sine", period=1.0)
    graph.link(phase, rest_sine, "Input")
    graph.link(animated_phase, animated_sine, "Input")
    displacement = graph.node("Wind relative to rest", "Subtract")
    graph.link(animated_sine, displacement, "A")
    graph.link(rest_sine, displacement, "B")
    root_mask = graph.node("Pinned roots and flexible tips", "Multiply")
    graph.link(authored, root_mask, "A", "R")
    graph.link(authored, root_mask, "B", "R")
    masked = graph.node("Root masked wind", "Multiply")
    graph.link(displacement, masked, "A")
    graph.link(root_mask, masked, "B")
    strength = graph.node("Bounded wind centimeters", "Multiply", const_b=1.4)
    graph.link(masked, strength, "A")
    wind_vector = graph.node("Horizontal foliage sway", "Multiply")
    graph.link(strength, wind_vector, "A")
    graph.link(graph.node("Wind direction", "Constant3Vector",
                          constant=unreal.LinearColor(1, .35, 0, 0)), wind_vector, "B")
    graph.output(wind_vector, "WORLD_POSITION_OFFSET")
    graph.output(graph.node("Matte foliage", "Constant", r=.95), "ROUGHNESS")
    graph.output(graph.node("Nonmetal foliage", "Constant", r=0), "METALLIC")
    graph.output(graph.node("Restrained leaf specular", "Constant", r=.15), "SPECULAR")
    result = graph.finish()
    result["runtime_contract"] = {
        "textures": ["FogMask"], "texture_samples": 1, "fog_sampler": "linear color, texture clamp",
        "vertex_color": {"R": "height bending mask", "G": "dry tone"},
        "WindTime": "simulation seconds; zero is authored rest; no engine Time node",
        "world_size_parameter": "CinderWorldSizeInverse", "two_sided": True, "opacity": "opaque",
        "maximum_wind_displacement_cm": 2.8 * math.sqrt(1 + .35 ** 2), "wind_z_cm": 0,
        "root_wind_displacement_cm": 0, "phase_cycles_per_second": .16}
    tag(graph.material, "FoliageOwner", OWNER)
    tag(graph.material, "SourceLicense", "original Cinderline procedural material")
    tag(graph.material, "GeneratorSHA256", sha256(__file__))
    tag(graph.material, "MaterialContract", "opaque two-sided; root-pinned simulation wind under3cm; clamped pixel fog")
    require(unreal.EditorAssetLibrary.save_loaded_asset(graph.material), "could not save foliage material")
    return graph.material, result


def import_mesh(record, replace, shared):
    source = record["lods"][0]["_source"]
    if replace:
        existing = unreal.load_asset(MESHES + "/" + NAME)
        require(isinstance(existing, unreal.StaticMesh) and metadata(existing, "FoliageOwner") == OWNER,
                "refusing to replace an unrelated mesh")
        data = existing.get_editor_property("asset_import_data")
        if not isinstance(data, unreal.FbxStaticMeshImportData):
            data = unreal.FbxStaticMeshImportData(outer=existing)
            existing.set_editor_property("asset_import_data", data)
        shared.configure_static_mesh_import_data(data)
        data.scripted_add_filename(str(source), 0, "")
    task = unreal.AssetImportTask()
    for key, value in {"filename": str(source), "destination_path": MESHES,
        "destination_name": NAME, "automated": True, "async_": False, "save": False,
        "replace_existing": replace, "replace_existing_settings": replace,
        "factory": unreal.FbxFactory(), "options": shared.import_options()}.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    expected = MESHES + "/" + NAME + "." + NAME
    meshes = [asset for asset in task.get_objects()
              if isinstance(asset, unreal.StaticMesh) and asset.get_path_name() == expected]
    require(len(meshes) == 1, "did not import exactly one named static mesh")
    data = meshes[0].get_editor_property("asset_import_data")
    require(isinstance(data, unreal.FbxStaticMeshImportData)
            and data.get_editor_property("vertex_color_import_option") == unreal.VertexColorImportOption.REPLACE,
            "legacy FBX vertex color replacement did not persist")
    return meshes[0]


def verify_mesh(mesh, record, material, subsystem, shared):
    lod = record["lods"][0]
    require(mesh.get_num_lods() == 1 and mesh.get_num_triangles(0) == lod["triangles"],
            "triangle count or LOD count differs")
    require(mesh.get_num_sections(0) == 1 and len(mesh.get_editor_property("static_materials")) == 1,
            "mesh must have one material section")
    mesh.set_material(0, material)
    require(mesh.get_material(0) == material, "material assignment failed")
    require(subsystem.has_vertex_colors(mesh), "mesh lost vertex colors")
    color_report = shared.verify_lod_colors(mesh, record)
    for entry in color_report:
        for stage in ("source", "render"):
            minimum, maximum = entry[stage]["rgba_min"], entry[stage]["rgba_max"]
            require(abs(minimum[0]) < .005 and maximum[0] > .99, "lost root-to-tip wind mask")
            require(maximum[1] - minimum[1] > .7, "lost olive and dry blade tone variation")
    subsystem.remove_collisions(mesh)
    subsystem.enable_section_collision(mesh, False, 0, 0)
    require(not subsystem.is_section_collision_enabled(mesh, 0, 0)
            and subsystem.get_simple_collision_count(mesh) == 0, "collision could not be disabled")
    require(not mesh.get_editor_property("nanite_settings").get_editor_property("enabled"), "Nanite is enabled")
    bounds = mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    size = [extent.x * 2, extent.y * 2, extent.z * 2]
    require(all(abs(a - b) < .5 for a, b in zip(size, lod["size_cm"])), "imported dimensions differ")
    require(abs(origin.x) < .5 and abs(origin.y) < .5 and abs(origin.z - extent.z) < .5,
            "bottom-center origin did not survive import")
    return {"path": mesh.get_path_name(), "dimensions_cm": size, "triangles": [lod["triangles"]],
            "sections": 1, "vertex_colors_per_lod": color_report, "nanite": False,
            "simple_collisions": 0, "section_collision": False, "material": material.get_path_name()}


def run():
    SUCCESS.unlink(missing_ok=True)
    report = {"success": False, "utc": datetime.now(timezone.utc).isoformat(), "owner": OWNER,
              "version": VERSION, "assets": [], "manifest_sha256": sha256(MANIFEST) if MANIFEST.is_file() else None}
    try:
        manifest, record = load_manifest()
        shared = load_helper("unreal_canyon_assets.py", "cinder_frontier_shared_fbx")
        replace = os.environ.get("CINDER_REIMPORT_FRONTIER_FOLIAGE") == "1"
        for directory in (MESHES, MATERIALS):
            unreal.EditorAssetLibrary.make_directory(directory)
        material, report["material"] = create_material()
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        require(subsystem is not None, "StaticMeshEditorSubsystem unavailable")
        with shared.legacy_fbx_import(report):
            mesh = unreal.load_asset(MESHES + "/" + NAME)
            if mesh is not None:
                require(isinstance(mesh, unreal.StaticMesh) and metadata(mesh, "FoliageOwner") == OWNER,
                        "unrelated asset occupies foliage destination")
                require(replace or metadata(mesh, "SourceSha256LOD0") == record["lods"][0]["sha256"],
                        "source changed; set CINDER_REIMPORT_FRONTIER_FOLIAGE=1")
            if mesh is None or replace:
                mesh = import_mesh(record, mesh is not None, shared)
            result = verify_mesh(mesh, record, material, subsystem, shared)
            tag(mesh, "FoliageOwner", OWNER)
            tag(mesh, "VisualVersion", VERSION)
            tag(mesh, "SourceLicense", manifest["license"])
            tag(mesh, "GeometryContract", "cm,+X,+Z,bottom-center; opaque single-LOD blades; root-pinned wind")
            tag(mesh, "SourceSha256LOD0", record["lods"][0]["sha256"])
            require(unreal.EditorAssetLibrary.save_loaded_asset(mesh), "could not save foliage mesh")
            report["assets"].append(result)
        report["success"] = True
        SUCCESS.write_text("ok\n", encoding="utf-8")
        unreal.log("CINDERLINE_FRONTIER_FOLIAGE_IMPORT_OK: " + str(REPORT))
    except Exception as error:
        report["error"], report["traceback"] = str(error), traceback.format_exc()
        raise
    finally:
        REPORT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    run()
