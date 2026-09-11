"""Import the Cairn Assembly model pack inside Unreal Editor.

Importing this module has no side effects. Call import_model_assets() from the
project bootstrap after the checkpoint that authorizes asset integration.

The explicit FBX factory selects UE 5.8's legacy FBX path without changing global
Interchange flags. This helper validates dimensions, bottom-centered pivots,
material slots and triangle counts; bounds cannot prove a model's front direction.
Unreal visual verification of facing remains necessary after the first import.
"""
import hashlib
import json
import math
import os

import unreal


MODEL_DESTINATION = "/Game/Art/Models"
MATERIAL_DESTINATION = "/Game/Art/Materials"
BASE_MATERIAL_NAME = "M_CinderModel"
MATERIAL_SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")
EXPECTED_MODELS = {
    "SM_Drudge": "Worker",
    "SM_Ember": "Striker",
    "SM_Needle": "Lancer",
    "SM_Skim": "Scout",
    "SM_Anvil": "Bastion",
    "SM_Cinderthrow": "Mortar",
    "SM_Mend": "Mender",
    "SM_Veil": "Kite",
    "SM_Anchor": "Headquarters",
    "SM_Siphon": "Processor",
    "SM_Kiln": "Foundry",
    "SM_Crucible": "MotorPool",
    "SM_Resonator": "Laboratory",
    "SM_Ward": "Turret",
    "SM_Ore": "Resource",
}


def _require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline model assets: " + message)


def _save(asset):
    _require(unreal.EditorAssetLibrary.save_loaded_asset(asset), "could not save " + asset.get_path_name())


def _read_manifest(project_root, required):
    path = os.path.join(project_root, "RawAssets", "Models", "manifest.json")
    if not os.path.isfile(path):
        _require(not required, "model manifest is missing: " + path)
        unreal.log_warning("Cinderline model pack absent; primitive fallback remains available.")
        return None
    with open(path, encoding="utf-8") as source:
        manifest = json.load(source)
    _require(manifest.get("unit") == "centimeter", "manifest must use centimeters")
    _require(manifest.get("forward_axis") == "+X" and manifest.get("up_axis") == "+Z", "manifest must declare +X forward and +Z up")
    _require(manifest.get("origin") == "bottom center of bounds", "manifest must declare a bottom-center origin")
    _require(tuple(manifest.get("material_slots", ())) == MATERIAL_SLOTS, "manifest material order differs from the runtime contract")
    records = manifest.get("assets", [])
    names = [record.get("name") for record in records]
    _require(len(names) == len(set(names)) and set(names) == set(EXPECTED_MODELS), "manifest must contain each of the 15 expected models exactly once")
    for record in records:
        name = record["name"]
        _require(record.get("kind") == EXPECTED_MODELS[name], name + " has an unexpected simulation kind")
        _require(tuple(record.get("material_slots", ())) == MATERIAL_SLOTS, name + " has an unexpected slot order")
        dimensions = record.get("dimensions_cm", [])
        _require(len(dimensions) == 3 and all(isinstance(value, (int, float)) and math.isfinite(value) and value > 0 for value in dimensions), name + " has invalid dimensions")
        radius = record.get("definition_radius_cm", 0)
        _require(isinstance(radius, (int, float)) and math.isfinite(radius) and radius > 0, name + " has an invalid footprint radius")
        _require(abs(max(dimensions[:2]) - 2 * radius) <= 0.05, name + " footprint does not match twice its definition radius")
        _require(isinstance(record.get("triangles"), int) and record["triangles"] > 0, name + " has no valid triangle count")
        source = os.path.realpath(os.path.join(project_root, record.get("fbx", "")))
        _require(os.path.commonpath((project_root, source)) == project_root and source.lower().endswith(".fbx"), name + " has an invalid source path")
    return manifest


def _create_expression(material, expression_class, x, y):
    expression = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    _require(expression is not None, "could not create a material expression")
    return expression


def _model_materials():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    unreal.EditorAssetLibrary.make_directory(MATERIAL_DESTINATION)
    path = MATERIAL_DESTINATION + "/" + BASE_MATERIAL_NAME
    material = unreal.load_asset(path)
    if material is None:
        material = tools.create_asset(BASE_MATERIAL_NAME, MATERIAL_DESTINATION, unreal.Material, unreal.MaterialFactoryNew())
        _require(isinstance(material, unreal.Material), "could not create " + path)
        tint = _create_expression(material, unreal.MaterialExpressionVectorParameter, -500, 0)
        tint.set_editor_property("parameter_name", "Tint")
        tint.set_editor_property("default_value", unreal.LinearColor(0.04, 0.82, 0.72, 1.0))
        _require(unreal.MaterialEditingLibrary.connect_material_property(tint, "RGB", unreal.MaterialProperty.MP_BASE_COLOR), "could not connect model base color")
        scalar_nodes = {}
        for index, (name, default, output) in enumerate((
            ("Roughness", 0.65, unreal.MaterialProperty.MP_ROUGHNESS),
            ("Metallic", 0.0, unreal.MaterialProperty.MP_METALLIC),
            ("GlowIntensity", 0.0, None),
        )):
            scalar = _create_expression(material, unreal.MaterialExpressionScalarParameter, -500, 160 + index * 120)
            scalar.set_editor_property("parameter_name", name)
            scalar.set_editor_property("default_value", default)
            scalar_nodes[name] = scalar
            if output is not None:
                _require(unreal.MaterialEditingLibrary.connect_material_property(scalar, "", output), "could not connect " + name)
        glow = _create_expression(material, unreal.MaterialExpressionMultiply, -220, 400)
        _require(unreal.MaterialEditingLibrary.connect_material_expressions(tint, "RGB", glow, "A"), "could not connect glow tint")
        _require(unreal.MaterialEditingLibrary.connect_material_expressions(scalar_nodes["GlowIntensity"], "", glow, "B"), "could not connect glow intensity")
        _require(unreal.MaterialEditingLibrary.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "could not connect model emissive")
    _require(isinstance(material, unreal.Material), path + " exists but is not a base material")
    unreal.MaterialEditingLibrary.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
    errors = list(unreal.MaterialEditingLibrary.recompile_material(material))
    _require(not errors, "model material shader compilation failed: " + "; ".join(str(error) for error in errors))
    vector_names = {str(name) for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(material)}
    scalar_names = {str(name) for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)}
    _require("Tint" in vector_names, "model material is missing its Tint vector parameter")
    _require({"Roughness", "Metallic", "GlowIntensity"}.issubset(scalar_names), "model material is missing required scalar parameters")
    _save(material)

    # Slot 3/4 Tint is overridden by shared per-team runtime dynamic instances.
    # Ore uses copper/amber overrides instead of these source teal defaults.
    palettes = (
        ("HullDark", (0.045, 0.08, 0.095), 0.78, 0.08, 0.0),
        ("HullLight", (0.38, 0.51, 0.53), 0.66, 0.10, 0.0),
        ("Metal", (0.13, 0.18, 0.20), 0.34, 0.72, 0.0),
        ("TeamPanel", (0.04, 0.82, 0.72), 0.60, 0.05, 0.08),
        ("CoreGlow", (0.50, 1.0, 0.88), 0.40, 0.0, 1.8),
    )
    instances = {}
    for slot, rgb, roughness, metallic, glow in palettes:
        name = "MI_Cinder" + slot
        instance_path = MATERIAL_DESTINATION + "/" + name
        instance = unreal.load_asset(instance_path)
        if instance is None:
            instance = tools.create_asset(name, MATERIAL_DESTINATION, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        _require(isinstance(instance, unreal.MaterialInstanceConstant), "could not create material instance " + instance_path)
        unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
        # UE 5.8's vector/scalar setters apply the value but return an unchanged
        # false local. Verify the stored value instead of relying on that return.
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, "Tint", unreal.LinearColor(*rgb, 1.0))
        tint = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(instance, "Tint")
        tint_values = (tint.r, tint.g, tint.b, tint.a)
        _require(all(math.isclose(actual, expected, abs_tol=1e-5) for actual, expected in zip(tint_values, (*rgb, 1.0))), "could not read back " + slot + " tint")
        scalar_values = {}
        for parameter, value in (("Roughness", roughness), ("Metallic", metallic), ("GlowIntensity", glow)):
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, parameter, value)
            actual = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, parameter)
            _require(math.isclose(actual, value, abs_tol=1e-5), "could not read back " + slot + " " + parameter)
            scalar_values[parameter] = actual
        _save(instance)
        instances[slot] = instance
        unreal.log("CINDERLINE_MODEL_MATERIAL_VALIDATED " + json.dumps({"name": name, "tint": tint_values, **scalar_values}, sort_keys=True))
    return material, instances


def _fbx_options():
    options = unreal.FbxImportUI()
    for name, value in (
        ("automated_import_should_detect_type", False),
        ("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH),
        ("import_as_skeletal", False),
        ("import_mesh", True),
        ("import_animations", False),
        ("import_materials", False),
        ("import_textures", False),
        ("create_physics_asset", False),
        ("override_full_name", True),
    ):
        options.set_editor_property(name, value)
    data = options.get_editor_property("static_mesh_import_data")
    _require(data is not None, "FbxImportUI did not create static mesh import data")
    for name, value in (
        ("combine_meshes", True),
        ("convert_scene", True),
        ("convert_scene_unit", True),
        # Blender's -Y/Z FBX export already preserves the authored +X-facing mesh
        # through UE's default front-axis conversion. Forcing X adds a quarter turn.
        ("force_front_x_axis", False),
        ("import_translation", unreal.Vector(0, 0, 0)),
        ("import_rotation", unreal.Rotator(0, 0, 0)),
        ("import_uniform_scale", 1.0),
        ("transform_vertex_to_absolute", True),
        ("bake_pivot_in_vertex", False),
        ("reorder_material_to_fbx_order", True),
        ("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS),
        ("auto_generate_collision", False),
        ("generate_lightmap_u_vs", False),
        ("build_nanite", False),
        ("remove_degenerates", True),
    ):
        data.set_editor_property(name, value)
    return options


def _import_mesh(source, name, replace_existing):
    task = unreal.AssetImportTask()
    for property_name, value in (
        ("filename", source),
        ("destination_path", MODEL_DESTINATION),
        ("destination_name", name),
        ("automated", True),
        ("async_", False),
        ("save", False),
        ("replace_existing", replace_existing),
        ("replace_existing_settings", replace_existing),
        ("factory", unreal.FbxFactory()),
        ("options", _fbx_options()),
    ):
        task.set_editor_property(property_name, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    objects = list(task.get_objects())
    expected_path = MODEL_DESTINATION + "/" + name + "." + name
    meshes = [asset for asset in objects if isinstance(asset, unreal.StaticMesh)]
    _require(len(meshes) == 1 and meshes[0].get_path_name() == expected_path,
             name + " did not import as exactly one mesh at " + expected_path + "; got " + repr([asset.get_path_name() for asset in objects]))
    return meshes[0]


def _validate_mesh(mesh, record):
    name = record["name"]
    _require(isinstance(mesh, unreal.StaticMesh), name + " is not a StaticMesh")
    slots = tuple(str(slot.get_editor_property("material_slot_name")) for slot in mesh.get_editor_property("static_materials"))
    bounds = mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    dimensions = (extent.x * 2, extent.y * 2, extent.z * 2)
    triangles = mesh.get_num_triangles(0)
    sections = mesh.get_num_sections(0)
    unreal.log("CINDERLINE_MODEL_INSPECTED " + json.dumps({
        "name": name, "dimensions_cm": dimensions, "triangles": triangles,
        "source_triangles": record["triangles"], "sections": sections,
        "material_slots": slots,
    }, sort_keys=True))
    _require(slots == MATERIAL_SLOTS, name + " imported material slots are " + repr(slots) + "; expected " + repr(MATERIAL_SLOTS))
    expected = record["dimensions_cm"]
    tolerance = max(0.10, max(expected) * 0.005)
    _require(all(math.isfinite(value) and abs(value - target) <= tolerance for value, target in zip(dimensions, expected)),
             name + " imported dimensions " + repr(dimensions) + " differ from manifest " + repr(expected) + "; check FBX units/axis conversion")
    _require(abs(origin.x) <= tolerance and abs(origin.y) <= tolerance and abs(origin.z - extent.z) <= tolerance,
             name + " did not retain its bottom-center origin")
    _require(triangles == record["triangles"], name + " imported " + str(triangles) + " triangles; manifest has " + str(record["triangles"]))
    _require(sections == len(MATERIAL_SLOTS), name + " must retain five populated material sections")
    _require(not mesh.get_editor_property("nanite_settings").get_editor_property("enabled"), name + " unexpectedly enabled Nanite")
    return {
        "name": name,
        "kind": record["kind"],
        "path": mesh.get_path_name(),
        "dimensions_cm": list(dimensions),
        "origin_cm": [origin.x, origin.y, origin.z],
        "triangles": triangles,
        "material_slots": list(slots),
    }


def import_model_assets(project_root=None, *, reimport=False, required=True):
    """Import/validate the current manifest and return assets plus validation data.

    Existing meshes are preserved unless reimport=True. A changed FBX is reported
    rather than silently overwriting a prior import. required=False permits a
    missing manifest or individual missing FBXs so the runtime can use primitives.
    Malformed manifests or invalid imported meshes always raise a precise error.
    No maps, actors, gameplay, project settings or global engine flags are changed.
    """
    project_root = os.path.realpath(project_root or os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    manifest = _read_manifest(project_root, required)
    if manifest is None:
        return {"meshes": {}, "materials": {}, "validated": [], "missing": list(EXPECTED_MODELS)}
    unreal.EditorAssetLibrary.make_directory(MODEL_DESTINATION)
    material, materials = _model_materials()
    result = {"meshes": {}, "materials": materials, "base_material": material, "validated": [], "missing": [], "failed": []}
    for record in manifest["assets"]:
        name = record["name"]
        try:
            source = os.path.realpath(os.path.join(project_root, record["fbx"]))
            asset_path = MODEL_DESTINATION + "/" + name
            mesh = unreal.load_asset(asset_path)
            exists = os.path.isfile(source)
            if not exists and (mesh is None or reimport):
                _require(not required, "missing FBX source for " + name + ": " + source)
                unreal.log_warning("Cinderline missing " + name + "; its primitive fallback remains available.")
                result["missing"].append(name)
                continue
            source_hash = None
            if exists:
                with open(source, "rb") as fbx:
                    source_hash = hashlib.sha256(fbx.read()).hexdigest()
            imported = mesh is None or reimport
            if imported:
                mesh = _import_mesh(source, name, reimport)
            elif source_hash:
                previous_hash = str(unreal.EditorAssetLibrary.get_metadata_tag(mesh, "Cinderline.SourceSHA256"))
                _require(not previous_hash or previous_hash == source_hash, name + " source changed; rerun with reimport=True to replace the prior import")
            validation = _validate_mesh(mesh, record)
            for index, slot_name in enumerate(MATERIAL_SLOTS):
                mesh.set_material(index, materials[slot_name])
            if imported and source_hash:
                unreal.EditorAssetLibrary.set_metadata_tag(mesh, "Cinderline.SourceSHA256", source_hash)
            unreal.EditorAssetLibrary.set_metadata_tag(mesh, "Cinderline.ModelKind", record["kind"])
            unreal.EditorAssetLibrary.set_metadata_tag(mesh, "Cinderline.ModelContract", "cm,+X,+Z,bottom-center,five-slots-v1")
            _save(mesh)
            result["meshes"][name] = mesh
            result["validated"].append(validation)
            unreal.log("CINDERLINE_MODEL_VALIDATED " + json.dumps(validation, sort_keys=True))
        except Exception as error:
            failure = {"name": name, "error": str(error)}
            result["failed"].append(failure)
            unreal.log_error("CINDERLINE_MODEL_FAILED " + json.dumps(failure, sort_keys=True))
    _require(not result["failed"], str(len(result["failed"])) + " model(s) failed validation: " + "; ".join(row["name"] + ": " + row["error"] for row in result["failed"]))
    unreal.log("CINDERLINE_MODELS_READY " + str(len(result["validated"])) + "/" + str(len(EXPECTED_MODELS)))
    return result
