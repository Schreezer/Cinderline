"""Add a restrained three-sample PBR finish to M_CinderModelV3.

Run this after scripts/unreal_visual_target_materials.py. Importing the module
has no side effects. Call augment_visual_target_model_finish() inside Unreal.
The operation is deterministic and replaces only nodes carrying this stage's
label, so repeated runs do not accumulate graph expressions.
"""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json

import unreal


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "RawAssets/VisualTarget/Models/SurfaceFinish"
DESTINATION = "/Game/Art/VisualTarget/Textures/ModelFinish"
MATERIAL_PATH = "/Game/Art/VisualTarget/Materials/M_CinderModelV3"
REPORT = ROOT / "artifacts/visual-target/models/model-finish-import.json"
OWNER = "scripts/unreal_visual_target_model_finish.py"
VERSION = "polyhaven-blue-metal-finish-v1"
PREFIX = "VT Model finish | "
LIB = unreal.MaterialEditingLibrary

TEXTURES = (
    ("T_VT_ModelFinish_Color", "blue_metal_plate_diff_1k.jpg", "Color",
     "a0162bffce47d4a35613a12af22571b28c18412dc5805cbb69eac343554ef750"),
    ("T_VT_ModelFinish_Normal", "blue_metal_plate_nor_dx_1k.jpg", "Normal",
     "578d18a2f105b78b23b858e638588f39576ff1d0c7a27fea6b71e76385dc08ec"),
    ("T_VT_ModelFinish_Roughness", "blue_metal_plate_rough_1k.jpg", "Roughness",
     "37168cf57144db28dd0743dab47fbebc09147fac919d5e2b5610b069c0b13a46"),
)


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline model finish: " + message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tag(asset, name, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Cinderline." + name, str(value))


def metadata(asset, name):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + name))


def save(asset):
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset), "could not save " + asset.get_path_name())


def import_texture(name, filename, role, expected_hash):
    source = SOURCE / filename
    require(source.is_file() and digest(source) == expected_hash, "source hash changed for " + filename)
    path = DESTINATION + "/" + name
    texture = unreal.load_asset(path)
    if texture is not None:
        require(isinstance(texture, unreal.Texture2D) and metadata(texture, "VisualOwner") == OWNER,
                "refusing to replace unrelated texture " + path)
    if texture is None or metadata(texture, "SourceSHA256") != expected_hash:
        task = unreal.AssetImportTask()
        for key, value in {
            "filename": str(source), "destination_path": DESTINATION,
            "destination_name": name, "automated": True, "async_": False,
            "save": False, "replace_existing": texture is not None,
            "replace_existing_settings": texture is not None,
        }.items():
            task.set_editor_property(key, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(path)
        require(isinstance(texture, unreal.Texture2D), "texture import failed for " + path)
    settings = {
        "compression_settings": {
            "Color": unreal.TextureCompressionSettings.TC_DEFAULT,
            "Normal": unreal.TextureCompressionSettings.TC_NORMALMAP,
            "Roughness": unreal.TextureCompressionSettings.TC_MASKS,
        }[role],
        "srgb": role == "Color",
        "lod_group": (unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP
                      if role == "Normal" else unreal.TextureGroup.TEXTUREGROUP_WORLD),
        "max_texture_size": 1024,
        "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP,
        "never_stream": False,
        "virtual_texture_streaming": False,
        "address_x": unreal.TextureAddress.TA_WRAP,
        "address_y": unreal.TextureAddress.TA_WRAP,
        "filter": unreal.TextureFilter.TF_DEFAULT,
    }
    if role == "Normal":
        settings["flip_green_channel"] = False
    for key, value in settings.items():
        texture.set_editor_property(key, value)
        require(texture.get_editor_property(key) == value, name + " did not retain " + key)
    require(texture.blueprint_get_size_x() == 1024 and texture.blueprint_get_size_y() == 1024,
            name + " did not import at 1024 square")
    tag(texture, "VisualOwner", OWNER)
    tag(texture, "VisualVersion", VERSION)
    tag(texture, "TextureRole", role)
    tag(texture, "SourceSHA256", expected_hash)
    tag(texture, "SourceLicense", "CC0-1.0")
    tag(texture, "SourceURL", "https://polyhaven.com/a/blue_metal_plate")
    save(texture)
    return texture


def expression(material, kind, label, x, y, **properties):
    node = LIB.create_material_expression(material, getattr(unreal, "MaterialExpression" + kind), x, y)
    require(node is not None, "could not create " + kind)
    node.set_editor_property("desc", PREFIX + label)
    for key, value in properties.items():
        node.set_editor_property(key, value)
    return node


def link(source, target, pin, output=""):
    require(LIB.connect_material_expressions(source, output, target, "" if pin == "Input" else pin),
            "could not connect " + str(source) + " to " + pin)


def find_original(material, label):
    matches = [node for node in LIB.get_material_expressions(material)
               if str(node.get_editor_property("desc")) == label]
    require(len(matches) == 1, "expected one original node named " + label)
    return matches[0]


def scalar(material, name, value, x, y):
    return expression(material, "ScalarParameter", name, x, y,
                      parameter_name=name, default_value=value)


def augment_visual_target_model_finish():
    """Import the CC0 maps and replace this stage's prior graph augmentation."""
    unreal.EditorAssetLibrary.make_directory(DESTINATION)
    textures = {role: import_texture(name, filename, role, expected_hash)
                for name, filename, role, expected_hash in TEXTURES}
    material = unreal.load_asset(MATERIAL_PATH)
    require(isinstance(material, unreal.Material), "main material is missing; run the material stage first")

    expected_vectors = {"Tint"}
    expected_scalars = {"Roughness", "Metallic", "GlowIntensity", "FinishVariation",
                        "SurfaceWear", "AO"}
    require(expected_vectors.issubset({str(name) for name in LIB.get_vector_parameter_names(material)}),
            "main material lost Tint")
    require(expected_scalars.issubset({str(name) for name in LIB.get_scalar_parameter_names(material)}),
            "main material lost its scalar contract")
    original_color = find_original(material, "Finish color with baked recess AO")
    original_roughness = find_original(material, "Physical roughness bounds")

    # Disconnect our possible previous outputs first, then remove a detached
    # snapshot of only our nodes. The main material generator's graph survives.
    for prop in (unreal.MaterialProperty.MP_BASE_COLOR,
                 unreal.MaterialProperty.MP_ROUGHNESS,
                 unreal.MaterialProperty.MP_NORMAL):
        LIB.disconnect_material_property(material, prop)
    old_nodes = [node for node in list(LIB.get_material_expressions(material))
                 if str(node.get_editor_property("desc")).startswith(PREFIX)]
    for node in old_nodes:
        LIB.delete_material_expression(material, node)
    require(not [node for node in LIB.get_material_expressions(material)
                 if str(node.get_editor_property("desc")).startswith(PREFIX)],
            "prior finish nodes were not fully removed")

    uv = expression(material, "TextureCoordinate", "Authored UV0 at compact finish scale", -1200, -620,
                    coordinate_index=0, u_tiling=3.2, v_tiling=3.2)
    color_sample = expression(material, "TextureSampleParameter2D", "Paint wear color sample", -980, -720,
                              parameter_name="ModelFinishColor", texture=textures["Color"],
                              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal_sample = expression(material, "TextureSampleParameter2D", "Fine tangent normal sample", -980, -510,
                               parameter_name="ModelFinishNormal", texture=textures["Normal"],
                               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    rough_sample = expression(material, "TextureSampleParameter2D", "Metal roughness sample", -980, -300,
                              parameter_name="ModelFinishRoughness", texture=textures["Roughness"],
                              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    for sample in (color_sample, normal_sample, rough_sample):
        link(uv, sample, "UVs")

    neutral = expression(material, "Desaturation", "Neutral paint wear value", -730, -720)
    link(color_sample, neutral, "Input", "RGB")
    link(scalar(material, "ModelFinishDesaturation", 1.0, -950, -870), neutral, "Fraction")
    centered_color = expression(material, "Add", "Centered paint wear", -500, -720, const_b=-0.5)
    link(neutral, centered_color, "A")
    color_strength = expression(material, "Multiply", "Restrained paint wear contrast", -280, -720)
    link(centered_color, color_strength, "A")
    link(scalar(material, "ModelFinishWearStrength", 0.16, -500, -870), color_strength, "B")
    color_gain = expression(material, "Add", "Paint wear gain around one", -60, -720, const_b=1.0)
    link(color_strength, color_gain, "A")
    bounded_gain = expression(material, "Clamp", "Bounded paint wear gain", 160, -720,
                              min_default=0.90, max_default=1.09)
    link(color_gain, bounded_gain, "Input")
    final_color = expression(material, "Multiply", "Tint and AO with subtle paint wear", 390, -720)
    link(original_color, final_color, "A")
    link(bounded_gain, final_color, "B")
    require(LIB.connect_material_property(final_color, "", unreal.MaterialProperty.MP_BASE_COLOR),
            "could not connect augmented base color")

    centered_rough = expression(material, "Add", "Centered texture roughness", -500, -300, const_b=-0.5)
    link(rough_sample, centered_rough, "A", "R")
    rough_strength = expression(material, "Multiply", "Restrained roughness variation", -280, -300)
    link(centered_rough, rough_strength, "A")
    link(scalar(material, "ModelFinishRoughnessStrength", 0.22, -500, -130), rough_strength, "B")
    varied_rough = expression(material, "Add", "Instance roughness plus metal variation", -60, -300)
    link(original_roughness, varied_rough, "A")
    link(rough_strength, varied_rough, "B")
    bounded_rough = expression(material, "Clamp", "Model finish roughness bounds", 160, -300,
                               min_default=0.14, max_default=0.94)
    link(varied_rough, bounded_rough, "Input")
    require(LIB.connect_material_property(bounded_rough, "", unreal.MaterialProperty.MP_ROUGHNESS),
            "could not connect augmented roughness")

    flat = expression(material, "Constant3Vector", "Flat tangent normal", -500, 20,
                      constant=unreal.LinearColor(0, 0, 1, 0))
    restrained_normal = expression(material, "LinearInterpolate", "Restrained fine normal", -260, 20)
    link(flat, restrained_normal, "A")
    link(normal_sample, restrained_normal, "B", "RGB")
    link(scalar(material, "ModelFinishNormalStrength", 0.24, -500, 170), restrained_normal, "Alpha")
    normalized = expression(material, "Normalize", "Normalized fine tangent normal", -20, 20)
    link(restrained_normal, normalized, "VectorInput")
    require(LIB.connect_material_property(normalized, "", unreal.MaterialProperty.MP_NORMAL),
            "could not connect augmented normal")

    errors = list(LIB.recompile_material(material))
    require(not errors, "material compile failed: " + "; ".join(str(error) for error in errors))
    samples = [node for node in LIB.get_material_expressions(material)
               if str(node.get_editor_property("desc")).startswith(PREFIX)
               and isinstance(node, unreal.MaterialExpressionTextureSample)]
    require(len(samples) == 3, "finish must use exactly three texture samples")
    tag(material, "ModelFinishOwner", OWNER)
    tag(material, "ModelFinishVersion", VERSION)
    tag(material, "ModelFinishTextureSamples", 3)
    save(material)

    report = {
        "success": True, "utc": datetime.now(timezone.utc).isoformat(),
        "owner": OWNER, "version": VERSION, "material": MATERIAL_PATH,
        "source_asset": "https://polyhaven.com/a/blue_metal_plate",
        "license": "CC0-1.0", "texture_samples": 3,
        "mapping": "authored UV0 at 3.2 tiling",
        "preserved": ["Tint", "five material-slot instances", "vertex AO alpha", "GlowIntensity"],
        "parameters": {"ModelFinishDesaturation": 1.0, "ModelFinishWearStrength": 0.16,
                       "ModelFinishRoughnessStrength": 0.22, "ModelFinishNormalStrength": 0.24},
        "textures": [{"path": textures[role].get_path_name(), "role": role,
                      "source": filename, "sha256": expected_hash}
                     for _name, filename, role, expected_hash in TEXTURES],
        "replaced_prior_finish_nodes": len(old_nodes),
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CINDERLINE_MODEL_FINISH_READY " + json.dumps(report, sort_keys=True))
    return report


if __name__ == "__main__":
    augment_visual_target_model_finish()
