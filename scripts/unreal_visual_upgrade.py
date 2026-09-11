"""Explicit, idempotent desktop material/terrain upgrade for Unreal 5.8.

Run in UnrealEditor-Cmd with -ExecutePythonScript=<absolute path to this file>.
Importing the module does nothing; bootstrap may call upgrade_visual_assets().
Only the named V2 assets and the five existing model material instances change.
Set CINDER_REIMPORT_VISUALS=1 to replace previously imported changed sources.
No maps, gameplay actors, global import flags or unrelated assets are modified.
"""
import hashlib
import importlib.util
import json
import math
import os
import struct
import traceback
import zlib
from datetime import datetime, timezone
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parent.parent
OWNER = "scripts/unreal_visual_upgrade.py"
VERSION = "desktop-pbr-v2.2.1"
MATERIALS = "/Game/Art/Materials"
TEXTURES = "/Game/Art/Textures/VisualUpgrade"
ENVIRONMENT = "/Game/Art/Environment"
SLOTS = ("HullDark", "HullLight", "Metal", "TeamPanel", "CoreGlow")
TILE_CM = 246.0  # Poly Haven dimensions: 2460 mm, not a guessed UV repeat count.
MACRO_TILE_CM = 1050.0
MACRO_BASALT_PATH = "/Game/Art/Textures/T_CinderBasalt"
GROUND_SOURCE = ROOT / "RawAssets/Textures/VisualUpgrade/gravel_floor_04"
GROUND_FILES = {
    "Color": "gravel_floor_04_diff_2k.png",
    "Normal": "gravel_floor_04_nor_dx_2k.png",
    "Roughness": "gravel_floor_04_rough_2k.png",
    "AO": "gravel_floor_04_ao_2k.png",
}
LIB = unreal.MaterialEditingLibrary


def require(condition, message):
    if not condition:
        raise RuntimeError("Cinderline visual upgrade: " + message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(asset):
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset), "could not save " + asset.get_path_name())


def tag(asset, name, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Cinderline." + name, str(value))


def metadata(asset, name):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, "Cinderline." + name))


def set_checked(asset, name, value):
    asset.set_editor_property(name, value)
    require(asset.get_editor_property(name) == value, asset.get_path_name() + " did not retain " + name)


def texture(source, name, role, report, reimport):
    require(source.is_file(), "missing texture source " + str(source))
    digest = sha256(source)
    path = TEXTURES + "/" + name
    asset = unreal.load_asset(path)
    if asset is not None:
        require(isinstance(asset, unreal.Texture2D) and metadata(asset, "VisualOwner") == OWNER,
                "refusing to replace an unrelated texture at " + path)
        require(reimport or metadata(asset, "SourceSHA256") == digest,
                name + " source changed; set CINDER_REIMPORT_VISUALS=1")
    if asset is None or reimport:
        task = unreal.AssetImportTask()
        for key, value in {"filename": str(source), "destination_path": TEXTURES,
                           "destination_name": name, "automated": True, "async_": False,
                           "save": False, "replace_existing": asset is not None,
                           "replace_existing_settings": asset is not None}.items():
            task.set_editor_property(key, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        asset = unreal.load_asset(path)
        require(isinstance(asset, unreal.Texture2D), "texture import failed for " + path)
        tag(asset, "VisualOwner", OWNER)
        tag(asset, "SourceSHA256", digest)
    compression = {
        "Color": unreal.TextureCompressionSettings.TC_DEFAULT,
        "Normal": unreal.TextureCompressionSettings.TC_NORMALMAP,
        "Roughness": unreal.TextureCompressionSettings.TC_MASKS,
        "AO": unreal.TextureCompressionSettings.TC_MASKS,
        "FogMask": unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
    }[role]
    is_fog = role == "FogMask"
    settings = {
        "compression_settings": compression, "srgb": role == "Color",
        "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP if role == "Normal" else unreal.TextureGroup.TEXTUREGROUP_WORLD,
        "max_texture_size": 4 if is_fog else 2048,
        "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS if is_fog else unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP,
        "never_stream": is_fog, "virtual_texture_streaming": False,
        "address_x": unreal.TextureAddress.TA_CLAMP if is_fog else unreal.TextureAddress.TA_WRAP,
        "address_y": unreal.TextureAddress.TA_CLAMP if is_fog else unreal.TextureAddress.TA_WRAP,
        "filter": unreal.TextureFilter.TF_BILINEAR if is_fog else unreal.TextureFilter.TF_DEFAULT,
    }
    if role == "Normal":
        settings["flip_green_channel"] = False  # Untouched upstream DirectX normal.
    for key, value in settings.items():
        set_checked(asset, key, value)
    tag(asset, "VisualVersion", VERSION)
    tag(asset, "TextureRole", role)
    tag(asset, "SourcePath", str(source.relative_to(ROOT)))
    if not is_fog:
        tag(asset, "SourceLicense", "CC0-1.0")
        tag(asset, "SourceURL", "https://polyhaven.com/a/gravel_floor_04")
        tag(asset, "TileWidthCm", TILE_CM)
    save(asset)
    dimensions = [asset.blueprint_get_size_x(), asset.blueprint_get_size_y()]
    require(dimensions == ([4, 4] if is_fog else [2048, 2048]), name + " has unexpected imported resolution " + repr(dimensions))
    report.append({"path": asset.get_path_name(), "source": str(source.relative_to(ROOT)),
                   "sha256": digest, "role": role, "dimensions": dimensions,
                   "srgb": bool(asset.get_editor_property("srgb")), "compression": str(compression),
                   "settings": {key: str(asset.get_editor_property(key)) for key in settings}})
    return asset


class Graph:
    def __init__(self, name, *, fog=False):
        path = MATERIALS + "/" + name
        self.material = unreal.load_asset(path)
        if self.material is None:
            self.material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
            require(isinstance(self.material, unreal.Material), "could not create " + path)
            tag(self.material, "VisualOwner", OWNER)
        require(isinstance(self.material, unreal.Material) and metadata(self.material, "VisualOwner") == OWNER,
                "refusing to replace an unrelated material at " + path)
        # UE 5.8 DeleteAllMaterialExpressions iterates GetExpressions() while
        # DeleteMaterialExpression removes entries from that same array. That
        # skips shifted nodes. Iterate a detached Python snapshot instead; the
        # per-node engine method still breaks links and removes parameters.
        previous_nodes = list(LIB.get_material_expressions(self.material))
        previous_details = self.node_details(previous_nodes)
        for previous_node in previous_nodes:
            LIB.delete_material_expression(self.material, previous_node)
        remaining = list(LIB.get_material_expressions(self.material))
        require(not remaining, path + " graph clear failed: " + json.dumps({
            "before_count": len(previous_nodes), "remaining_count": len(remaining),
            "before": previous_details, "remaining": self.node_details(remaining)}, sort_keys=True))
        self.replacement = {"method": "snapshot of expressions, delete each through engine API",
                            "before_count": len(previous_nodes), "remaining_after_delete": len(remaining)}
        unreal.log("CINDERLINE_MATERIAL_GRAPH_REPLACED " + json.dumps({"path": path, **self.replacement}, sort_keys=True))
        set_checked(self.material, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if fog else unreal.BlendMode.BLEND_OPAQUE)
        set_checked(self.material, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT if fog else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        set_checked(self.material, "two_sided", fog)
        set_checked(self.material, "disable_depth_test", False)
        if fog:
            set_checked(self.material, "cast_dynamic_shadow_as_masked", False)
            set_checked(self.material, "cast_ray_traced_shadows", False)
        LIB.set_base_material_usage(self.material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
        self.nodes, self.edges, self.outputs = {}, [], []

    @staticmethod
    def node_details(nodes):
        return [{"path": node.get_path_name(), "label": str(node.get_editor_property("desc")),
                 "class": node.get_class().get_name()} if node is not None else {"path": None}
                for node in nodes]

    def validate_nodes(self, phase):
        actual_nodes = list(LIB.get_material_expressions(self.material))
        expected_nodes = list(self.nodes.values())
        actual_paths = [node.get_path_name() if node is not None else None for node in actual_nodes]
        expected_paths = [node.get_path_name() for node in expected_nodes]
        require(len(actual_nodes) == len(expected_nodes) and set(actual_paths) == set(expected_paths),
                self.material.get_path_name() + " unexpected material nodes " + json.dumps({
                    "phase": phase, "expected_count": len(expected_nodes), "actual_count": len(actual_nodes),
                    "expected": self.node_details(expected_nodes), "actual": self.node_details(actual_nodes),
                    "replacement": self.replacement}, sort_keys=True))
        return actual_nodes

    def node(self, label, kind, **properties):
        require(label not in self.nodes, "duplicate graph label " + label)
        node = LIB.create_material_expression(self.material, getattr(unreal, "MaterialExpression" + kind),
                                              -1000 + (len(self.nodes) % 5) * 220, (len(self.nodes) // 5) * 180)
        require(node is not None, "could not create " + kind)
        node.set_editor_property("desc", label)
        for key, value in properties.items():
            node.set_editor_property(key, value)
        self.nodes[label] = node
        return node

    def scalar(self, label, value):
        return self.node(label, "ScalarParameter", parameter_name=label, default_value=value)

    def color(self, label, rgb):
        return self.node(label, "VectorParameter", parameter_name=label, default_value=unreal.LinearColor(*rgb, 1))

    def link(self, source, target, pin, output=""):
        # UE 5.8 MaterialGraphNode::GetShortenPinName maps the generic Input
        # pin to NAME_None; MaterialEditingLibrary uses that shortened name.
        pin = "" if pin == "Input" else pin
        require(LIB.connect_material_expressions(source, output, target, pin), "cannot connect " + pin)
        self.edges.append((source, output, target, pin))

    def output(self, node, property_name, output=""):
        prop = getattr(unreal.MaterialProperty, "MP_" + property_name)
        require(LIB.connect_material_property(node, output, prop), "cannot connect material " + property_name)
        self.outputs.append((node, output, property_name, prop))

    def world_uv(self, tile_cm, prefix=""):
        world = self.node(prefix + "Absolute world position", "WorldPosition")
        xy = self.node(prefix + "World XY", "ComponentMask", r=True, g=True, b=False, a=False)
        self.link(world, xy, "Input")
        scale = self.node(prefix + "World centimeters to texture UV", "Multiply", const_b=1.0 / tile_cm)
        self.link(xy, scale, "A")
        return scale

    def uv0(self, tiling):
        return self.node("Authored UV0", "TextureCoordinate", coordinate_index=0,
                         u_tiling=tiling, v_tiling=tiling)

    def sample(self, label, asset, uv, sampler):
        node = self.node(label, "TextureSampleParameter2D", parameter_name=label, texture=asset,
                         sampler_type=getattr(unreal.MaterialSamplerType, "SAMPLERTYPE_" + sampler))
        self.link(uv, node, "UVs")
        return node

    def normal(self, sampled, strength):
        flat = self.node("Flat tangent normal", "Constant3Vector", constant=unreal.LinearColor(0, 0, 1, 0))
        mix = self.node("Restrained normal detail", "LinearInterpolate")
        self.link(flat, mix, "A")
        self.link(sampled, mix, "B", "RGB")
        self.link(self.scalar("NormalStrength", strength), mix, "Alpha")
        normalized = self.node("Normalized tangent normal", "Normalize")
        self.link(mix, normalized, "VectorInput")
        self.output(normalized, "NORMAL")

    def finish(self):
        self.validate_nodes("before shader compilation")
        errors = list(LIB.recompile_material(self.material))
        require(not errors, self.material.get_path_name() + " shader errors: " + repr(errors))
        actual_nodes = self.validate_nodes("after shader compilation")
        edge_report = []
        for source, channel, target, pin in self.edges:
            names = [str(name) for name in LIB.get_material_expression_input_names(target)]
            inputs = list(LIB.get_inputs_for_material_expression(self.material, target))
            index = 0 if not pin else (names.index(pin) if pin in names else -1)
            require(0 <= index < len(inputs) and inputs[index] == source,
                    "material connection readback failed for " + target.get_editor_property("desc") + "." + pin)
            edge_report.append({"from": source.get_editor_property("desc"), "output": channel,
                                "to": target.get_editor_property("desc"), "input": pin})
        output_report = {}
        for node, channel, name, prop in self.outputs:
            require(LIB.get_material_property_input_node(self.material, prop) == node, "material output readback failed for " + name)
            actual_channel = str(LIB.get_material_property_input_node_output_name(self.material, prop))
            require(not channel or actual_channel == channel, "material output channel mismatch for " + name)
            output_report[name] = {"node": node.get_editor_property("desc"), "output": actual_channel}
        tag(self.material, "VisualVersion", VERSION)
        tag(self.material, "GeneratorSHA256", sha256(__file__))
        save(self.material)
        return {"path": self.material.get_path_name(), "version": VERSION, "nodes": len(actual_nodes),
                "graph_replacement": self.replacement,
                "blend_mode": str(self.material.get_editor_property("blend_mode")),
                "shading_model": str(self.material.get_editor_property("shading_model")),
                "two_sided": bool(self.material.get_editor_property("two_sided")),
                "depth_test_enabled": not self.material.get_editor_property("disable_depth_test"),
                "connections": edge_report, "outputs": output_report, "shader_errors": errors}


def ground_material(textures, *, rock=False):
    graph = Graph("M_CinderBasaltV2" if rock else "M_CinderGroundV2")
    # Cliff normal maps must follow the mesh's authored tangent UVs on every
    # face. The stationary horizontal battlefield retains metric world mapping.
    uv = graph.uv0(2.0) if rock else graph.world_uv(TILE_CM)
    color = graph.sample("GroundColor", textures["Color"], uv, "COLOR")
    desaturate = graph.node("Neutral basalt grain", "Desaturation")
    graph.link(color, desaturate, "Input", "RGB")
    graph.link(graph.scalar("Desaturation", 0.9 if rock else 0.94), desaturate, "Fraction")
    tint = graph.color("GroundTint", (0.34, 0.43, 0.47) if rock else (0.50, 0.62, 0.72))
    tinted = graph.node("Ground color and tint", "Multiply")
    graph.link(desaturate, tinted, "A")
    graph.link(tint, tinted, "B", "RGB")
    macro_report = None
    if rock:
        graph.output(tinted, "BASE_COLOR")
    else:
        # Reuse original artwork only as bounded broad albedo variation. The
        # measured PBR normal/roughness/AO maps remain the surface response.
        macro_texture = unreal.load_asset(MACRO_BASALT_PATH)
        require(isinstance(macro_texture, unreal.Texture2D), "bootstrap original basalt texture before V2.2")
        require(macro_texture.get_editor_property("srgb"), "existing basalt must retain its color sampler contract")
        macro = graph.sample("BasaltMacroColor", macro_texture,
                             graph.world_uv(MACRO_TILE_CM, "Macro "), "COLOR")
        neutral = graph.node("Macro rock luminance", "Desaturation")
        graph.link(macro, neutral, "Input", "RGB")
        graph.link(graph.node("Macro desaturation", "Constant", r=1.0), neutral, "Fraction")
        gain = graph.node("Macro luminance scale", "Multiply", const_b=5.0)
        graph.link(neutral, gain, "A")
        bias = graph.node("Macro luminance bias", "Add", const_b=0.72)
        graph.link(gain, bias, "A")
        modulation = graph.node("Restrained macro albedo modulation", "Clamp", min_default=0.84, max_default=1.14)
        graph.link(bias, modulation, "Input")
        albedo = graph.node("Slate ground with broad rock variation", "Multiply")
        graph.link(tinted, albedo, "A")
        graph.link(modulation, albedo, "B")
        graph.output(albedo, "BASE_COLOR")
        macro_report = {"path": macro_texture.get_path_name(), "tile_width_cm": MACRO_TILE_CM,
                        "usage": "desaturated albedo modulation only", "modulation_bounds": [0.84, 1.14],
                        "luminance_gain": 5.0, "luminance_bias": 0.72,
                        "asset_modified": False, "original_source": "RawAssets/Textures/T_CinderBasalt.png",
                        "original_source_sha256": sha256(ROOT / "RawAssets/Textures/T_CinderBasalt.png")}
    graph.normal(graph.sample("GroundNormal", textures["Normal"], uv, "NORMAL"), 0.45 if rock else 0.85)
    roughness = graph.sample("GroundRoughness", textures["Roughness"], uv, "MASKS")
    graph.output(roughness, "ROUGHNESS", "R")
    graph.output(graph.sample("GroundAO", textures["AO"], uv, "MASKS"), "AMBIENT_OCCLUSION", "R")
    graph.output(graph.node("Nonmetal mineral", "Constant", r=0.0), "METALLIC")
    validation = graph.finish()
    validation["texture_coordinates"] = {"mode": "authored UV0", "channel": 0, "tiling": [2.0, 2.0]} if rock else {
        "mode": "absolute world XY", "fine_tile_width_cm": TILE_CM, "macro_tile_width_cm": MACRO_TILE_CM}
    validation["albedo_treatment"] = {"desaturation": 0.9 if rock else 0.94,
                                      "tint": [0.34, 0.43, 0.47] if rock else [0.50, 0.62, 0.72]}
    if macro_report:
        validation["macro_albedo"] = macro_report
    return graph.material, validation


def model_material(textures):
    graph = Graph("M_CinderModelV2")
    graph.output(graph.color("Tint", (0.07, 0.10, 0.12)), "BASE_COLOR", "RGB")
    # Authored UVs move and rotate with each mesh; world-space microdetail swam
    # across moving units and disagreed with their imported tangent frames.
    uv = graph.uv0(4.0)
    graph.normal(graph.sample("MicroNormal", textures["Normal"], uv, "NORMAL"), 0.10)
    micro = graph.sample("MicroRoughness", textures["Roughness"], uv, "MASKS")
    centered = graph.node("Centered micro roughness", "Add", const_b=-0.5)
    graph.link(micro, centered, "A", "R")
    varied = graph.node("Small roughness variation", "Multiply")
    graph.link(centered, varied, "A")
    graph.link(graph.scalar("RoughnessVariation", 0.10), varied, "B")
    roughness = graph.node("Paint roughness with micro detail", "Add")
    graph.link(varied, roughness, "A")
    graph.link(graph.scalar("Roughness", 0.48), roughness, "B")
    bounded = graph.node("Physical roughness bounds", "Clamp", min_default=0.15, max_default=0.95)
    graph.link(roughness, bounded, "Input")
    graph.output(bounded, "ROUGHNESS")
    graph.output(graph.scalar("Metallic", 0.20), "METALLIC")
    glow = graph.node("Team core glow", "Multiply")
    graph.link(graph.nodes["Tint"], glow, "A", "RGB")
    graph.link(graph.scalar("GlowIntensity", 0.0), glow, "B")
    graph.output(glow, "EMISSIVE_COLOR")
    validation = graph.finish()
    validation["texture_coordinates"] = {"mode": "authored UV0", "channel": 0, "tiling": [4.0, 4.0],
                                          "normal_and_roughness_share_uvs": True}
    require({"Tint"}.issubset({str(x) for x in LIB.get_vector_parameter_names(graph.material)}), "V2 model lacks Tint")
    require({"Roughness", "Metallic", "GlowIntensity"}.issubset({str(x) for x in LIB.get_scalar_parameter_names(graph.material)}), "V2 model lacks runtime-compatible parameters")
    palettes = (
        ("HullDark", (0.055, 0.075, 0.09), 0.50, 0.20, 0.0),
        ("HullLight", (0.17, 0.22, 0.24), 0.46, 0.20, 0.0),
        ("Metal", (0.12, 0.16, 0.18), 0.34, 0.80, 0.0),
        ("TeamPanel", (0.04, 0.70, 0.59), 0.45, 0.18, 0.04),
        ("CoreGlow", (0.50, 1.0, 0.88), 0.40, 0.0, 1.20),
    )
    instances = []
    for slot, rgb, roughness, metallic, glow in palettes:
        path = MATERIALS + "/MI_Cinder" + slot
        instance = unreal.load_asset(path)
        require(isinstance(instance, unreal.MaterialInstanceConstant), "import original model pack first; missing " + path)
        previous_parent = instance.get_editor_property("parent")
        require(previous_parent and previous_parent.get_name() in {"M_CinderModel", "M_CinderModelV2"}, "unexpected model instance parent at " + path)
        LIB.set_material_instance_parent(instance, graph.material)
        require(instance.get_editor_property("parent") == graph.material, "V2 parent did not persist")
        LIB.set_material_instance_vector_parameter_value(instance, "Tint", unreal.LinearColor(*rgb, 1))
        actual = LIB.get_material_instance_vector_parameter_value(instance, "Tint")
        require(all(math.isclose(a, b, abs_tol=1e-5) for a, b in zip((actual.r, actual.g, actual.b), rgb)), "Tint readback failed")
        values = {"Roughness": roughness, "Metallic": metallic, "GlowIntensity": glow}
        for parameter, value in values.items():
            LIB.set_material_instance_scalar_parameter_value(instance, parameter, value)
            require(math.isclose(LIB.get_material_instance_scalar_parameter_value(instance, parameter), value, abs_tol=1e-5), "instance parameter readback failed: " + parameter)
        tag(instance, "VisualVersion", VERSION)
        save(instance)
        instances.append({"path": instance.get_path_name(), "slot": slot, "parent": graph.material.get_path_name(), "tint": list(rgb), **values})
    return validation, instances


def fog_material(mask):
    graph = Graph("M_CinderFogV2", fog=True)
    sample = graph.sample("FogMask", mask, graph.world_uv(4800.0), "LINEAR_COLOR")
    graph.output(sample, "OPACITY", "R")
    color = graph.node("Unknown and explored fog colors", "LinearInterpolate")
    graph.link(graph.color("FogColor", (0.010, 0.017, 0.027)), color, "A", "RGB")
    graph.link(graph.color("FogExploredColor", (0.027, 0.041, 0.049)), color, "B", "RGB")
    graph.link(sample, color, "Alpha", "G")
    graph.output(color, "EMISSIVE_COLOR")
    result = graph.finish()
    result["runtime_contract"] = {"FogMask": "256x256 PF_B8G8R8A8, sRGB=false, clamp, bilinear, no mips",
                                  "R": "opacity", "G": "explored factor", "uv": "absolute world XY / 4800",
                                  "strict_fog": "CPU mask keeps hidden cells opaque; feather only inside visible cells",
                                  "component_cast_shadow": "must be false in runtime adapter"}
    return result


def default_fog_png():
    path = ROOT / "Intermediate/VisualUpgrade/T_CinderFogDefault.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    # R=opaque, G=unexplored. Safe if a runtime mask has not been attached yet.
    raw = (b"\x00" + bytes((255, 0, 0, 255)) * 4) * 4
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    if not path.exists() or path.read_bytes() != png:
        path.write_bytes(png)
    return path


def terrain_meshes(material, reimport):
    manifest_path = ROOT / "RawAssets/Terrain/manifest.json"
    manifest = json.loads(manifest_path.read_text())
    require(manifest.get("unit") == "centimeter" and manifest.get("origin") == "bottom center of bounds", "invalid terrain units or origin")
    require(manifest.get("forward_axis") == "+X" and manifest.get("up_axis") == "+Z", "invalid terrain axes")
    require(manifest.get("material_slots") == ["Basalt"], "terrain must keep one Basalt slot")
    records = manifest.get("assets", [])
    names = [row.get("name") for row in records]
    require(len(names) == 4 and set(names) == {"SM_BasaltCliff_" + c for c in "ABCD"}, "terrain manifest must contain exactly four expected cliffs")
    spec = importlib.util.spec_from_file_location("cinderline_model_import_options", ROOT / "scripts/unreal_model_assets.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    results = []
    for row in records:
        name = row["name"]
        source = (ROOT / row["fbx"]).resolve()
        require(source.is_relative_to(ROOT) and source.suffix.lower() == ".fbx" and source.is_file(), "invalid terrain source " + name)
        digest = sha256(source)
        require(digest == row["sha256"], "terrain source SHA256 differs from manifest: " + name)
        path = ENVIRONMENT + "/" + name
        mesh = unreal.load_asset(path)
        if mesh is not None:
            require(isinstance(mesh, unreal.StaticMesh) and metadata(mesh, "VisualOwner") == OWNER, "unrelated asset at " + path)
            require(reimport or metadata(mesh, "SourceSHA256") == digest, name + " changed; set CINDER_REIMPORT_VISUALS=1")
        if mesh is None or reimport:
            task = unreal.AssetImportTask()
            for key, value in {"filename": str(source), "destination_path": ENVIRONMENT, "destination_name": name,
                               "automated": True, "async_": False, "save": False, "replace_existing": mesh is not None,
                               "replace_existing_settings": mesh is not None, "factory": unreal.FbxFactory(), "options": helper._fbx_options()}.items():
                task.set_editor_property(key, value)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            objects = list(task.get_objects())
            imported = [asset for asset in objects if isinstance(asset, unreal.StaticMesh)]
            require(len(imported) == 1 and imported[0].get_path_name() == path + "." + name, "FBX must import exactly one expected mesh: " + name)
            mesh = imported[0]
            tag(mesh, "VisualOwner", OWNER)
            tag(mesh, "SourceSHA256", digest)
        slots = [str(slot.get_editor_property("material_slot_name")) for slot in mesh.get_editor_property("static_materials")]
        require(slots == ["Basalt"] and mesh.get_num_sections(0) == 1, name + " did not keep one populated Basalt slot")
        bounds = mesh.get_bounds()
        origin, extent = bounds.origin, bounds.box_extent
        dimensions = [extent.x * 2, extent.y * 2, extent.z * 2]
        require(all(abs(a - b) <= 0.5 for a, b in zip(dimensions, row["dimensions_cm"])), name + " FBX scale differs from manifest")
        require(all(abs(a - b) <= 0.5 for a, b in zip(dimensions, (100, 100, 100))), name + " must fit normalized 100 cm bounds")
        require(abs(origin.x) <= 0.5 and abs(origin.y) <= 0.5 and abs(origin.z - extent.z) <= 0.5, name + " lost its bottom-center pivot")
        require(mesh.get_num_triangles(0) == row["triangles"], name + " triangle count changed during import")
        require(not mesh.get_editor_property("nanite_settings").get_editor_property("enabled"), name + " unexpectedly enabled Nanite")
        mesh.set_material(0, material)
        require(mesh.get_material(0) == material, name + " material assignment failed")
        tag(mesh, "VisualVersion", VERSION)
        tag(mesh, "TerrainContract", "cm,+X,+Z,bottom-center,100-cube,one-Basalt-slot")
        save(mesh)
        results.append({"path": mesh.get_path_name(), "source": str(source.relative_to(ROOT)), "sha256": digest,
                        "dimensions_cm": dimensions, "origin_cm": [origin.x, origin.y, origin.z],
                        "triangles": mesh.get_num_triangles(0), "slots": slots, "material": material.get_path_name()})
    return results


def upgrade_visual_assets():
    report_path = ROOT / "artifacts/visual-upgrade-import-results.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report = {"success": False, "version": VERSION, "utc": datetime.now(timezone.utc).isoformat(),
              "generator_sha256": sha256(__file__), "textures": [], "materials": [], "instances": [], "terrain": [],
              "texture_source": {"asset": "Gravel Floor 04", "url": "https://polyhaven.com/a/gravel_floor_04",
                                 "license": "CC0-1.0", "license_url": "https://polyhaven.com/license", "tile_width_cm": TILE_CM,
                                 "normal_convention": "DirectX, green channel unchanged"}}
    try:
        source_manifest = GROUND_SOURCE / "manifest.json"
        source_metadata = json.loads(source_manifest.read_text())
        require(source_metadata.get("license") == "CC0-1.0" and source_metadata.get("asset_id") == "gravel_floor_04",
                "ground source manifest has an unexpected asset or license")
        require(source_metadata.get("tile_dimensions_cm") == [TILE_CM, TILE_CM], "ground source tile scale changed")
        source_rows = {row["filename"]: row for row in source_metadata.get("files", [])}
        require(set(source_rows) == set(GROUND_FILES.values()), "ground source manifest must identify exactly four maps")
        for filename in GROUND_FILES.values():
            row = source_rows[filename]
            require(row.get("width") == 2048 and row.get("height") == 2048, "source map resolution changed: " + filename)
            require(sha256(GROUND_SOURCE / filename) == row["sha256"], "source texture SHA256 differs from provenance: " + filename)
        report["source_manifest"] = {"path": str(source_manifest.relative_to(ROOT)), "sha256": sha256(source_manifest),
                                     "authors": source_metadata.get("authors"), "files": source_metadata["files"]}
        for directory in (MATERIALS, TEXTURES, ENVIRONMENT):
            unreal.EditorAssetLibrary.make_directory(directory)
        reimport = os.environ.get("CINDER_REIMPORT_VISUALS") == "1"
        textures = {role: texture(GROUND_SOURCE / filename, "T_CinderGroundV2_" + role, role, report["textures"], reimport)
                    for role, filename in GROUND_FILES.items()}
        mask = texture(default_fog_png(), "T_CinderFogDefaultV2", "FogMask", report["textures"], reimport)
        _, ground_validation = ground_material(textures)
        basalt, basalt_validation = ground_material(textures, rock=True)
        model_validation, report["instances"] = model_material(textures)
        report["materials"] = [ground_validation, basalt_validation, model_validation, fog_material(mask)]
        report["terrain"] = terrain_meshes(basalt, reimport)
        report["success"] = True
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    unreal.log("CINDERLINE_VISUAL_UPGRADE_OK: 4 PBR maps, 4 versioned materials, 5 model slots, 4 validated cliffs; " + str(report_path))
    return report


if __name__ == "__main__":
    upgrade_visual_assets()
