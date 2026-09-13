"""Build the cached terrain-layer material and import the current four cliff FBXs.

Run explicitly inside UnrealEditor-Cmd. Uses existing licensed PBR textures without
editing them. The four-pixel mask is neutral data, replaced by runtime linear BGRA.
"""
import importlib.util
import json
import os
import struct
import traceback
import zlib
from datetime import datetime, timezone
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parent.parent
OWNER = "scripts/unreal_terrain_surface.py"


def build_terrain_surface():
    spec = importlib.util.spec_from_file_location("cinder_surface_graph", ROOT / "scripts/unreal_visual_upgrade.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    report_path = ROOT / "artifacts/animation-terrain/terrain-import.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report = {"success": False, "utc": datetime.now(timezone.utc).isoformat(), "owner": OWNER,
              "source_sha256": helper.sha256(__file__), "terrain": []}
    try:
        # Keep the existing terrain ownership contract when replacing those four meshes.
        basalt = unreal.load_asset("/Game/Art/Materials/M_CinderBasaltV2")
        helper.require(isinstance(basalt, unreal.MaterialInterface), "baseline basalt material missing")
        report["terrain"] = helper.terrain_meshes(basalt, os.environ.get("CINDER_REIMPORT_VISUALS") == "1")
        helper.OWNER = OWNER
        helper.VERSION = "layered-basalt-v3.0"
        path = ROOT / "Intermediate/VisualUpgrade/T_CinderTerrainLayersDefault.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
        raw = (b"\x00" + bytes((0, 0, 0, 255)) * 4) * 4
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
        if not path.exists() or path.read_bytes() != png:
            path.write_bytes(png)
        report["textures"] = []
        mask = helper.texture(path, "T_CinderTerrainLayersDefault", "FogMask", report["textures"], False)
        helper.tag(mask, "TextureRole", "TerrainLayers: R stone, G mineral, B ash; linear cached data")
        helper.save(mask)
        textures = {}
        for role in ("Color", "Normal", "Roughness", "AO"):
            textures[role] = unreal.load_asset("/Game/Art/Textures/VisualUpgrade/T_CinderGroundV2_" + role)
            helper.require(isinstance(textures[role], unreal.Texture2D), "missing existing " + role + " texture")

        graph = helper.Graph("M_CinderGroundV3")
        uv = graph.world_uv(helper.TILE_CM)
        grain = graph.sample("GroundColor", textures["Color"], uv, "COLOR")
        neutral = graph.node("Desaturated mineral grain", "Desaturation")
        graph.link(grain, neutral, "Input", "RGB")
        graph.link(graph.scalar("Desaturation", 0.88), neutral, "Fraction")
        base = graph.node("Cool basalt base", "Multiply")
        graph.link(neutral, base, "A")
        graph.link(graph.color("BasaltTint", (0.34, 0.43, 0.48)), base, "B", "RGB")

        macro_texture = unreal.load_asset(helper.MACRO_BASALT_PATH)
        helper.require(isinstance(macro_texture, unreal.Texture2D), "original basalt macro texture missing")
        macro = graph.sample("BasaltMacroColor", macro_texture, graph.world_uv(1370.0, "Macro "), "COLOR")
        luminance = graph.node("Broad fractures luminance", "Desaturation")
        graph.link(macro, luminance, "Input", "RGB")
        graph.link(graph.node("Neutral macro color", "Constant", r=1.0), luminance, "Fraction")
        gain = graph.node("Fracture variation gain", "Multiply", const_b=7.0)
        graph.link(luminance, gain, "A")
        bias = graph.node("Fracture variation bias", "Add", const_b=0.56)
        graph.link(gain, bias, "A")
        variation = graph.node("Bounded broad rock value", "Clamp", min_default=0.64, max_default=1.22)
        graph.link(bias, variation, "Input")
        bedrock = graph.node("Varied basalt bed", "Multiply")
        graph.link(base, bedrock, "A")
        graph.link(variation, bedrock, "B")

        layers = graph.sample("TerrainLayers", mask, graph.world_uv(prefix="Layers "), "LINEAR_COLOR")
        def tint_layer(label, current, tint, channel, strength):
            tinted = graph.node(label + " grain", "Multiply")
            graph.link(neutral, tinted, "A")
            graph.link(graph.color(label + " tint", tint), tinted, "B", "RGB")
            alpha = graph.node(label + " blend weight", "Multiply", const_b=strength)
            graph.link(layers, alpha, "A", channel)
            result = graph.node(label + " transition", "LinearInterpolate")
            graph.link(current, result, "A")
            graph.link(tinted, result, "B")
            graph.link(alpha, result, "Alpha")
            return result
        color = tint_layer("Windblown ash", bedrock, (0.62, 0.56, 0.43), "B", 0.80)
        color = tint_layer("Exposed cliff stone", color, (0.17, 0.22, 0.25), "R", 0.68)
        color = tint_layer("Iron mineral staining", color, (0.64, 0.30, 0.12), "G", 0.76)
        graph.output(color, "BASE_COLOR")
        graph.normal(graph.sample("GroundNormal", textures["Normal"], uv, "NORMAL"), 0.78)
        rough = graph.sample("GroundRoughness", textures["Roughness"], uv, "MASKS")
        dusty_rough = graph.node("Ash roughness", "LinearInterpolate", const_b=0.96)
        graph.link(rough, dusty_rough, "A", "R")
        graph.link(layers, dusty_rough, "Alpha", "B")
        graph.output(dusty_rough, "ROUGHNESS")
        graph.output(graph.sample("GroundAO", textures["AO"], uv, "MASKS"), "AMBIENT_OCCLUSION", "R")
        graph.output(graph.node("Nonmetal stone", "Constant", r=0.0), "METALLIC")
        report["material"] = graph.finish()
        report["material"]["texture_samples"] = 6
        report["runtime_mask"] = {"size": [256, 256], "srgb": False,
                                  "channels": {"R": "explored cliff stone", "G": "observed mineral positions", "B": "map-seeded ash"},
                                  "update": "on reset or newly observed features only",
                                  "world_size_parameter": "CinderWorldSizeInverse", "collision_modified": False}
        report["success"] = True
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    unreal.log("CINDERLINE_TERRAIN_SURFACE_OK: layered ground and four validated cliffs; " + str(report_path))
    return report


if __name__ == "__main__":
    build_terrain_surface()
