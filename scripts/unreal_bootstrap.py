"""Run inside Unreal Editor: create original material and a blank native-game map.

Idempotent. The existing map is retained unless CINDER_REBUILD_CONTENT=1. Material edits are retained.
The game mode creates the battlefield, lights, instanced silhouettes and camera.
"""
import os
import unreal


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
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.EditorAssetLibrary.save_loaded_asset(material)

    map_path = "/Game/Maps/Frontier"
    if rebuild or not unreal.EditorAssetLibrary.does_asset_exist(map_path):
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        mode = unreal.load_class(None, "/Script/Cinderline.CinderGameMode")
        if mode is None:
            raise RuntimeError("Cinderline native module is not loaded. Build the Editor target first.")
        world.get_world_settings().set_editor_property("default_game_mode", mode)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            raise RuntimeError("Could not save /Game/Maps/Frontier")
    unreal.log("CINDERLINE_BOOTSTRAP_OK: material and native battlefield map are ready")


bootstrap()
