"""Build the original fog-aware industrial map architecture material.

Call build_map_architecture() in a render-capable Unreal editor. This only owns
M_CinderMapArchitecture; it neither imports geometry nor changes level actors.
The small graph helpers also supply the authored-map paving in canyon ground.
"""
from __future__ import annotations

import importlib.util
import json
import traceback
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[1]
OWNER = "scripts/unreal_map_architecture.py"
VERSION = "cinder-map-architecture-v2.1-explored-detail"
MATERIALS = "/Game/Art/Canyon/Materials"
MATERIAL_PATH = MATERIALS + "/M_CinderMapArchitecture"


def smoothstep(graph, label, source, lower, upper, channel=""):
    """Explicit polynomial avoids opaque material-function graph connections."""
    shifted = graph.node(label + " threshold", "Subtract", const_b=lower)
    graph.link(source, shifted, "A", channel)
    scaled = graph.node(label + " span", "Multiply", const_b=1.0 / (upper - lower))
    graph.link(shifted, scaled, "A")
    t = graph.node(label + " bounded fraction", "Clamp", min_default=0.0, max_default=1.0)
    graph.link(scaled, t, "Input")
    squared = graph.node(label + " square", "Multiply")
    graph.link(t, squared, "A")
    graph.link(t, squared, "B")
    doubled = graph.node(label + " doubled fraction", "Multiply", const_b=2.0)
    graph.link(t, doubled, "A")
    cubic_factor = graph.node(label + " cubic factor", "Subtract", const_a=3.0)
    graph.link(doubled, cubic_factor, "B")
    result = graph.node(label + " smooth transition", "Multiply")
    graph.link(squared, result, "A")
    graph.link(cubic_factor, result, "B")
    return result


def slab_pattern(graph, label, coordinates, tint, grain=None):
    """Metric UV in; original subtly varied panels, joints and grime out.

    No added texture samples. Tint is the surface albedo, not a multiplier of a
    dark soil albedo. The bounded per-cell modulation leaves units readable.
    """
    cell = graph.node(label + " panel index", "Floor")
    graph.link(coordinates, cell, "Input")
    cell_dot = graph.node(label + " panel index tone", "DotProduct")
    graph.link(cell, cell_dot, "A")
    # A low-amplitude irrational sequence stays stable in mobile mediump. A
    # conventional sin-hash times 43758 loses its fractional part in fp16 and
    # makes every panel identical on hardware even if desktop looks varied.
    graph.link(graph.node(label + " tone basis", "Constant2Vector", r=0.61803399, g=0.41421356), cell_dot, "B")
    cell_hash = graph.node(label + " individual panel tone", "Frac")
    graph.link(cell_dot, cell_hash, "Input")
    centered_cell = graph.node(label + " centered panel tone", "Subtract", const_b=0.5)
    graph.link(cell_hash, centered_cell, "A")
    tone_strength = graph.node(label + " bounded panel variation", "Clamp", min_default=0.0, max_default=0.4)
    graph.link(graph.scalar('PanelVariation', 0.24), tone_strength, 'Input')
    cell_gain = graph.node(label + " quiet panel variation", "Multiply")
    graph.link(centered_cell, cell_gain, "A")
    graph.link(tone_strength, cell_gain, "B")
    panel_tone = graph.node(label + " panel tone near unity", "Add", const_b=1.0)
    graph.link(cell_gain, panel_tone, "A")
    panels = graph.node(label + " tinted panels", "Multiply")
    graph.link(tint, panels, "A", "RGB")
    graph.link(panel_tone, panels, "B")
    if grain is not None:
        gain = graph.node(label + " fine aggregate contrast", "Multiply", const_b=0.15)
        graph.link(grain, gain, "A")
        bias = graph.node(label + " fine aggregate near unity", "Add", const_b=0.97)
        graph.link(gain, bias, "A")
        aggregate = graph.node(label + " bounded fine aggregate", "Clamp", min_default=0.98, max_default=1.04)
        graph.link(bias, aggregate, "Input")
        detailed = graph.node(label + " fine concrete aggregate", "Multiply")
        graph.link(panels, detailed, "A")
        graph.link(aggregate, detailed, "B")
        panels = detailed

    phase = graph.node(label + " panel interior", "Frac")
    graph.link(coordinates, phase, "Input")
    reverse = graph.node(label + " opposite panel edge", "OneMinus")
    graph.link(phase, reverse, "Input")
    edge_xy = graph.node(label + " nearest panel edges", "Min")
    graph.link(phase, edge_xy, "A")
    graph.link(reverse, edge_xy, "B")
    edge_x = graph.node(label + " horizontal edge distance", "ComponentMask", r=True, g=False, b=False, a=False)
    edge_y = graph.node(label + " vertical edge distance", "ComponentMask", r=False, g=True, b=False, a=False)
    graph.link(edge_xy, edge_x, "Input")
    graph.link(edge_xy, edge_y, "Input")
    edge = graph.node(label + " nearest joint distance", "Min")
    graph.link(edge_x, edge, "A")
    graph.link(edge_y, edge, "B")
    clean = smoothstep(graph, label + " clean interior", edge, 0.016, 0.115)
    grime = graph.node(label + " accumulated edge grime", "OneMinus")
    graph.link(clean, grime, "Input")
    wear_strength = graph.node(label + " bounded edge wear", "Clamp", min_default=0.0, max_default=0.45)
    graph.link(graph.scalar('EdgeWear', 0.22), wear_strength, 'Input')
    variable_wear = graph.node(label + " variable panel weathering", "LinearInterpolate", const_a=0.45, const_b=1.0)
    graph.link(cell_hash, variable_wear, 'Alpha')
    panel_wear = graph.node(label + " authored panel wear amount", "Multiply")
    graph.link(variable_wear, panel_wear, 'A')
    graph.link(wear_strength, panel_wear, 'B')
    grime_weight = graph.node(label + " weathering at slab edges", "Multiply")
    graph.link(grime, grime_weight, 'A')
    graph.link(panel_wear, grime_weight, 'B')
    grime_tone = graph.node(label + " mild edge grime tone", "OneMinus")
    graph.link(grime_weight, grime_tone, 'Input')
    weathered = graph.node(label + " weathered panel surface", "Multiply")
    graph.link(panels, weathered, "A")
    graph.link(grime_tone, weathered, "B")
    outside_joint = smoothstep(graph, label + " joint feather", edge, 0.004, 0.017)
    joint = graph.node(label + " recessed panel joint", "OneMinus")
    graph.link(outside_joint, joint, "Input")
    joint_color = graph.node(label + " restrained joint albedo", "Multiply", const_b=0.63)
    graph.link(tint, joint_color, "A", "RGB")
    color = graph.node(label + " panels with dark joints", "LinearInterpolate")
    graph.link(weathered, color, "A")
    graph.link(joint_color, color, "B")
    graph.link(joint, color, "Alpha")
    # A thin hairline on a small minority of slabs breaks the sterile grid.
    # This is a bounded analytic line with a quiet kink, not per-pixel noise.
    # Keeping its feature size tied to a slab avoids random crack maps swimming
    # across architecture as the camera moves or mobile mips change.
    u = graph.node(label + " slab local U", 'ComponentMask', r=True, g=False, b=False, a=False)
    v = graph.node(label + " slab local V", 'ComponentMask', r=False, g=True, b=False, a=False)
    graph.link(phase, u, 'Input')
    graph.link(phase, v, 'Input')
    crack_slope = graph.node(label + " hairline direction", 'Multiply', const_b=0.37)
    graph.link(u, crack_slope, 'A')
    crack_offset = graph.node(label + " hairline start", 'Add', const_b=0.22)
    graph.link(crack_slope, crack_offset, 'A')
    kink_phase = graph.node(label + " hairline kink phase", 'Multiply', const_b=2.7)
    graph.link(u, kink_phase, 'A')
    kink = graph.node(label + " hairline kink", 'Sine', period=1.0)
    graph.link(kink_phase, kink, 'Input')
    small_kink = graph.node(label + " slight fracture irregularity", 'Multiply', const_b=0.017)
    graph.link(kink, small_kink, 'A')
    crack_curve = graph.node(label + " hairline curve", 'Add')
    graph.link(crack_offset, crack_curve, 'A')
    graph.link(small_kink, crack_curve, 'B')
    crack_distance_signed = graph.node(label + " signed hairline distance", 'Subtract')
    graph.link(v, crack_distance_signed, 'A')
    graph.link(crack_curve, crack_distance_signed, 'B')
    crack_distance = graph.node(label + " hairline distance", 'Abs')
    graph.link(crack_distance_signed, crack_distance, 'Input')
    crack_width = graph.node(label + " hairline feather", 'Multiply', const_b=100.0)
    graph.link(crack_distance, crack_width, 'A')
    crack_outside = graph.node(label + " outside hairline", 'Clamp', min_default=0.0, max_default=1.0)
    graph.link(crack_width, crack_outside, 'Input')
    crack_line = graph.node(label + " hairline fracture", 'OneMinus')
    graph.link(crack_outside, crack_line, 'Input')
    crack_presence = smoothstep(graph, label + " occasional cracked slab", cell_hash, 0.80, 0.94)
    sparse_line = graph.node(label + " sparse slab fractures", 'Multiply')
    graph.link(crack_line, sparse_line, 'A')
    graph.link(crack_presence, sparse_line, 'B')
    crack_strength = graph.node(label + " bounded crack contrast", 'Clamp', min_default=0.0, max_default=0.5)
    graph.link(graph.scalar('CrackStrength', 0.24), crack_strength, 'Input')
    fracture_weight = graph.node(label + " restrained fracture contrast", 'Multiply')
    graph.link(sparse_line, fracture_weight, 'A')
    graph.link(crack_strength, fracture_weight, 'B')
    fracture_tone = graph.node(label + " hairline albedo modulation", 'OneMinus')
    graph.link(fracture_weight, fracture_tone, 'Input')
    cracked_color = graph.node(label + " worn slab finish", 'Multiply')
    graph.link(color, cracked_color, 'A')
    graph.link(fracture_tone, cracked_color, 'B')
    return cracked_color, joint, grime


def remembered_surface_fraction(graph, label, fog_sample, authored_surface=None):
    """Keep static, previously seen surface detail dim without revealing unknowns.

    R is fog opacity and G is explored coverage. The added term is zero on
    visible or unknown ground. Optional coverage confines it to authored paving;
    it never changes the fog texture or the visibility of units/buildings.
    """
    visible = graph.node(label + " visible fraction", "OneMinus")
    graph.link(fog_sample, visible, "Input", "R")
    known_fog = graph.node(label + " previously explored fog", "Multiply")
    graph.link(fog_sample, known_fog, "A", "R")
    graph.link(fog_sample, known_fog, "B", "G")
    if authored_surface is not None:
        coverage = graph.node(label + " authored static surface only", "Multiply")
        graph.link(known_fog, coverage, "A")
        graph.link(authored_surface, coverage, "B")
        known_fog = coverage
    dim_detail = graph.node(label + " dim remembered detail", "Multiply", const_b=0.28)
    graph.link(known_fog, dim_detail, "A")
    fraction = graph.node(label + " visible or remembered detail", "Add")
    graph.link(visible, fraction, "A")
    graph.link(dim_detail, fraction, "B")
    return fraction


def build_map_architecture():
    spec = importlib.util.spec_from_file_location("cinder_map_architecture_graph", ROOT / "scripts/unreal_visual_upgrade.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.OWNER, helper.VERSION, helper.MATERIALS = OWNER, VERSION, MATERIALS
    report = {"success": False, "source_sha256": helper.sha256(__file__)}
    target = ROOT / "artifacts/map-redesign/architecture-material-import.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        fog = unreal.load_asset("/Game/Art/Textures/VisualUpgrade/T_CinderFogDefaultV2")
        helper.require(isinstance(fog, unreal.Texture2D), "missing map architecture fog texture")
        helper.require(not fog.get_editor_property("srgb")
                       and fog.get_editor_property("address_x") == unreal.TextureAddress.TA_CLAMP
                       and fog.get_editor_property("address_y") == unreal.TextureAddress.TA_CLAMP,
                       "map fog must be linear and clamped")
        g = helper.Graph("M_CinderMapArchitecture")
        world = g.node("Architecture world position", "WorldPosition")
        components = {}
        for name, channels in (("X", (True, False, False)), ("Y", (False, True, False)), ("Z", (False, False, True))):
            component = g.node("Architecture world " + name, "ComponentMask", r=channels[0], g=channels[1], b=channels[2], a=False)
            g.link(world, component, "Input")
            components[name] = component
        normal = g.node("Architecture geometric world normal", "VertexNormalWS")
        absolute_normal = g.node("Architecture absolute world normal", "Abs")
        g.link(normal, absolute_normal, "Input")
        nx = g.node("Architecture X facing", "ComponentMask", r=True, g=False, b=False, a=False)
        ny = g.node("Architecture Y facing", "ComponentMask", r=False, g=True, b=False, a=False)
        nz = g.node("Architecture up facing", "ComponentMask", r=False, g=False, b=True, a=False)
        for component in (nx, ny, nz):
            g.link(absolute_normal, component, "Input")
        dominance = g.node("Wall projection facing difference", "Subtract")
        g.link(nx, dominance, "A")
        g.link(ny, dominance, "B")
        x_facing = smoothstep(g, "Wall projection axis", dominance, -0.10, 0.10)
        wall_h = g.node("Wall horizontal metric coordinate", "LinearInterpolate")
        g.link(components["X"], wall_h, "A")
        g.link(components["Y"], wall_h, "B")
        g.link(x_facing, wall_h, "Alpha")
        wall_hz = g.node("Wall horizontal and vertical metric coordinates", "AppendVector")
        g.link(wall_h, wall_hz, "A")
        g.link(components["Z"], wall_hz, "B")
        wall_uv = g.node("Wall panels 160 by 80 cm", "Multiply")
        g.link(wall_hz, wall_uv, "A")
        g.link(g.node("Wall panel reciprocal dimensions", "Constant2Vector", r=1.0 / 160.0, g=1.0 / 80.0), wall_uv, "B")
        top_xy = g.node("Architecture top metric coordinates", "ComponentMask", r=True, g=True, b=False, a=False)
        g.link(world, top_xy, "Input")
        top_uv = g.node("Top panels 120 by 90 cm", "Multiply")
        g.link(top_xy, top_uv, "A")
        g.link(g.node("Top panel reciprocal dimensions", "Constant2Vector", r=1.0 / 120.0, g=1.0 / 90.0), top_uv, "B")
        top_weight = smoothstep(g, "Top facing panels", nz, 0.70, 0.90)
        uv = g.node("World aligned structural panel coordinates", "LinearInterpolate")
        g.link(wall_uv, uv, "A")
        g.link(top_uv, uv, "B")
        g.link(top_weight, uv, "Alpha")
        tint = g.color("ArchitectureTint", (0.09, 0.095, 0.082))
        color, joint, _ = slab_pattern(g, "Structure", uv, tint)

        fog_uv = g.world_uv(prefix="Architecture fog ")
        fog_sample = g.sample("FogMask", fog, fog_uv, "LINEAR_COLOR")
        visible = remembered_surface_fraction(g, "Static architecture", fog_sample)
        visible_color = g.node("Fog gated architecture diffuse", "Multiply")
        g.link(color, visible_color, "A")
        g.link(visible, visible_color, "B")
        g.output(visible_color, "BASE_COLOR")
        fog_color = g.node("Unknown and explored architecture fog", "LinearInterpolate")
        g.link(g.color("UnknownFog", (0.010, 0.017, 0.027)), fog_color, "A", "RGB")
        g.link(g.color("ExploredFog", (0.027, 0.041, 0.049)), fog_color, "B", "RGB")
        g.link(fog_sample, fog_color, "Alpha", "G")
        fog_emission = g.node("Architecture fog independent of lighting", "Multiply")
        g.link(fog_color, fog_emission, "A")
        g.link(fog_sample, fog_emission, "B", "R")
        g.output(fog_emission, "EMISSIVE_COLOR")
        # Sink unknown geometry beneath the ground's -1 cm plane. Sharing its
        # plane would cause flattened triangles to reveal a z-fighting outline.
        height_fog = g.sample("ArchitectureHeightFog", fog, fog_uv, "LINEAR_COLOR")
        height_fog.set_editor_property("parameter_name", "FogMask")
        height_fog.set_editor_property("mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
        height_fog.set_editor_property("const_mip_value", 0)
        unknown = g.node("Unexplored architecture height fraction", "OneMinus")
        g.link(height_fog, unknown, "Input", "G")
        above = g.node("Architecture hidden baseline below terrain", "Add", const_b=32.0)
        g.link(components["Z"], above, "A")
        hidden = g.node("Unknown architecture height", "Multiply")
        g.link(above, hidden, "A")
        g.link(unknown, hidden, "B")
        flatten = g.node("Flatten unexplored architecture", "Multiply")
        g.link(hidden, flatten, "A")
        g.link(g.node("Architecture downward offset", "Constant3Vector", constant=unreal.LinearColor(0, 0, -1, 0)), flatten, "B")
        g.output(flatten, "WORLD_POSITION_OFFSET")
        g.output(g.node("Architecture geometric surface normals", "Constant3Vector", constant=unreal.LinearColor(0, 0, 1, 0)), "NORMAL")
        roughness = g.node("Matte structural surface roughness", "Clamp", min_default=0.65, max_default=1.0)
        g.link(g.scalar("Roughness", 0.90), roughness, "Input")
        rough_joints = g.node("Dry structural joints", "LinearInterpolate", const_b=0.97)
        g.link(roughness, rough_joints, "A")
        g.link(joint, rough_joints, "Alpha")
        g.output(rough_joints, "ROUGHNESS")
        ao = g.node("Restrained structural joint cavity", "LinearInterpolate", const_a=1.0, const_b=0.87)
        g.link(joint, ao, "Alpha")
        g.output(ao, "AMBIENT_OCCLUSION")
        g.output(g.node("Nonmetal structural concrete", "Constant", r=0.0), "METALLIC")
        g.output(g.node("Restrained structural specular", "Constant", r=0.30), "SPECULAR")
        report["material"] = g.finish()
        report["runtime_parameters"] = {"ArchitectureTint": [0.09, 0.095, 0.082], "Roughness": 0.90,
                                        "PanelVariation": 0.24, "EdgeWear": 0.22, "CrackStrength": 0.24,
                                        "FogMask": "linear clamped RG visibility/exploration", "CinderWorldSizeInverse": "1 / world size cm"}
        report["texture_samples"] = {"pixel": 1, "vertex": 1}
        report["surface"] = {"projection": "world XY tops; world horizontal/Z walls", "top_panel_cm": [120, 90],
                             "wall_panel_cm": [160, 80], "panel_tone_bounds": [0.88, 1.12],
                             "unknown_baseline_world_z": -32.0, "normal_textures": False}
        report["success"] = True
    except Exception as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        target.write_text(json.dumps(report, indent=2) + "\n")
    unreal.log("CINDERLINE_MAP_ARCHITECTURE_OK " + str(target))
    return report


if __name__ == "__main__":
    build_map_architecture()
