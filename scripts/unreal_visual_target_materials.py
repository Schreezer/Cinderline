"""Create the Visual Target material set inside Unreal Editor.

Run explicitly with UnrealEditor-Cmd, or import this file and call
``build_visual_target_materials()`` from another editor script. Importing the
module has no side effects. The generator only creates or replaces assets below
``/Game/Art/VisualTarget/Materials`` and writes its JSON report below
``artifacts/visual-target/materials``.

The graphs target the mobile renderer: model finish variation uses authored UV0
and bounded arithmetic, while terrain uses one planar world-XY sample per map.
Only the small scenery-rock material uses three-axis projection, capped at twelve
PBR samples so nonuniform instance scale does not stretch its surface. There is
no animated noise or per-unit texture dependency.
"""
import importlib.util
import hashlib
import json
import math
import os
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parent.parent
OWNER = "scripts/unreal_visual_target_materials.py"
VERSION = "cinematic-visual-target-materials-v1.6.1"
DESTINATION = "/Game/Art/VisualTarget/Materials"
REPORT_PATH = ROOT / "artifacts/visual-target/materials/material-import.json"
LIB = unreal.MaterialEditingLibrary
GROUND_ALBEDO_TILE_CM = 740.0
COARSE_GROUND_TILE_CM = 400.0
COARSE_GROUND_SOURCE = ROOT / "RawAssets/VisualTarget/Materials/dry_ground_rocks"
COARSE_GROUND_FILES = {
    "Color": ("dry_ground_rocks_diff_2k.jpg", "3106a1157ea4c6823b78494988ac0269"),
    "Normal": ("dry_ground_rocks_nor_dx_2k.jpg", "8907b86aed5000d0a40e41f49580ad53"),
    "Roughness": ("dry_ground_rocks_rough_2k.jpg", "5a461a436095cbe54cbd1de156bdc164"),
    "AO": ("dry_ground_rocks_ao_2k.jpg", "b1151db9b276cbf92e9d05185eebaeb7"),
}

# The unlit core surface is the one emitter allowed to exceed the bloom
# threshold, so its intensity is named once and shared by the palette row and
# _core_instance rather than repeated as a literal in two places.
CORE_TINT = (0.18, 0.46, 0.40)
CORE_GLOW_INTENSITY = 2.60
BLOOM_THRESHOLD = 0.62

# name, slot, tint, roughness, metallic, glow, finish variation, surface wear,
# AO, rim light, rim colour.
#
# Rim light is the largest single readability change in the model material.
# M_CinderModelV3 has always built the Fresnel rim and paid its ALU on every unit
# pixel, but no instance ever wrote RimLight, so units shipped with the 0.09
# graph default and read as near-black blobs with no edge separation from the
# warm ground. Darker hulls get more rim because they have further to travel to
# reach a readable edge; the rim colour stays cool blue-white against warm
# terrain so the separation is hue as well as value.
#
# The glow ordering below is deliberate and load-bearing. The thermal ladder
# zeroes bloom at Minimum quality, so nothing may depend on bloom to be legible:
#   TeamPanel  (0.025, 0.44, 0.37) * 0.55 peaks at 0.242, under the 0.62 bloom
#              threshold. A panel therefore reads as solid team colour with bloom
#              off and gains nothing it needs when bloom is on. At runtime the
#              C++ overrides Tint with the team colour, whose brightest channel is
#              1.00, so the worst case is still 0.55 and remains under threshold.
#   CoreGlow   (0.18, 0.46, 0.40) * 2.60 peaks at 1.196, well over the threshold,
#              so cores are the one thing on a unit that blooms: a garnish on an
#              already-legible shape, never the thing that makes it legible.
MODEL_PALETTES = (
    ("MI_VT_HullDark", "HullDark", (0.045, 0.065, 0.075), 0.43, 0.30, 0.0, 0.050, 0.09, 1.0,
     0.55, (0.62, 0.78, 1.00)),
    ("MI_VT_HullLight", "HullLight", (0.42, 0.50, 0.52), 0.40, 0.22, 0.0, 0.040, 0.05, 1.0,
     0.40, (0.66, 0.80, 1.00)),
    ("MI_VT_Metal", "Metal", (0.070, 0.085, 0.095), 0.25, 0.92, 0.0, 0.025, 0.08, 0.94,
     0.35, (0.80, 0.86, 1.00)),
    ("MI_VT_TeamPanel", "TeamPanel", (0.025, 0.44, 0.37), 0.38, 0.22, 0.55, 0.035, 0.05, 1.0,
     0.25, (0.66, 0.80, 1.00)),
    # Unused by _model_material: the core slot is parented to the unlit
    # M_VT_CoreSurface by _core_instance. Kept truthful so the palette table
    # still documents every shipped slot.
    ("MI_VT_CoreGlow", "CoreGlow", CORE_TINT, 0.32, 0.0, CORE_GLOW_INTENSITY, 0.015, 0.02, 1.0,
     0.0, (0.62, 0.78, 1.00)),
    ("MI_CinderSceneryMetal", "SceneryMetal", (0.065, 0.078, 0.085), 0.30, 0.88, 0.0, 0.035, 0.11, 0.92,
     0.18, (0.80, 0.86, 1.00)),
    ("MI_CinderSceneryPaint", "SceneryPaint", (0.235, 0.135, 0.055), 0.46, 0.28, 0.0, 0.050, 0.14, 0.95,
     0.18, (0.66, 0.80, 1.00)),
)


def _load_graph_helper():
    path = ROOT / "scripts/unreal_visual_upgrade.py"
    spec = importlib.util.spec_from_file_location("cinder_visual_target_graph", path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.OWNER = OWNER
    helper.VERSION = VERSION
    helper.MATERIALS = DESTINATION
    return helper


def _instance(helper, parent, name, slot, values):
    path = DESTINATION + "/" + name
    instance = unreal.load_asset(path)
    if instance is None:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DESTINATION, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        helper.require(isinstance(instance, unreal.MaterialInstanceConstant),
                       "could not create " + path)
        helper.tag(instance, "VisualOwner", OWNER)
    helper.require(isinstance(instance, unreal.MaterialInstanceConstant)
                   and helper.metadata(instance, "VisualOwner") == OWNER,
                   "refusing to replace an unrelated material instance at " + path)
    LIB.set_material_instance_parent(instance, parent)
    helper.require(instance.get_editor_property("parent") == parent,
                   "material instance parent did not persist at " + path)

    readback = {}
    # RimColor is written exactly like Tint. Without it every instance inherits the
    # graph default and the whole army rims the same blue, which throws away the
    # only cue that separates a painted hull from bare metal at phone zoom.
    for parameter in ("Tint", "RimColor"):
        rgb = values[parameter]
        LIB.set_material_instance_vector_parameter_value(
            instance, parameter, unreal.LinearColor(*rgb, 1.0))
        actual_color = LIB.get_material_instance_vector_parameter_value(instance, parameter)
        helper.require(all(math.isclose(actual, expected, abs_tol=1e-5)
                           for actual, expected in zip(
                               (actual_color.r, actual_color.g, actual_color.b, actual_color.a),
                               (*rgb, 1.0))), parameter + " readback failed at " + path)
        readback[parameter] = list(rgb)
    for parameter in ("Roughness", "Metallic", "GlowIntensity",
                      "FinishVariation", "SurfaceWear", "AO", "RimLight"):
        value = values[parameter]
        LIB.set_material_instance_scalar_parameter_value(instance, parameter, value)
        actual = LIB.get_material_instance_scalar_parameter_value(instance, parameter)
        helper.require(math.isclose(actual, value, abs_tol=1e-5),
                       parameter + " readback failed at " + path)
        readback[parameter] = actual
    helper.tag(instance, "VisualVersion", VERSION)
    helper.tag(instance, "VisualPalette", slot)
    helper.save(instance)
    return {"path": instance.get_path_name(), "slot": slot,
            "parent": parent.get_path_name(), "parameters": readback}


def _core_instance(helper, parent):
    name = "MI_VT_CoreGlow"
    path = DESTINATION + "/" + name
    instance = unreal.load_asset(path)
    if instance is None:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DESTINATION, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        helper.require(isinstance(instance, unreal.MaterialInstanceConstant),
                       "could not create " + path)
        helper.tag(instance, "VisualOwner", OWNER)
    helper.require(isinstance(instance, unreal.MaterialInstanceConstant)
                   and helper.metadata(instance, "VisualOwner") == OWNER,
                   "refusing to replace an unrelated material instance at " + path)
    LIB.set_material_instance_parent(instance, parent)
    helper.require(instance.get_editor_property("parent") == parent,
                   "core material instance parent did not persist")
    tint = CORE_TINT
    LIB.set_material_instance_vector_parameter_value(
        instance, "Tint", unreal.LinearColor(*tint, 1.0))
    actual_tint = LIB.get_material_instance_vector_parameter_value(instance, "Tint")
    helper.require(all(math.isclose(actual, expected, abs_tol=1e-5)
                       for actual, expected in zip(
                           (actual_tint.r, actual_tint.g, actual_tint.b, actual_tint.a),
                           (*tint, 1.0))), "core Tint readback failed")
    # 2.60, not 0.80: CORE_TINT peaks at 0.46, so the old value peaked at 0.368 and
    # the core sat below the 0.62 bloom threshold, indistinguishable from the team
    # panel beside it. At 2.60 the peak is 1.196 and the core is the only part of
    # the silhouette that blooms. It is unlit, so this is pure emissive headroom
    # and costs no light, no shadow and no sampler.
    glow = CORE_GLOW_INTENSITY
    LIB.set_material_instance_scalar_parameter_value(instance, "GlowIntensity", glow)
    helper.require(math.isclose(
        LIB.get_material_instance_scalar_parameter_value(instance, "GlowIntensity"),
        glow, abs_tol=1e-5), "core GlowIntensity readback failed")
    helper.tag(instance, "VisualVersion", VERSION)
    helper.tag(instance, "VisualPalette", "CoreGlowUnlit")
    helper.save(instance)
    return {"path": instance.get_path_name(), "slot": "CoreGlow",
            "parent": parent.get_path_name(), "parameters": {
                "Tint": list(tint), "GlowIntensity": glow}}


def _scenery_rock_material(helper):
    textures = {}
    for role in ("Color", "Normal", "Roughness", "AO"):
        path = "/Game/Art/Textures/VisualUpgrade/T_CinderGroundV2_" + role
        textures[role] = unreal.load_asset(path)
        helper.require(isinstance(textures[role], unreal.Texture2D),
                       "missing licensed Poly Haven rock texture " + path)

    graph = helper.Graph("M_VT_SceneryRock")
    world = graph.node("Absolute world position", "WorldPosition")
    projections = {}
    for axis, channels in (("X", (False, True, True)),
                           ("Y", (True, False, True)),
                           ("Z", (True, True, False))):
        projected = graph.node(axis + " facing planar coordinates", "ComponentMask",
                               r=channels[0], g=channels[1], b=channels[2], a=False)
        graph.link(world, projected, "Input")
        uv = graph.node(axis + " projection at 246 cm", "Multiply",
                        const_b=1.0 / helper.TILE_CM)
        graph.link(projected, uv, "A")
        projections[axis] = uv

    vertex_normal = graph.node("World vertex normal", "VertexNormalWS")
    absolute_normal = graph.node("Absolute world normal", "Abs")
    graph.link(vertex_normal, absolute_normal, "Input")
    weights = {}
    for axis, channel in (("X", "R"), ("Y", "G"), ("Z", "B")):
        component = graph.node(axis + " normal component", "ComponentMask",
                               r=axis == "X", g=axis == "Y", b=axis == "Z", a=False)
        graph.link(absolute_normal, component, "Input")
        squared = graph.node(axis + " dominant projection weight", "Multiply")
        graph.link(component, squared, "A")
        graph.link(component, squared, "B")
        weights[axis] = squared

    def sample(role, axis, sampler):
        node = graph.node("Rock " + role + " " + axis + " projection", "TextureSample",
                          texture=textures[role],
                          sampler_type=getattr(unreal.MaterialSamplerType,
                                               "SAMPLERTYPE_" + sampler))
        graph.link(projections[axis], node, "UVs")
        return node

    def blend(role, sampler, output):
        weighted = []
        for axis in ("X", "Y", "Z"):
            value = graph.node("Weighted " + role + " " + axis, "Multiply")
            graph.link(sample(role, axis, sampler), value, "A", output)
            graph.link(weights[axis], value, "B")
            weighted.append(value)
        first = graph.node("Blended " + role + " XY", "Add")
        graph.link(weighted[0], first, "A")
        graph.link(weighted[1], first, "B")
        result = graph.node("Blended " + role + " XYZ", "Add")
        graph.link(first, result, "A")
        graph.link(weighted[2], result, "B")
        return result

    color = blend("Color", "COLOR", "RGB")
    neutral = graph.node("Desaturated rock grain", "Desaturation")
    graph.link(color, neutral, "Input")
    graph.link(graph.scalar("Desaturation", 0.82), neutral, "Fraction")
    tinted = graph.node("Warm PBR rock", "Multiply")
    graph.link(neutral, tinted, "A")
    graph.link(graph.color("RockTint", (0.36, 0.28, 0.22)), tinted, "B", "RGB")
    graph.output(tinted, "BASE_COLOR")

    blended_normal = blend("Normal", "NORMAL", "RGB")
    flat_normal = graph.node("Flat tangent normal", "Constant3Vector",
                             constant=unreal.LinearColor(0, 0, 1, 0))
    restrained_normal = graph.node("Restrained rock normal", "LinearInterpolate")
    graph.link(flat_normal, restrained_normal, "A")
    graph.link(blended_normal, restrained_normal, "B")
    graph.link(graph.scalar("NormalStrength", 0.32), restrained_normal, "Alpha")
    normalized = graph.node("Normalized rock detail", "Normalize")
    graph.link(restrained_normal, normalized, "VectorInput")
    graph.output(normalized, "NORMAL")

    roughness = graph.node("Rock roughness bounds", "Clamp",
                           min_default=0.42, max_default=0.98)
    graph.link(blend("Roughness", "MASKS", "R"), roughness, "Input")
    graph.output(roughness, "ROUGHNESS")
    graph.output(blend("AO", "MASKS", "R"), "AMBIENT_OCCLUSION")
    graph.output(graph.node("Nonmetal rock", "Constant", r=0.0), "METALLIC")
    validation = graph.finish()
    validation["texture_samples"] = 12
    validation["mapping"] = {
        "mode": "three planar world-position projections",
        "blend_weights": "squared absolute world vertex normal; weights sum to one",
        "tile_width_cm": helper.TILE_CM, "stable_under_instance_scale": True,
        "scope": "small scenery rocks only"}
    validation["pbr_maps"] = ["Color", "Normal", "Roughness", "AO"]
    validation["parameters"] = {
        "vectors": ["RockTint"], "scalars": ["Desaturation", "NormalStrength"]}
    return graph.material, validation


def _scenery_rock_instance(helper, parent):
    """Create the warm rock palette on the scale-stable scenery graph."""
    name = "MI_CinderSceneryRock"
    path = DESTINATION + "/" + name
    instance = unreal.load_asset(path)
    if instance is None:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DESTINATION, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        helper.require(isinstance(instance, unreal.MaterialInstanceConstant),
                       "could not create " + path)
        helper.tag(instance, "VisualOwner", OWNER)
    helper.require(isinstance(instance, unreal.MaterialInstanceConstant)
                   and helper.metadata(instance, "VisualOwner") == OWNER,
                   "refusing to replace an unrelated material instance at " + path)
    LIB.set_material_instance_parent(instance, parent)
    helper.require(instance.get_editor_property("parent") == parent,
                   "scenery rock parent did not persist")

    tint = (0.36, 0.28, 0.22)
    LIB.set_material_instance_vector_parameter_value(
        instance, "RockTint", unreal.LinearColor(*tint, 1.0))
    actual_tint = LIB.get_material_instance_vector_parameter_value(instance, "RockTint")
    helper.require(all(math.isclose(actual, expected, abs_tol=1e-5)
                       for actual, expected in zip(
                           (actual_tint.r, actual_tint.g, actual_tint.b, actual_tint.a),
                           (*tint, 1.0))), "RockTint readback failed at " + path)
    scalars = {"Desaturation": 0.82, "NormalStrength": 0.32}
    for parameter, value in scalars.items():
        LIB.set_material_instance_scalar_parameter_value(instance, parameter, value)
        actual = LIB.get_material_instance_scalar_parameter_value(instance, parameter)
        helper.require(math.isclose(actual, value, abs_tol=1e-5),
                       parameter + " readback failed at " + path)
    helper.tag(instance, "VisualVersion", VERSION)
    helper.tag(instance, "VisualPalette", "SceneryRockWorldPBR")
    helper.save(instance)
    return {"path": instance.get_path_name(), "slot": "SceneryRock",
            "parent": parent.get_path_name(), "parameters": {
                "RockTint": list(tint), **scalars},
            "pbr_maps": ["Color", "Normal", "Roughness", "AO"],
            "texture_samples": 12,
            "coordinates": "three world-position projections blended by world vertex normal"}


def _model_material(helper):
    graph = helper.Graph("M_CinderModelV3")
    tint = graph.color("Tint", (0.07, 0.09, 0.105))
    vertex_color = graph.node("Mesh vertex color", "VertexColor")

    # Two low-frequency, authored-UV waves make a soft patch signal. Its effect
    # stays small enough that it cannot read as painted lines or crosshatching.
    uv = graph.uv0(1.0)
    u = graph.node("Authored UV U", "ComponentMask", r=True, g=False, b=False, a=False)
    v = graph.node("Authored UV V", "ComponentMask", r=False, g=True, b=False, a=False)
    graph.link(uv, u, "Input")
    graph.link(uv, v, "Input")
    u_scale = graph.node("Irregular U frequency", "Multiply", const_b=1.73)
    v_scale = graph.node("Irregular V frequency", "Multiply", const_b=2.41)
    graph.link(u, u_scale, "A")
    graph.link(v, v_scale, "A")
    combined = graph.node("Broad finish coordinate", "Add")
    graph.link(u_scale, combined, "A")
    graph.link(v_scale, combined, "B")
    wave = graph.node("Broad finish wave", "Sine", period=1.0)
    graph.link(combined, wave, "Input")
    patch = graph.node("Unsigned finish patches", "Abs")
    graph.link(wave, patch, "Input")
    centered = graph.node("Centered finish patches", "Add", const_b=-0.5)
    graph.link(patch, centered, "A")
    variation = graph.node("Bounded finish variation", "Multiply")
    graph.link(centered, variation, "A")
    graph.link(graph.scalar("FinishVariation", 0.035), variation, "B")
    color_gain = graph.node("Finish color gain", "Add", const_b=1.0)
    graph.link(variation, color_gain, "A")
    finish_color = graph.node("Tint with subtle finish variation", "Multiply")
    graph.link(tint, finish_color, "A", "RGB")
    graph.link(color_gain, finish_color, "B")
    # Baked crevices read as real shadow rather than a faint tint. RecessDepth is
    # how far a fully occluded vertex darkens; 0 keeps the mesh flat-lit.
    recess = graph.scalar("RecessDepth", 0.58)
    vertex_ao_scale = graph.node("Visible baked AO scale", "Multiply")
    graph.link(vertex_color, vertex_ao_scale, "A", "A")
    graph.link(recess, vertex_ao_scale, "B")
    recess_floor = graph.node("Baked AO floor", "OneMinus")
    graph.link(recess, recess_floor, "Input")
    vertex_ao_floor = graph.node("Baked recess response", "Add")
    graph.link(vertex_ao_scale, vertex_ao_floor, "A")
    graph.link(recess_floor, vertex_ao_floor, "B")
    shaded = graph.node("Finish color with baked recess AO", "Multiply")
    graph.link(finish_color, shaded, "A")
    graph.link(vertex_ao_floor, shaded, "B")

    # Stylized sky term. Upward faces lift and undersides fall, so hard-surface
    # volumes read at phone zoom even where the key light does not reach them.
    # Mesh-size independent: it keys off the vertex normal, not object bounds.
    normal_ws = graph.node("World vertex normal", "VertexNormalWS")
    normal_z = graph.node("Vertex normal up component", "ComponentMask",
                          r=False, g=False, b=True, a=False)
    graph.link(normal_ws, normal_z, "Input")
    # gain = 1 + normal.z * SkyShading * 0.5, so a horizontal face stays exactly
    # neutral and only the tilt away from level moves the albedo.
    sky_half = graph.node("Half sky shading", "Multiply", const_b=0.5)
    graph.link(graph.scalar("SkyShading", 0.30), sky_half, "A")
    sky_signed = graph.node("Signed sky shading", "Multiply")
    graph.link(normal_z, sky_signed, "A")
    graph.link(sky_half, sky_signed, "B")
    sky_gain = graph.node("Sky shading gain", "Add", const_b=1.0)
    graph.link(sky_signed, sky_gain, "A")
    color = graph.node("Shaded color with sky term", "Multiply")
    graph.link(shaded, color, "A")
    graph.link(sky_gain, color, "B")
    graph.output(color, "BASE_COLOR")

    rough_variation = graph.node("Finish roughness variation", "Multiply", const_b=0.10)
    graph.link(centered, rough_variation, "A")
    wear_roughness = graph.node("Wear roughness response", "Multiply")
    graph.link(patch, wear_roughness, "A")
    graph.link(graph.scalar("SurfaceWear", 0.08), wear_roughness, "B")
    rough_with_finish = graph.node("Roughness plus finish", "Add")
    graph.link(graph.scalar("Roughness", 0.50), rough_with_finish, "A")
    graph.link(rough_variation, rough_with_finish, "B")
    rough_with_wear = graph.node("Roughness plus wear", "Add")
    graph.link(rough_with_finish, rough_with_wear, "A")
    graph.link(wear_roughness, rough_with_wear, "B")
    roughness = graph.node("Physical roughness bounds", "Clamp",
                           min_default=0.12, max_default=0.96)
    graph.link(rough_with_wear, roughness, "Input")
    # Grazing-angle sheen, three ALU nodes and no sampler: pull the clamped
    # roughness towards 0.22 only where the surface turns away from the camera, so
    # hard edges catch a glancing highlight instead of reading as matte plastic.
    # Exponent 8 is deliberately much tighter than the silhouette rim's 3.2 - the
    # rim owns the broad readable band, this only touches the last few degrees, so
    # the two never stack into a halo.
    sheen = graph.node("Grazing angle sheen", "Fresnel", exponent=8.0, base_reflect_fraction=0.0)
    polished = graph.node("Roughness with grazing sheen", "LinearInterpolate", const_b=0.22)
    graph.link(roughness, polished, "A")
    graph.link(sheen, polished, "Alpha")
    graph.output(polished, "ROUGHNESS")
    graph.output(graph.scalar("Metallic", 0.18), "METALLIC")

    ao = graph.node("Vertex AO times instance AO", "Multiply")
    graph.link(vertex_color, ao, "A", "A")
    graph.link(graph.scalar("AO", 1.0), ao, "B")
    graph.output(ao, "AMBIENT_OCCLUSION")

    glow = graph.node("Restrained emissive", "Multiply")
    graph.link(tint, glow, "A", "RGB")
    graph.link(graph.scalar("GlowIntensity", 0.0), glow, "B")

    # Silhouette separation. A view-facing rim keeps a 40-pixel unit legible
    # against warm ground without depending on where the key light points. It is
    # emissive rather than a light, so it survives fog, shadow and the far zoom.
    # Exponent 3.2 rather than 4.0. MetalFX upscales from 80% linear with FXAA as
    # the only anti-aliasing, so a one or two pixel rim is smeared out of
    # existence before it reaches the panel. The broader band survives the upscale
    # and still reads as an edge rather than a glow.
    rim = graph.node("Silhouette rim", "Fresnel", exponent=3.2, base_reflect_fraction=0.0)
    rim_strength = graph.node("Rim strength", "Multiply")
    graph.link(rim, rim_strength, "A")
    graph.link(graph.scalar("RimLight", 0.09), rim_strength, "B")
    rim_color = graph.node("Rim colour", "Multiply")
    graph.link(graph.color("RimColor", (0.55, 0.72, 1.0)), rim_color, "A", "RGB")
    graph.link(rim_strength, rim_color, "B")
    emissive = graph.node("Emissive plus rim", "Add")
    graph.link(glow, emissive, "A")
    graph.link(rim_color, emissive, "B")
    graph.output(emissive, "EMISSIVE_COLOR")
    validation = graph.finish()

    expected_vectors = {"Tint", "RimColor"}
    expected_scalars = {"Roughness", "Metallic", "GlowIntensity",
                        "FinishVariation", "SurfaceWear", "AO",
                        "RecessDepth", "SkyShading", "RimLight"}
    helper.require(expected_vectors.issubset(
        {str(name) for name in LIB.get_vector_parameter_names(graph.material)}),
        "M_CinderModelV3 lacks Tint")
    helper.require(expected_scalars.issubset(
        {str(name) for name in LIB.get_scalar_parameter_names(graph.material)}),
        "M_CinderModelV3 lacks required scalar parameters")
    validation["parameters"] = {
        "vectors": sorted(expected_vectors), "scalars": sorted(expected_scalars)}
    validation["finish"] = {
        "coordinates": "authored UV0", "animated": False, "texture_samples": 0,
        "base_color_gain_bounds_at_default": [0.9825, 1.0175],
        "visible_vertex_ao_multiplier":
            "(1 - RecessDepth) + RecessDepth * vertex color alpha, RecessDepth 0.58",
        "ao_source": "vertex color alpha multiplied by AO parameter",
        "vertex_alpha_contract": "1.0 fully lit; lower values mark baked crevices",
        "sky_shading": "1 + world vertex normal Z * SkyShading * 0.5, neutral on level faces",
        "rim_light": "Fresnel exponent 3.2 times RimLight times RimColor, added to "
                     "emissive, not a scene light; band widened for 80% MetalFX plus FXAA",
        "grazing_sheen": "Fresnel exponent 8 lerps clamped roughness towards 0.22",
        "bloom_threshold_reference": BLOOM_THRESHOLD}
    instances = []
    for (name, slot, rgb, roughness_value, metallic, glow_value, finish, wear,
         ao_value, rim_light, rim_color) in MODEL_PALETTES:
        if slot == "CoreGlow":
            continue
        values = {"Tint": rgb, "Roughness": roughness_value, "Metallic": metallic,
                  "GlowIntensity": glow_value, "FinishVariation": finish,
                  "SurfaceWear": wear, "AO": ao_value, "RimLight": rim_light,
                  "RimColor": rim_color}
        instances.append(_instance(helper, graph.material, name, slot, values))
    return validation, instances


def _core_surface_material(helper):
    graph = helper.Graph("M_VT_CoreSurface")
    helper.set_checked(graph.material, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tint = graph.color("Tint", (0.18, 0.46, 0.40))
    tinted_glow = graph.node("Tint times restrained glow", "Multiply")
    graph.link(tint, tinted_glow, "A", "RGB")
    graph.link(graph.scalar("GlowIntensity", 0.80), tinted_glow, "B")
    vertex_color = graph.node("Mesh vertex color", "VertexColor")
    vertex_ao = graph.node("Gentle vertex alpha AO", "Multiply", const_b=0.25)
    graph.link(vertex_color, vertex_ao, "A", "A")
    vertex_ao_floor = graph.node("Core AO floor", "Add", const_b=0.75)
    graph.link(vertex_ao, vertex_ao_floor, "A")
    emissive = graph.node("Stable teal core emissive", "Multiply")
    graph.link(tinted_glow, emissive, "A")
    graph.link(vertex_ao_floor, emissive, "B")
    graph.output(emissive, "EMISSIVE_COLOR")
    validation = graph.finish()
    validation["parameters"] = {
        "vectors": ["Tint"], "scalars": ["GlowIntensity"]}
    validation["lighting"] = {
        "shading": "opaque unlit", "texture_samples": 0,
        "vertex_alpha_multiplier": "0.75 + 0.25 * alpha",
        "directional_light_independent": True}
    return graph.material, validation


def _dirt_road_material(helper):
    texture_path = "/Game/Art/Textures/VisualUpgrade/T_CinderGroundV2_Color"
    texture = unreal.load_asset(texture_path)
    helper.require(isinstance(texture, unreal.Texture2D),
                   "missing licensed road albedo " + texture_path)
    graph = helper.Graph("M_VT_DirtRoad")
    sample = graph.sample("GroundColor", texture,
                          graph.world_uv(GROUND_ALBEDO_TILE_CM, "Road "), "COLOR",
                          helper.WRAP_SAMPLER_GROUP)
    neutral = graph.node("Packed earth grain", "Desaturation")
    graph.link(sample, neutral, "Input", "RGB")
    graph.link(graph.scalar("Desaturation", 0.58), neutral, "Fraction")
    color = graph.node("Dark packed earth", "Multiply")
    graph.link(neutral, color, "A")
    graph.link(graph.color("Tint", (0.30, 0.22, 0.14)), color, "B", "RGB")
    graph.output(color, "BASE_COLOR")
    graph.output(graph.node("Packed earth roughness", "Constant", r=0.95), "ROUGHNESS")
    graph.output(graph.node("Nonmetal road", "Constant", r=0.0), "METALLIC")
    graph.output(graph.node("Flat road normal", "Constant3Vector",
                            constant=unreal.LinearColor(0, 0, 1, 0)), "NORMAL")
    validation = graph.finish()
    validation["texture_samples"] = 1
    validation["mapping"] = {
        "mode": "planar absolute world XY", "tile_width_cm": GROUND_ALBEDO_TILE_CM,
        "shared_model_finish": False, "animated": False}
    validation["parameters"] = {
        "vectors": ["Tint"], "scalars": ["Desaturation"]}
    validation["surface"] = {"roughness": 0.95, "metallic": 0.0,
                             "normal": [0.0, 0.0, 1.0]}
    return validation


def _coarse_ground_textures(helper, report):
    """Import the verified 2K CC0 set beside the generated materials."""
    textures = {}
    previous_destination = helper.TEXTURES
    previous_tile = helper.TILE_CM
    helper.TEXTURES = DESTINATION
    helper.TILE_CM = COARSE_GROUND_TILE_CM
    try:
        for role, (filename, expected_md5) in COARSE_GROUND_FILES.items():
            source = COARSE_GROUND_SOURCE / filename
            helper.require(source.is_file(), "missing coarse ground texture " + str(source))
            actual_md5 = hashlib.md5(source.read_bytes(), usedforsecurity=False).hexdigest()
            helper.require(actual_md5 == expected_md5,
                           filename + " does not match the official Poly Haven checksum")
            asset = helper.texture(source, "T_VT_DryGroundRocks_" + role, role, report,
                                   os.environ.get("CINDER_REIMPORT_VISUALS") == "1")
            helper.tag(asset, "SourceLicense", "CC0-1.0")
            helper.tag(asset, "SourceURL", "https://polyhaven.com/a/dry_ground_rocks")
            helper.tag(asset, "PolyHavenAsset", "dry_ground_rocks")
            helper.tag(asset, "SourceMD5", expected_md5)
            helper.tag(asset, "TileWidthCm", COARSE_GROUND_TILE_CM)
            helper.save(asset)
            report[-1].update({
                "md5": expected_md5, "license": "CC0-1.0",
                "source_url": "https://polyhaven.com/a/dry_ground_rocks",
                "tile_width_cm": COARSE_GROUND_TILE_CM})
            textures[role] = asset
    finally:
        helper.TEXTURES = previous_destination
        helper.TILE_CM = previous_tile
    return textures


def _terrain_material(helper, textures):
    mask_path = "/Game/Art/Textures/VisualUpgrade/T_CinderTerrainLayersDefault"
    mask = unreal.load_asset(mask_path)
    helper.require(isinstance(mask, unreal.Texture2D),
                   "missing existing terrain layer mask " + mask_path)

    graph = helper.Graph("M_CinderGroundV4")
    surface_uv = graph.world_uv(COARSE_GROUND_TILE_CM, "Dry ground ")
    primary_color = graph.sample("GroundColor", textures["Color"], surface_uv, "COLOR",
                                 helper.WRAP_SAMPLER_GROUP)

    # A second lookup of the same photograph breaks the visible 400 cm repeat.
    # Rotate explicitly with scalar arithmetic so the graph uses only stable UE
    # material nodes, then apply an irrational frequency ratio and UV offset.
    primary_u = graph.node("Primary ground U", "ComponentMask",
                           r=True, g=False, b=False, a=False)
    primary_v = graph.node("Primary ground V", "ComponentMask",
                           r=False, g=True, b=False, a=False)
    graph.link(surface_uv, primary_u, "Input")
    graph.link(surface_uv, primary_v, "Input")
    u_cos = graph.node("Rotated U cosine", "Multiply", const_b=0.79863551)
    v_minus_sin = graph.node("Rotated U negative sine", "Multiply", const_b=-0.60181502)
    graph.link(primary_u, u_cos, "A")
    graph.link(primary_v, v_minus_sin, "A")
    rotated_u = graph.node("Ground U rotated 37 degrees", "Add")
    graph.link(u_cos, rotated_u, "A")
    graph.link(v_minus_sin, rotated_u, "B")
    u_sin = graph.node("Rotated V sine", "Multiply", const_b=0.60181502)
    v_cos = graph.node("Rotated V cosine", "Multiply", const_b=0.79863551)
    graph.link(primary_u, u_sin, "A")
    graph.link(primary_v, v_cos, "A")
    rotated_v = graph.node("Ground V rotated 37 degrees", "Add")
    graph.link(u_sin, rotated_v, "A")
    graph.link(v_cos, rotated_v, "B")
    secondary_u_scale = graph.node("Golden ratio secondary U", "Multiply", const_b=1.61803399)
    secondary_v_scale = graph.node("Golden ratio secondary V", "Multiply", const_b=1.61803399)
    graph.link(rotated_u, secondary_u_scale, "A")
    graph.link(rotated_v, secondary_v_scale, "A")
    secondary_u = graph.node("Offset secondary U", "Add", const_b=0.173)
    secondary_v = graph.node("Offset secondary V", "Add", const_b=0.619)
    graph.link(secondary_u_scale, secondary_u, "A")
    graph.link(secondary_v_scale, secondary_v, "A")
    secondary_uv = graph.node("Rotated offset ground UV", "AppendVector")
    graph.link(secondary_u, secondary_uv, "A")
    graph.link(secondary_v, secondary_uv, "B")
    secondary_color = graph.sample("GroundColorOffset", textures["Color"],
                                   secondary_uv, "COLOR", helper.WRAP_SAMPLER_GROUP)
    photographed = graph.node("Low-cost anti-tiled ground color", "LinearInterpolate")
    graph.link(primary_color, photographed, "A", "RGB")
    graph.link(secondary_color, photographed, "B", "RGB")
    graph.link(graph.scalar("AntiTileBlend", 0.35), photographed, "Alpha")
    varied_base = graph.node("Warm photographed dry ground", "Multiply")
    graph.link(photographed, varied_base, "A")
    graph.link(graph.color("BasaltTint", (0.58, 0.54, 0.48)),
               varied_base, "B", "RGB")

    # Deliberately NOT in the shared wrap group. The mask is TA_CLAMP and covers
    # exactly one battlefield; a wrap sampler would tile it past the map edge and
    # paint unobserved simulation state into the border. Fog privacy is
    # correctness, so this keeps its own sampler slot.
    layers = graph.sample("TerrainLayers", mask,
                          graph.world_uv(prefix="Layers "), "LINEAR_COLOR")

    def tint_layer(label, current, tint_name, tint, channel, strength_name, strength):
        tinted = graph.node(label + " photographed detail", "Multiply")
        graph.link(varied_base, tinted, "A")
        graph.link(graph.color(tint_name, tint), tinted, "B", "RGB")
        weight = graph.node(label + " weight", "Multiply")
        graph.link(layers, weight, "A", channel)
        graph.link(graph.scalar(strength_name, strength), weight, "B")
        result = graph.node(label + " transition", "LinearInterpolate")
        graph.link(current, result, "A")
        graph.link(tinted, result, "B")
        graph.link(weight, result, "Alpha")
        return result

    color = tint_layer("Ochre dust", varied_base, "DustTint", (1.08, 0.94, 0.76),
                       "B", "DustStrength", 0.22)
    color = tint_layer("Dark recess stone", color, "StoneTint", (0.56, 0.62, 0.68),
                       "R", "StoneStrength", 0.34)
    color = tint_layer("Iron mineral", color, "MineralTint", (1.12, 0.66, 0.42),
                       "G", "MineralStrength", 0.25)

    rough_sample = graph.sample("GroundRoughness", textures["Roughness"], surface_uv, "MASKS",
                                helper.WRAP_SAMPLER_GROUP)
    inverse_rough = graph.node("Poly Haven surface cuts", "OneMinus")
    graph.link(rough_sample, inverse_rough, "Input", "R")
    road_mask = graph.node("Road scuffs within dust", "Multiply")
    graph.link(inverse_rough, road_mask, "A")
    graph.link(layers, road_mask, "B", "B")
    road_weight = graph.node("Road scuff weight", "Multiply")
    graph.link(road_mask, road_weight, "A")
    graph.link(graph.scalar("RoadScuffStrength", 0.18), road_weight, "B")
    road_color = graph.node("Road scuff grain", "Multiply")
    graph.link(varied_base, road_color, "A")
    graph.link(graph.color("RoadScuffTint", (0.48, 0.44, 0.40)),
               road_color, "B", "RGB")
    scuffed = graph.node("Textured road wear", "LinearInterpolate")
    graph.link(color, scuffed, "A")
    graph.link(road_color, scuffed, "B")
    graph.link(road_weight, scuffed, "Alpha")
    graph.output(scuffed, "BASE_COLOR")

    graph.normal(graph.sample("GroundNormal", textures["Normal"], surface_uv, "NORMAL",
                              helper.WRAP_SAMPLER_GROUP), 0.86)
    rough_centered = graph.node("Centered ground roughness", "Add", const_b=-0.5)
    graph.link(rough_sample, rough_centered, "A", "R")
    rough_response = graph.node("Ground roughness response", "Multiply", const_b=0.72)
    graph.link(rough_centered, rough_response, "A")
    rough_midpoint = graph.node("Ground roughness midpoint", "Add", const_b=0.69)
    graph.link(rough_response, rough_midpoint, "A")
    base_rough = graph.node("Bounded base roughness", "Clamp",
                            min_default=0.46, max_default=0.96)
    graph.link(rough_midpoint, base_rough, "Input")
    dusty_rough = graph.node("Ochre dust roughness", "LinearInterpolate", const_b=0.94)
    graph.link(base_rough, dusty_rough, "A")
    graph.link(layers, dusty_rough, "Alpha", "B")
    road_rough_delta = graph.node("Road roughness reduction", "Multiply", const_b=-0.10)
    graph.link(road_weight, road_rough_delta, "A")
    final_rough = graph.node("Ground roughness with road scuffs", "Add")
    graph.link(dusty_rough, final_rough, "A")
    graph.link(road_rough_delta, final_rough, "B")
    bounded_rough = graph.node("Ground roughness bounds", "Clamp",
                               min_default=0.32, max_default=0.98)
    graph.link(final_rough, bounded_rough, "Input")
    graph.output(bounded_rough, "ROUGHNESS")
    graph.output(graph.sample("GroundAO", textures["AO"], surface_uv, "MASKS",
                              helper.WRAP_SAMPLER_GROUP), "AMBIENT_OCCLUSION", "R")
    graph.output(graph.node("Nonmetal ground", "Constant", r=0.0), "METALLIC")
    validation = graph.finish()
    validation["texture_samples"] = 6
    validation["sampler_slots"] = {
        "shared_world_wrap_group": ["GroundColor", "GroundColorOffset", "GroundNormal",
                                    "GroundRoughness", "GroundAO"],
        "own_slot": ["TerrainLayers"],
        "reason": "clamped fog-privacy mask must never sample in a wrap group"}
    validation["mapping"] = {
        "pbr_set": "primary planar absolute world XY / 400 cm",
        "albedo_anti_tiling": {
            "primary_weight": 0.65, "secondary_weight": 0.35,
            "secondary_rotation_degrees": 37.0,
            "secondary_frequency_ratio": 1.61803399,
            "secondary_uv_offset": [0.173, 0.619],
            "normal_roughness_ao_alignment": "primary projection"},
        "terrain_mask": "planar absolute world XY * CinderWorldSizeInverse",
        "world_aligned_triplanar_samples": 0}
    validation["albedo_response"] = {
        "source": "Poly Haven dry_ground_rocks 2K diffuse",
        "basis": "direct photographed RGB times BasaltTint; no clamp or desaturation",
        "runtime_tints_are_low_strength_multipliers": True,
        "regular_procedural_bands": False}
    validation["runtime_mask"] = {
        "parameter": "TerrainLayers", "channels": {
            "R": "explored cliff stone", "G": "observed mineral positions",
            "B": "map-seeded ash and dust", "A": "unused opaque upload channel"}}
    validation["parameters"] = {
        "vectors": ["BasaltTint", "DustTint", "StoneTint", "MineralTint", "RoadScuffTint"],
        "scalars": ["AntiTileBlend", "DustStrength", "StoneStrength", "MineralStrength",
                    "RoadScuffStrength", "NormalStrength"],
        "textures": ["GroundColor", "GroundColorOffset", "GroundNormal",
                     "GroundRoughness", "GroundAO", "TerrainLayers"]}
    return validation


def _effect_materials(helper):
    results = []

    emissive = helper.Graph("M_VT_Emissive", fog=True)
    helper.set_checked(emissive.material, "blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    glow = emissive.node("Tint times glow", "Multiply")
    emissive.link(emissive.color("Tint", (0.30, 0.85, 0.72)), glow, "A", "RGB")
    emissive.link(emissive.scalar("GlowIntensity", 0.55), glow, "B")
    emissive.output(glow, "EMISSIVE_COLOR")
    result = emissive.finish()
    result["parameters"] = {"Tint": [0.30, 0.85, 0.72], "GlowIntensity": 0.55}
    result["usage"] = "two-sided unlit additive world-effect mesh"
    results.append(result)

    results.append(_dust_material(helper))
    return results


def _dust_material(helper):
    """Build only the cloud material, without touching other effects or terrain."""
    dust = helper.Graph("M_VT_Dust", fog=True)
    dust.output(dust.color("Tint", (0.38, 0.20, 0.085)), "EMISSIVE_COLOR", "RGB")
    # DepthFade softens intersections, but leaves a free-floating quad's outer
    # edges opaque. A UV disc supplies the cloud silhouette without a texture.
    # Its 0.48 radius reaches zero before any quad edge; squaring the falloff
    # also makes the opacity derivative zero there, avoiding a hard circular rim.
    centered_uv = dust.node("Cloud centered UV", "Subtract", const_b=0.5)
    dust.link(dust.uv0(1.0), centered_uv, "A")
    radius_squared = dust.node("Cloud radius squared", "DotProduct")
    dust.link(centered_uv, radius_squared, "A")
    dust.link(centered_uv, radius_squared, "B")
    normalized_radius = dust.node("Cloud normalized squared radius", "Multiply",
                                  const_b=1.0 / (0.48 * 0.48))
    dust.link(radius_squared, normalized_radius, "A")
    radial_falloff = dust.node("Cloud radial falloff", "OneMinus")
    dust.link(normalized_radius, radial_falloff, "Input")
    disc = dust.node("Cloud disc bounds", "Clamp", min_default=0.0, max_default=1.0)
    dust.link(radial_falloff, disc, "Input")
    soft_disc = dust.node("Cloud soft edge", "Multiply")
    dust.link(disc, soft_disc, "A")
    dust.link(disc, soft_disc, "B")
    cloud_opacity = dust.node("Cloud opacity", "Multiply")
    dust.link(soft_disc, cloud_opacity, "A")
    dust.link(dust.scalar("Opacity", 0.22), cloud_opacity, "B")
    depth_fade = dust.node("Soft intersection fade", "DepthFade")
    dust.link(cloud_opacity, depth_fade, "Opacity")
    dust.link(dust.scalar("FadeDistance", 42.0), depth_fade, "FadeDistance")
    dust.output(depth_fade, "OPACITY")
    result = dust.finish()
    result["parameters"] = {"Tint": [0.38, 0.20, 0.085], "Opacity": 0.22,
                            "FadeDistance": 42.0}
    result["usage"] = "two-sided unlit translucent cloud with soft radial and intersection fades"
    result["opacity_shape"] = {"uv_channel": 0, "radius": 0.48,
                               "formula": "Opacity * clamp(1 - dot(UV0 - 0.5, UV0 - 0.5) / 0.48^2, 0, 1)^2 * depth fade",
                               "texture_samples": 0, "edge_opacity": 0.0}
    return result


def build_visual_target_materials():
    """Build, compile, save, and report every Visual Target material asset."""
    helper = _load_graph_helper()
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    report = {"success": False, "version": VERSION,
              "utc": datetime.now(timezone.utc).isoformat(), "owner": OWNER,
              "generator_sha256": helper.sha256(__file__), "materials": [],
              "instances": [], "effects": [], "textures": []}
    try:
        unreal.EditorAssetLibrary.make_directory(DESTINATION)
        model, report["instances"] = _model_material(helper)
        core, core_validation = _core_surface_material(helper)
        report["instances"].append(_core_instance(helper, core))
        rock, rock_validation = _scenery_rock_material(helper)
        report["instances"].append(_scenery_rock_instance(helper, rock))
        ground_textures = _coarse_ground_textures(helper, report["textures"])
        ground = _terrain_material(helper, ground_textures)
        road = _dirt_road_material(helper)
        report["effects"] = _effect_materials(helper)
        report["materials"] = [model, core_validation, ground, rock_validation, road]
        report["asset_paths"] = {
            "model": DESTINATION + "/M_CinderModelV3",
            "core_surface": DESTINATION + "/M_VT_CoreSurface",
            "ground": DESTINATION + "/M_CinderGroundV4",
            "dirt_road": DESTINATION + "/M_VT_DirtRoad",
            "scenery_rock": DESTINATION + "/M_VT_SceneryRock",
            "emissive": DESTINATION + "/M_VT_Emissive",
            "dust": DESTINATION + "/M_VT_Dust"}
        report["success"] = True
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        REPORT_PATH.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    unreal.log("CINDERLINE_VISUAL_TARGET_MATERIALS_OK: 7 base materials, "
               + str(len(report["instances"])) + " palettes; " + str(REPORT_PATH))
    return report


if __name__ == "__main__":
    build_visual_target_materials()
