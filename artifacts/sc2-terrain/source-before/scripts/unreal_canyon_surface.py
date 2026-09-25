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
        world_uv = g.world_uv(prefix='Map ')
        # TerrainLayers, FogMask and HeightFog below stay on their own asset
        # samplers on purpose. They are TA_CLAMP and cover exactly one
        # battlefield; a shared wrap group would tile them past the map edge and
        # paint unobserved simulation state into the border. Fog privacy is
        # correctness, so these three never join the wrap group no matter how
        # tight the sampler budget gets.
        layers = g.sample('TerrainLayers', mask, world_uv, 'LINEAR_COLOR')

        # --- Cliff face shading -------------------------------------------------------
        # The obstacle cliffs are the only near-vertical surface the landscape has, and
        # until now this material had no idea they existed: every fetch below is projected
        # from world XY, which on a vertical face drags one texel column the whole height
        # of the wall. The props hid it. With the cliff body moving into the heightfield
        # there is nothing left to hide it, so slope awareness is what makes a sculpted
        # cliff read as rock rather than as smeared ground.
        terrain_normal = g.node('Terrain world normal', 'VertexNormalWS')
        up = g.node('Terrain up facing', 'ComponentMask', r=False, g=False, b=True, a=False)
        g.link(terrain_normal, up, 'Input')
        # THRESHOLD IS SET AGAINST THE SMOOTHED VERTEX NORMAL, NOT THE TRUE FACE ANGLE.
        # An obstacle is only about six vertices across at 38 cm spacing, so the landscape
        # averages each vertex normal over its neighbouring quads and the material sees a
        # face far gentler than the geometry actually is. Measured: a 72-74 degree sculpted
        # face reported N.z well above 0.62 over most of its area, and a 0.62 threshold lit
        # only a narrow stripe at the single steepest row while the rest of the wall kept
        # flat-ground shading. 0.88 covers the whole face.
        #
        # It is still unreachable from any lane: WalkableGradient 0.27 holds every walkable
        # sample at N.z >= 0.965, which leaves 0.085 of margin above this threshold, and the
        # known-stone gate below is a second independent guard.
        steep = g.node('Slope below flat', 'Subtract', const_a=0.88)
        g.link(up, steep, 'B')
        gain = g.node('Sharpened cliff slope', 'Multiply', const_b=8.0)
        g.link(steep, gain, 'A')
        slope = g.node('Saturated cliff slope', 'Clamp', min_default=0.0, max_default=1.0)
        g.link(gain, slope, 'Input')
        # Gating on the known-stone channel is correctness, not decoration. The fallback
        # ground is a scaled cube whose 30 cm border side walls are vertical, and a bare
        # slope mask would paint them as rock; R is 0 there. R also feathers outside each
        # rectangle, so the mask still has margin at the toe of the sculpted wall.
        cliff = g.node('Explored cliff face', 'Multiply')
        g.link(slope, cliff, 'A')
        g.link(layers, cliff, 'B', 'R')
        grain = g.sample('GroundColor', textures['Color'], uv, 'COLOR',
                         sampler_source=helper.WRAP_SAMPLER_GROUP)
        # Anti-tiling. The previous second octave re-sampled this same albedo at
        # 1190 cm, desaturated it to pure luminance, and multiplied it back in as
        # a value gain of 1.2 with a 1.22 bias. Two axis-aligned lookups of one
        # photograph mip to the same flat average at the shipped camera height,
        # so at play distance the ground was mathematically a constant colour.
        # Take a second full-colour lookup instead, on a UV rotated 37 degrees and
        # tiled at 0.618x the base (golden ratio, so the two grids never re-align
        # anywhere inside the battlefield), and keep the darker of the two. Still
        # exactly two colour fetches, so this is sampler-neutral.
        primary_u = g.node('Primary sand U', 'ComponentMask', r=True, g=False, b=False, a=False)
        primary_v = g.node('Primary sand V', 'ComponentMask', r=False, g=True, b=False, a=False)
        g.link(uv, primary_u, 'Input')
        g.link(uv, primary_v, 'Input')
        # Rotation written out as scalar arithmetic rather than through the
        # CustomRotator material function: Graph.finish() validates this material
        # edge by edge on readback, and a function-call node exposes no inputs to
        # validate. cos 37 deg = 0.79863551, sin 37 deg = 0.60181502.
        u_cos = g.node('Rotated sand U cosine', 'Multiply', const_b=0.79863551)
        v_negative_sin = g.node('Rotated sand U negative sine', 'Multiply', const_b=-0.60181502)
        g.link(primary_u, u_cos, 'A')
        g.link(primary_v, v_negative_sin, 'A')
        rotated_u = g.node('Sand U rotated 37 degrees', 'Add')
        g.link(u_cos, rotated_u, 'A')
        g.link(v_negative_sin, rotated_u, 'B')
        u_sin = g.node('Rotated sand V sine', 'Multiply', const_b=0.60181502)
        v_cos = g.node('Rotated sand V cosine', 'Multiply', const_b=0.79863551)
        g.link(primary_u, u_sin, 'A')
        g.link(primary_v, v_cos, 'A')
        rotated_v = g.node('Sand V rotated 37 degrees', 'Add')
        g.link(u_sin, rotated_v, 'A')
        g.link(v_cos, rotated_v, 'B')
        detail_u = g.node('Golden ratio sand U', 'Multiply', const_b=0.61803399)
        detail_v = g.node('Golden ratio sand V', 'Multiply', const_b=0.61803399)
        g.link(rotated_u, detail_u, 'A')
        g.link(rotated_v, detail_v, 'A')
        detail_uv = g.node('Rotated sand detail UV', 'AppendVector')
        g.link(detail_u, detail_uv, 'A')
        g.link(detail_v, detail_uv, 'B')
        detail = g.sample('GroundDetail', textures['Color'], detail_uv, 'COLOR',
                          sampler_source=helper.WRAP_SAMPLER_GROUP)
        # Minimum rather than a lerp: keeping the darker octave preserves the
        # crevice contrast of both grids, where averaging them pulls the result
        # back towards the flat mean this change exists to destroy.
        broken = g.node('Anti-tiled sand albedo', 'Min')
        g.link(grain, broken, 'A', 'RGB')
        g.link(detail, broken, 'B', 'RGB')
        # M2. One extra albedo fetch, projected from the side, for the cliff faces only.
        # Which side depends on which way the face points: |N.x| > |N.y| means the normal
        # runs along X, so the face spans Y and Z and its horizontal coordinate is world Y.
        cliff_world = g.node('Cliff world position', 'WorldPosition')
        cliff_wx = g.node('Cliff world X', 'ComponentMask', r=True, g=False, b=False, a=False)
        g.link(cliff_world, cliff_wx, 'Input')
        cliff_wy = g.node('Cliff world Y', 'ComponentMask', r=False, g=True, b=False, a=False)
        g.link(cliff_world, cliff_wy, 'Input')
        cliff_wz = g.node('Cliff world Z', 'ComponentMask', r=False, g=False, b=True, a=False)
        g.link(cliff_world, cliff_wz, 'Input')
        face_abs = g.node('Cliff normal magnitude', 'Abs')
        g.link(terrain_normal, face_abs, 'Input')
        face_ax = g.node('Cliff normal X magnitude', 'ComponentMask', r=True, g=False, b=False, a=False)
        g.link(face_abs, face_ax, 'Input')
        face_ay = g.node('Cliff normal Y magnitude', 'ComponentMask', r=False, g=True, b=False, a=False)
        g.link(face_abs, face_ay, 'Input')
        face_bias = g.node('Cliff facing difference', 'Subtract')
        g.link(face_ax, face_bias, 'A')
        g.link(face_ay, face_bias, 'B')
        face_gain = g.node('Sharpened cliff facing', 'Multiply', const_b=10.0)
        g.link(face_bias, face_gain, 'A')
        face_offset = g.node('Biased cliff facing', 'Add', const_b=0.5)
        g.link(face_gain, face_offset, 'A')
        face_axis = g.node('Saturated cliff facing', 'Clamp', min_default=0.0, max_default=1.0)
        g.link(face_offset, face_axis, 'Input')
        side_h = g.node('Cliff horizontal coordinate', 'LinearInterpolate')
        g.link(cliff_wx, side_h, 'A')
        g.link(cliff_wy, side_h, 'B')
        g.link(face_axis, side_h, 'Alpha')
        side_xy = g.node('Cliff side coordinates', 'AppendVector')
        g.link(side_h, side_xy, 'A')
        g.link(cliff_wz, side_xy, 'B')
        side_uv = g.node('Cliff side projection at 246 cm', 'Multiply', const_b=1.0 / 246.0)
        g.link(side_xy, side_uv, 'A')
        cliff_grain = g.sample('CliffGrain', textures['Color'], side_uv, 'COLOR',
                               sampler_source=helper.WRAP_SAMPLER_GROUP)
        # M3. Blend the two SAMPLES, never the two coordinate sets. Cross-fading the UVs
        # would drag one lookup across a large fraction of a tile inside the blend band and
        # smear the grain into exactly the streaks this whole block exists to remove - the
        # project's own cliff material at unreal_canyon_assets.py:272-278 says so in as many
        # words. NORMAL and ROUGHNESS deliberately stay on the flat-projected uv below:
        # feeding them a side projection would reinterpret a side-projected normal map
        # through the landscape's world-XY-derived tangent basis.
        face_mix = g.node('Cliff face grain', 'LinearInterpolate')
        g.link(broken, face_mix, 'A')
        g.link(cliff_grain, face_mix, 'B', 'RGB')
        g.link(cliff, face_mix, 'Alpha')
        neutral = g.node('Neutral sand grain', 'Desaturation')
        g.link(face_mix, neutral, 'Input')
        g.link(g.scalar('Desaturation', 0.84), neutral, 'Fraction')
        base = g.node('Warm sediment', 'Multiply')
        g.link(neutral, base, 'A')
        g.link(g.color('SandTint', (0.64, 0.39, 0.21)), base, 'B', 'RGB')
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
        # The four mask tints used to sit within about 30 degrees of each other on
        # the hue wheel, so the whole battlefield read as one orange. Spreading
        # them costs nothing - they are existing multipliers on an existing fetch:
        #   ash is now a cool grey at a much higher strength, and cool grey against
        #   warm sand is the single largest colour-composition win available here;
        #   sandstone loses saturation so it reads as rock rather than as more sand;
        #   mineral staining goes teal so the ore channel is a different hue from
        #   the ground it sits on instead of disappearing into it.
        color = tint_layer('Windblown dust', base, (0.50, 0.52, 0.56), 'B', 0.62)
        color = tint_layer('Exposed sandstone', color, (0.44, 0.21, 0.13), 'R', 0.93)
        color = tint_layer('Mineral staining', color, (0.30, 0.72, 0.66), 'G', 0.52)
        color = tint_layer('Packed service ground', color, (0.43, 0.37, 0.29), 'A', 0.72)

        # M4. Horizontal bedding from world Z. Zero fetches: the vertex already carries its
        # own height. This is what replaces the four geometric terraces that used to live in
        # HeightAt - at 126 quads the narrowest obstacle has about two vertex spacings of
        # sculptable half-width, so four terraces were sub-resolution on every obstacle in
        # the game and never reached the screen at all. Strata painted at 85 cm read at any
        # obstacle size because they cost no geometry.
        bed_phase = g.node('Cliff bedding at 85 cm', 'Multiply', const_b=1.0 / 85.0)
        g.link(cliff_wz, bed_phase, 'A')
        bed_wave = g.node('Cliff bedding wave', 'Sine')
        g.link(bed_phase, bed_wave, 'Input')
        bed_scaled = g.node('Cliff bedding amplitude', 'Multiply', const_b=0.34)
        g.link(bed_wave, bed_scaled, 'A')
        bed = g.node('Cliff bedding fraction', 'Add', const_b=0.62)
        g.link(bed_scaled, bed, 'A')
        strata = g.node('Cliff strata tint', 'LinearInterpolate')
        g.link(g.color('CanyonShadow', (0.178, 0.127, 0.099)), strata, 'A', 'RGB')
        g.link(g.color('CanyonSandstone', (0.315, 0.205, 0.140)), strata, 'B', 'RGB')
        g.link(bed, strata, 'Alpha')
        # THIS BOUND IS MANDATORY, and it is the same one the rock material uses. The sun is
        # a single key at yaw 45, so on every axis-aligned obstacle one face receives
        # N dot -L of exactly zero. Multiplying an absolute strata colour of 0.18-0.32 by a
        # raw grain value on a face that already has no key light lands it at black. Holding
        # the grain in [0.78, 1.20] keeps it a variation rather than a second darkener.
        grain_gain = g.node('Cliff grain gain', 'Multiply', const_b=0.72)
        g.link(neutral, grain_gain, 'A')
        grain_bias = g.node('Cliff grain bias', 'Add', const_b=0.64)
        g.link(grain_gain, grain_bias, 'A')
        bounded_grain = g.node('Bounded cliff grain', 'Clamp', min_default=0.78, max_default=1.20)
        g.link(grain_bias, bounded_grain, 'Input')
        face_rock = g.node('Cliff face rock', 'Multiply')
        g.link(strata, face_rock, 'A')
        g.link(bounded_grain, face_rock, 'B')
        layer_color = color
        color = g.node('Ground against cliff face', 'LinearInterpolate')
        g.link(layer_color, color, 'A')
        g.link(face_rock, color, 'B')
        g.link(cliff, color, 'Alpha')

        # Altitude shading. The landscape now carries real relief - rolling hills,
        # drainage troughs and damped pathways outside the obstacle cliffs - but
        # relief this shallow reaches the eye ONLY through the lambert term, and a
        # 15 degree hillside against a 46 degree key varies by a few percent. That
        # is why the first capture of the new heightfield still read as a plane.
        #
        # A landscape vertex's world Z IS its relief, so altitude costs no fetch,
        # no sampler and no extra interpolator: one mask, one lerp, one multiply on
        # a value the vertex shader already has. Physically it is the right cue as
        # well - low ground collects damp sediment and reads cooler and darker,
        # while crests are wind-scoured and sun-bleached - so hills read as hills
        # instead of as noise, and it holds up after the thermal ladder zeroes both
        # bloom and shadows, which is when the lambert term alone is all that is
        # left. The band is set over the walkable relief, so obstacle plateau tops
        # saturate at the bleached end, which is what a mesa cap should look like.
        altitude = g.node('Landscape world position', 'WorldPosition')
        altitude_z = g.node('Landscape altitude', 'ComponentMask',
                            r=False, g=False, b=True, a=False)
        g.link(altitude, altitude_z, 'Input')
        altitude_low = g.node('Trough floor altitude', 'Add', const_b=70.0)
        g.link(altitude_z, altitude_low, 'A')
        altitude_span = g.node('Walkable altitude fraction', 'Multiply', const_b=1.0 / 180.0)
        g.link(altitude_low, altitude_span, 'A')
        altitude_mask = g.node('Saturated altitude', 'Clamp', min_default=0.0, max_default=1.0)
        g.link(altitude_span, altitude_mask, 'Input')
        altitude_tint = g.node('Valley to crest tint', 'LinearInterpolate')
        g.link(g.color('ValleyShade', (0.84, 0.88, 0.95)), altitude_tint, 'A', 'RGB')
        # M5. Split the bleached crest into cap-versus-face. At the shipped camera pitch the
        # plateau top is about half of every obstacle's projected area and is the brightest
        # surface in the scene, so letting the face share the cap's multiplier is what makes
        # a sculpted mesa read as one flat blob. CliffFaceShade is a MULTIPLIER into this
        # slot, not an albedo: the runtime CrestShade is 1.24, so 0.72 is a 0.58x separation.
        crest_split = g.node('Cap against face', 'LinearInterpolate')
        g.link(g.color('CrestShade', (1.12, 1.07, 0.98)), crest_split, 'A', 'RGB')
        g.link(g.color('CliffFaceShade', (0.72, 0.68, 0.62)), crest_split, 'B', 'RGB')
        g.link(cliff, crest_split, 'Alpha')
        g.link(crest_split, altitude_tint, 'B')
        g.link(altitude_mask, altitude_tint, 'Alpha')
        layered_color = color
        color = g.node('Relief shaded ground', 'Multiply')
        g.link(layered_color, color, 'A')
        g.link(altitude_tint, color, 'B')
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
        g.normal(g.sample('GroundNormal', textures['Normal'], uv, 'NORMAL',
                          sampler_source=helper.WRAP_SAMPLER_GROUP), 0.78)
        roughness = g.node('Dry ground roughness', 'LinearInterpolate', const_b=0.96, const_alpha=0.70)
        g.link(g.sample('GroundRoughness', textures['Roughness'], uv, 'MASKS',
                        sampler_source=helper.WRAP_SAMPLER_GROUP), roughness, 'A', 'R')
        g.output(roughness, 'ROUGHNESS')
        # SPECULAR is deliberately left unconnected so the engine default of 0.5
        # applies. Forcing it to zero removed every grazing highlight from the
        # battlefield and left the ground reading as lambert construction paper
        # under the one shadow-casting light the budget allows. METALLIC stays 0.
        g.output(g.node('Nonmetal canyon ground', 'Constant', r=0.0), 'METALLIC')
        # The first ambient occlusion of any kind on iOS in this project: SSAO is
        # Mac-only, static lighting is off, and distance fields are off. It costs
        # zero new samplers because it reuses the TerrainLayers fetch already
        # taken above, so the count stays at six pixel plus one vertex. R is the
        # known-stone channel, so contact darkening only ever appears where
        # explored stone already appears - no unobserved state becomes visible.
        ao = g.node('Contact occlusion', 'LinearInterpolate', const_a=1.0, const_b=0.62)
        g.link(layers, ao, 'Alpha', 'R')
        g.output(ao, 'AMBIENT_OCCLUSION')
        report['material'] = g.finish()
        report['texture_samples'] = {'pixel': 7, 'vertex': 1}
        report['albedo_anti_tiling'] = {'second_octave_rotation_degrees': 37.0,
                                        'second_octave_tiling_ratio': 0.61803399,
                                        'combine': 'per-channel minimum', 'added_fetches': 0}
        report['ambient_occlusion'] = {'source': 'TerrainLayers R', 'floor': 0.62,
                                       'added_fetches': 0}
        report['specular'] = 'engine default 0.5; no longer driven to zero'
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
