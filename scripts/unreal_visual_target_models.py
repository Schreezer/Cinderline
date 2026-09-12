"""Import and validate Cinderline's visual-target replacement meshes in Unreal.

Importing this module has no side effects. From an Unreal Python command, call:

    import unreal_visual_target_models as visual_models
    visual_models.import_visual_target_models(reimport=True)

Assets go to /Game/Art/VisualTarget/Models. The battlefield loader must opt into
that path before these replace /Game/Art/Models at runtime.
"""
import hashlib
import json
import math
import os

import unreal


DESTINATION = "/Game/Art/VisualTarget/Models"
MATERIAL_ROOT = "/Game/Art/VisualTarget/Materials"
SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")
LOD_TRIANGLE_PERCENTAGES = (1.0, 0.68, 0.36)
LOD_SCREEN_SIZES = (1.0, 0.08, 0.025)
EXPECTED = {
    "SM_Anchor": "Headquarters",
    "SM_Kiln": "Foundry",
    "SM_Siphon": "Processor",
    "SM_Crucible": "MotorPool",
    "SM_Resonator": "Laboratory",
    "SM_Ward": "Turret",
    "SM_Ore": "Resource",
}


def _require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline visual-target models: " + message)


def _manifest(project_root):
    path = os.path.join(project_root, "RawAssets", "VisualTarget", "Models", "manifest.json")
    _require(os.path.isfile(path), "manifest is missing: " + path)
    with open(path, encoding="utf-8") as source:
        data = json.load(source)
    _require(data.get("unit") == "centimeter", "manifest unit must be centimeter")
    _require(data.get("forward_axis") == "+X" and data.get("up_axis") == "+Z", "manifest axes changed")
    _require(data.get("origin") == "bottom center of bounds", "manifest origin changed")
    _require(tuple(data.get("material_slots", ())) == SLOTS, "manifest material order changed")
    records = data.get("assets", [])
    _require({row.get("name") for row in records} == set(EXPECTED), "manifest must contain the seven replacements")
    _require(len(records) == len(EXPECTED), "manifest contains duplicate replacement records")
    for row in records:
        name = row["name"]
        _require(row.get("kind") == EXPECTED[name], name + " kind changed")
        _require(tuple(row.get("material_slots", ())) == SLOTS, name + " slot order changed")
        dimensions = row.get("dimensions_cm", [])
        _require(len(dimensions) == 3 and all(math.isfinite(v) and v > 0 for v in dimensions), name + " has invalid dimensions")
        _require(isinstance(row.get("triangles"), int) and row["triangles"] > 0, name + " has invalid triangles")
    return data


def _import(source, name, reimport):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("async_", False)
    task.set_editor_property("replace_existing", reimport)
    task.set_editor_property("replace_existing_settings", reimport)
    task.set_editor_property("save", False)
    task.set_editor_property("factory", unreal.FbxFactory())
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("create_physics_asset", False)
    options.set_editor_property("override_full_name", True)
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    static_options = options.get_editor_property("static_mesh_import_data")
    static_options.set_editor_property("convert_scene", True)
    static_options.set_editor_property("convert_scene_unit", True)
    static_options.set_editor_property("combine_meshes", True)
    static_options.set_editor_property("force_front_x_axis", False)
    static_options.set_editor_property("import_translation", unreal.Vector(0, 0, 0))
    static_options.set_editor_property("import_rotation", unreal.Rotator(0, 0, 0))
    static_options.set_editor_property("transform_vertex_to_absolute", True)
    static_options.set_editor_property("bake_pivot_in_vertex", False)
    static_options.set_editor_property("reorder_material_to_fbx_order", True)
    static_options.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    static_options.set_editor_property("generate_lightmap_u_vs", False)
    static_options.set_editor_property("auto_generate_collision", False)
    static_options.set_editor_property("build_nanite", False)
    static_options.set_editor_property("remove_degenerates", True)
    static_options.set_editor_property("import_uniform_scale", 1.0)
    vertex_color_option = getattr(unreal, "VertexColorImportOption", None)
    _require(vertex_color_option is not None, "editor does not expose VertexColorImportOption")
    static_options.set_editor_property("vertex_color_import_option", vertex_color_option.REPLACE)
    options.set_editor_property("static_mesh_import_data", static_options)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    expected_path = DESTINATION + "/" + name + "." + name
    meshes = [asset for asset in task.get_objects()
              if isinstance(asset, unreal.StaticMesh) and asset.get_path_name() == expected_path]
    _require(len(meshes) == 1, "import did not create exactly one mesh at " + expected_path)
    return meshes[0]


def _triangle_count(mesh, lod=0):
    return int(mesh.get_num_triangles(lod))


def _set_lods(mesh, percentages):
    """Create two reduced LODs when the installed editor exposes reduction APIs."""
    settings_type = getattr(unreal, "StaticMeshReductionSettings", None)
    options_type = getattr(unreal, "StaticMeshReductionOptions", None)
    library = getattr(unreal, "EditorStaticMeshLibrary", None)
    if settings_type is None or options_type is None or library is None or not hasattr(library, "set_lods"):
        unreal.log_warning(mesh.get_name() + ": editor LOD reduction API unavailable; imported LOD0 remains valid")
        return False
    settings = []
    for percentage, screen_size in zip(percentages, LOD_SCREEN_SIZES):
        row = settings_type()
        row.set_editor_property("percent_triangles", float(percentage))
        row.set_editor_property("screen_size", float(screen_size))
        settings.append(row)
    options = options_type()
    options.set_editor_property("auto_compute_lod_screen_size", False)
    options.set_editor_property("reduction_settings", settings)
    count = int(library.set_lods(mesh, options))
    _require(count >= 3, mesh.get_name() + " LOD reducer returned " + str(count) + " LODs")
    return True


def _materials():
    result = {}
    for slot in SLOTS:
        path = MATERIAL_ROOT + "/MI_VT_" + slot
        material = unreal.load_asset(path)
        _require(isinstance(material, unreal.MaterialInterface), "shared material is missing: " + path)
        result[slot] = material
    return result


def _validate(mesh, record):
    bounds = mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    dimensions = (float(extent.x * 2), float(extent.y * 2), float(extent.z * 2))
    expected = tuple(float(value) for value in record["dimensions_cm"])
    tolerance = max(0.1, max(expected) * 0.005)
    _require(all(abs(actual - target) <= tolerance for actual, target in zip(dimensions, expected)),
             record["name"] + " dimensions " + repr(dimensions) + " differ from " + repr(expected))
    _require(abs(origin.x) <= tolerance and abs(origin.y) <= tolerance and abs(origin.z - extent.z) <= tolerance,
             record["name"] + " did not retain its bottom-center origin")
    slots = tuple(str(item.get_editor_property("material_slot_name"))
                  for item in mesh.get_editor_property("static_materials"))
    _require(slots == SLOTS, record["name"] + " material slots are " + repr(slots))
    triangles = _triangle_count(mesh, 0)
    _require(triangles == record["triangles"], record["name"] + " imported " + str(triangles) + " triangles, expected " + str(record["triangles"]))
    _require(mesh.get_num_sections(0) == len(SLOTS), record["name"] + " must retain five populated material sections")
    _require(not mesh.get_editor_property("nanite_settings").get_editor_property("enabled"),
             record["name"] + " unexpectedly enabled Nanite")
    return dimensions, triangles


def import_visual_target_models(project_root=None, *, reimport=True):
    """Import all seven replacements, create LODs when possible, and validate."""
    project_root = os.path.realpath(project_root or os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    manifest = _manifest(project_root)
    unreal.EditorAssetLibrary.make_directory(DESTINATION)
    materials = _materials()
    results = []
    for record in manifest["assets"]:
        name = record["name"]
        source = os.path.realpath(os.path.join(project_root, record["fbx"]))
        _require(os.path.commonpath((project_root, source)) == project_root and os.path.isfile(source), "invalid FBX for " + name)
        with open(source, "rb") as handle:
            source_hash = hashlib.sha256(handle.read()).hexdigest()
        _require(source_hash == record["sha256"], name + " FBX hash differs from manifest")
        mesh = unreal.load_asset(DESTINATION + "/" + name)
        if mesh is None or reimport:
            mesh = _import(source, name, reimport)
        dimensions, triangles = _validate(mesh, record)
        for index, slot in enumerate(SLOTS):
            mesh.set_material(index, materials[slot])
            _require(mesh.get_material(index).get_path_name() == materials[slot].get_path_name(),
                     name + " material mapping failed for " + slot)
        # Keep LOD0 through normal 100-300 px RTS presentation. The manifest's
        # older reduction hint is intentionally superseded by this runtime policy.
        lods_created = _set_lods(mesh, LOD_TRIANGLE_PERCENTAGES)
        unreal.EditorAssetLibrary.set_metadata_tag(mesh, "Cinderline.SourceSHA256", source_hash)
        unreal.EditorAssetLibrary.set_metadata_tag(mesh, "Cinderline.ModelContract", "visual-target-v1")
        _require(unreal.EditorAssetLibrary.save_loaded_asset(mesh), "could not save " + mesh.get_path_name())
        row = {"name": name, "path": mesh.get_path_name(), "dimensions_cm": list(dimensions),
               "triangles_lod0": triangles, "lod_count": mesh.get_num_lods(), "lods_created": lods_created,
               "lod_policy": {"triangle_percentages": list(LOD_TRIANGLE_PERCENTAGES),
                              "screen_sizes": list(LOD_SCREEN_SIZES)},
               "material_slots": list(SLOTS), "passed": True}
        results.append(row)
        unreal.log("CINDERLINE_VISUAL_TARGET_VALIDATED " + json.dumps(row, sort_keys=True))
    evidence = os.path.join(project_root, "artifacts", "visual-target", "models", "unreal-import-results.json")
    os.makedirs(os.path.dirname(evidence), exist_ok=True)
    with open(evidence, "w", encoding="utf-8") as output:
        json.dump({"destination": DESTINATION, "validated": results, "passed": True}, output, indent=2)
        output.write("\n")
    unreal.log("CINDERLINE_VISUAL_TARGET_READY 7/7")
    return results
