"""Run inside Unreal Editor: create original material and a blank native-game map.

Idempotent. The existing map is retained unless CINDER_REBUILD_CONTENT=1. Material edits are retained.
The game mode creates the battlefield, lights, instanced silhouettes and camera.
"""
import importlib.util
import os
import unreal


def import_texture(relative_path, destination, name, terrain=False):
    asset_path = destination + "/" + name
    texture = unreal.load_asset(asset_path)
    if texture is None:
        source = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), relative_path)
        if not os.path.isfile(source):
            raise RuntimeError("Missing source artwork: " + source)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source)
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        task.set_editor_property("replace_existing", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(asset_path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError("Could not import texture: " + asset_path)
    texture.set_editor_property("srgb", True)
    if terrain:
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        # Resize during the engine texture build; retain the original generated PNG.
        texture.set_editor_property("power_of_two_mode", unreal.TexturePowerOfTwoSetting.RESIZE_TO_SPECIFIC_RESOLUTION)
        texture.set_editor_property("resize_during_build_x", 1024)
        texture.set_editor_property("resize_during_build_y", 1024)
        texture.set_editor_property("max_texture_size", 1024)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    else:
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("never_stream", True)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture):
        raise RuntimeError("Could not save texture: " + asset_path)
    return texture


def bootstrap():
    rebuild = os.environ.get("CINDER_REBUILD_CONTENT") == "1"
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    unreal.EditorAssetLibrary.make_directory("/Game/Generated")
    unreal.EditorAssetLibrary.make_directory("/Game/Maps")
    material_path = "/Game/Generated/M_CinderTint"
    material = unreal.load_asset(material_path)
    if material is None:
        material = tools.create_asset("M_CinderTint", "/Game/Generated", unreal.Material, unreal.MaterialFactoryNew())
        tint = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -320, 0)
        tint.set_editor_property("parameter_name", "Tint")
        tint.set_editor_property("default_value", unreal.LinearColor(0.08, 0.85, 0.70, 1.0))
        roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -320, 160)
        roughness.set_editor_property("r", 0.74)
        unreal.MaterialEditingLibrary.connect_material_property(tint, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        # A small emissive term preserves faction readability with mobile lighting.
        multiply = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, 240)
        multiply.set_editor_property("const_b", 0.12)
        unreal.MaterialEditingLibrary.connect_material_expressions(tint, "RGB", multiply, "A")
        unreal.MaterialEditingLibrary.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    unreal.MaterialEditingLibrary.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        raise RuntimeError("Could not save /Game/Generated/M_CinderTint")

    basalt = import_texture("RawAssets/Textures/T_CinderBasalt.png", "/Game/Art/Textures", "T_CinderBasalt", terrain=True)
    import_texture("RawAssets/UI/T_CinderBackdrop.png", "/Game/Art/UI", "T_CinderBackdrop")
    ground_path = "/Game/Generated/M_CinderGround"
    ground = unreal.load_asset(ground_path)
    if ground is None:
        ground = tools.create_asset("M_CinderGround", "/Game/Generated", unreal.Material, unreal.MaterialFactoryNew())
        sample = unreal.MaterialEditingLibrary.create_material_expression(ground, unreal.MaterialExpressionTextureSample, -280, 0)
        sample.set_editor_property("texture", basalt)
        coords = unreal.MaterialEditingLibrary.create_material_expression(ground, unreal.MaterialExpressionTextureCoordinate, -500, 0)
        coords.set_editor_property("u_tiling", 8.0)
        coords.set_editor_property("v_tiling", 8.0)
        unreal.MaterialEditingLibrary.connect_material_expressions(coords, "", sample, "UVs")
        unreal.MaterialEditingLibrary.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = unreal.MaterialEditingLibrary.create_material_expression(ground, unreal.MaterialExpressionConstant, -280, 180)
        rough.set_editor_property("r", 0.92)
        unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.set_base_material_usage(ground, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
    unreal.MaterialEditingLibrary.recompile_material(ground)
    if not unreal.EditorAssetLibrary.save_loaded_asset(ground):
        raise RuntimeError("Could not save " + ground_path)

    model_helper_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "unreal_model_assets.py")
    model_spec = importlib.util.spec_from_file_location("cinderline_model_assets", model_helper_path)
    model_helper = importlib.util.module_from_spec(model_spec)
    model_spec.loader.exec_module(model_helper)
    model_helper.import_model_assets(reimport=os.environ.get("CINDER_REIMPORT_MODELS") == "1", required=False)

    # The legacy model importer refreshes the five slot instances. Reapply the
    # explicit V2 contract once installed so future bootstraps retain that upgrade.
    if os.environ.get("CINDER_VISUAL_UPGRADE") == "1" or unreal.EditorAssetLibrary.does_asset_exist("/Game/Art/Materials/M_CinderModelV2"):
        visual_spec = importlib.util.spec_from_file_location("cinderline_visual_upgrade", os.path.join(os.path.dirname(__file__), "unreal_visual_upgrade.py"))
        visual_helper = importlib.util.module_from_spec(visual_spec)
        visual_spec.loader.exec_module(visual_helper)
        visual_helper.upgrade_visual_assets()

    audio_helper_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "unreal_audio_assets.py")
    audio_spec = importlib.util.spec_from_file_location("cinderline_audio_assets", audio_helper_path)
    audio_helper = importlib.util.module_from_spec(audio_spec)
    audio_spec.loader.exec_module(audio_helper)
    audio_helper.import_audio()

    map_path = "/Game/Maps/Frontier"
    if rebuild or not unreal.EditorAssetLibrary.does_asset_exist(map_path):
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        mode = unreal.load_class(None, "/Script/Cinderline.CinderGameMode")
        if mode is None:
            raise RuntimeError("Cinderline native module is not loaded. Build the Editor target first.")
        world.get_world_settings().set_editor_property("default_game_mode", mode)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            raise RuntimeError("Could not save /Game/Maps/Frontier")
    unreal.log("CINDERLINE_BOOTSTRAP_OK: artwork, models, audio, materials and native battlefield map are ready")


bootstrap()
