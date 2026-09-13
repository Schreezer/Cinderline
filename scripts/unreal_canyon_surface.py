"""Author the owned canyon ground material; reuse existing licensed PBR textures.

The cached linear mask supplies known stone, mineral, ash and service ground.
Fog is applied on the terrain itself so raised Landscape never reveals unknown
areas above the flat gameplay fog plane. No displacement or gameplay collision.
"""
import importlib.util
import json
import struct
import traceback
import zlib
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]


def build_canyon_surface():
    spec = importlib.util.spec_from_file_location('cinder_canyon_graph', ROOT / 'scripts/unreal_visual_upgrade.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.OWNER = 'scripts/unreal_canyon_surface.py'
    helper.VERSION = 'canyon-ground-v1'
    helper.MATERIALS = '/Game/Art/Canyon/Materials'
    helper.TEXTURES = '/Game/Art/Canyon/Textures'
    report = {'success': False, 'textures': [], 'source_sha256': helper.sha256(__file__)}
    target = ROOT / 'artifacts/canyon-hud/ground-import.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        path = ROOT / 'Intermediate/Canyon/T_CinderCanyonLayersDefault.png'
        path.parent.mkdir(parents=True, exist_ok=True)
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 6, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress((b'\0' + bytes(16)) * 4)) + chunk(b'IEND', b'')
        if not path.exists() or path.read_bytes() != png:
            path.write_bytes(png)
        mask = helper.texture(path, 'T_CinderCanyonLayersDefault', 'FogMask', report['textures'], False)
        fog = unreal.load_asset('/Game/Art/Textures/VisualUpgrade/T_CinderFogDefaultV2')
        textures = {role: unreal.load_asset('/Game/Art/Textures/VisualUpgrade/T_CinderGroundV2_' + role)
                    for role in ('Color', 'Normal', 'Roughness')}
        helper.require(all(isinstance(t, unreal.Texture2D) for t in [fog, *textures.values()]), 'missing existing PBR/fog texture')
        g = helper.Graph('M_CinderCanyonGround')
        uv = g.world_uv(246.0)
        grain = g.sample('GroundColor', textures['Color'], uv, 'COLOR')
        neutral = g.node('Neutral sand grain', 'Desaturation')
        g.link(grain, neutral, 'Input', 'RGB')
        g.link(g.scalar('Desaturation', 0.84), neutral, 'Fraction')
        base = g.node('Warm sediment', 'Multiply')
        g.link(neutral, base, 'A')
        g.link(g.color('SandTint', (0.64, 0.39, 0.21)), base, 'B', 'RGB')
        macro = g.sample('MacroGrain', textures['Color'], g.world_uv(1190.0, 'Macro '), 'COLOR')
        macro_luma = g.node('Broad ground variation', 'Desaturation')
        g.link(macro, macro_luma, 'Input', 'RGB')
        g.link(g.node('Macro neutral', 'Constant', r=1.0), macro_luma, 'Fraction')
        gain = g.node('Restrained macro gain', 'Multiply', const_b=1.2)
        g.link(macro_luma, gain, 'A')
        bias = g.node('Macro minimum', 'Add', const_b=1.22)
        g.link(gain, bias, 'A')
        varied = g.node('Varied sand', 'Multiply')
        g.link(base, varied, 'A')
        g.link(bias, varied, 'B')
        world_uv = g.world_uv(prefix='Map ')
        layers = g.sample('TerrainLayers', mask, world_uv, 'LINEAR_COLOR')
        def tint_layer(label, current, tint, channel, strength):
            tinted = g.node(label + ' grain', 'Multiply')
            g.link(neutral, tinted, 'A')
            g.link(g.color(label + ' tint', tint), tinted, 'B', 'RGB')
            weight = g.node(label + ' weight', 'Multiply', const_b=strength)
            g.link(layers, weight, 'A', channel)
            blend = g.node(label + ' blend', 'LinearInterpolate')
            g.link(current, blend, 'A')
            g.link(tinted, blend, 'B')
            g.link(weight, blend, 'Alpha')
            return blend
        color = tint_layer('Windblown dust', varied, (0.82, 0.60, 0.35), 'B', 0.40)
        color = tint_layer('Exposed sandstone', color, (0.53, 0.19, 0.075), 'R', 0.93)
        color = tint_layer('Mineral staining', color, (0.83, 0.46, 0.15), 'G', 0.52)
        color = tint_layer('Packed service ground', color, (0.43, 0.37, 0.29), 'A', 0.72)
        fog_sample = g.sample('FogMask', fog, world_uv, 'LINEAR_COLOR')
        visible = g.node('Known visible fraction', 'OneMinus')
        g.link(fog_sample, visible, 'Input', 'R')
        visible_color = g.node('Fog gated diffuse', 'Multiply')
        g.link(color, visible_color, 'A')
        g.link(visible, visible_color, 'B')
        g.output(visible_color, 'BASE_COLOR')
        fog_color = g.node('Unknown and explored fog', 'LinearInterpolate')
        g.link(g.color('UnknownFog', (0.010, 0.017, 0.027)), fog_color, 'A', 'RGB')
        g.link(g.color('ExploredFog', (0.027, 0.041, 0.049)), fog_color, 'B', 'RGB')
        g.link(fog_sample, fog_color, 'Alpha', 'G')
        fog_emission = g.node('Fog independent of lighting', 'Multiply')
        g.link(fog_color, fog_emission, 'A')
        g.link(fog_sample, fog_emission, 'B', 'R')
        g.output(fog_emission, 'EMISSIVE_COLOR')
        # Unknown relief must not silhouette or occlude visible ground. Flatten
        # unobserved height in the vertex shader; explored terrain keeps its shape.
        # Explicit mip 0 is required for vertex texture fetch on mobile Metal.
        height_fog = g.sample('HeightFog', fog, world_uv, 'LINEAR_COLOR')
        height_fog.set_editor_property('parameter_name', 'FogMask')
        height_fog.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
        height_fog.set_editor_property('const_mip_value', 0)
        unknown = g.node('Unexplored height fraction', 'OneMinus')
        g.link(height_fog, unknown, 'Input', 'G')
        world = g.node('Original terrain position', 'WorldPosition')
        z = g.node('Terrain height above baseline', 'ComponentMask', r=False, g=False, b=True, a=False)
        g.link(world, z, 'Input')
        above = g.node('Baseline at minus one', 'Add', const_b=1.0)
        g.link(z, above, 'A')
        hidden_height = g.node('Unknown relief', 'Multiply')
        g.link(above, hidden_height, 'A')
        g.link(unknown, hidden_height, 'B')
        flatten = g.node('Flatten unknown vertices', 'Multiply')
        g.link(hidden_height, flatten, 'A')
        g.link(g.color('Downward offset', (0.0, 0.0, -1.0)), flatten, 'B', 'RGB')
        g.output(flatten, 'WORLD_POSITION_OFFSET')
        g.normal(g.sample('GroundNormal', textures['Normal'], uv, 'NORMAL'), 0.78)
        roughness = g.node('Dry ground roughness', 'LinearInterpolate', const_b=0.96, const_alpha=0.70)
        g.link(g.sample('GroundRoughness', textures['Roughness'], uv, 'MASKS'), roughness, 'A', 'R')
        g.output(roughness, 'ROUGHNESS')
        zero = g.node('No metallic or specular reflection', 'Constant', r=0.0)
        g.output(zero, 'METALLIC')
        g.output(zero, 'SPECULAR')
        report['material'] = g.finish()
        report['texture_samples'] = {'pixel': 6, 'vertex': 1}
        report['channels'] = {'R': 'known stone', 'G': 'observed minerals', 'B': 'macro dust', 'A': 'level service pads and roads'}
        report['success'] = True
    except Exception as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        target.write_text(json.dumps(report, indent=2) + '\n')
    unreal.log('CINDERLINE_CANYON_GROUND_OK ' + str(target))
    return report


if __name__ == '__main__':
    build_canyon_surface()
