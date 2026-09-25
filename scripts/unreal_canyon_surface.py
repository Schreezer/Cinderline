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
    helper.VERSION = 'canyon-ground-v5.2-explored-paving'
    helper.MATERIALS = '/Game/Art/Canyon/Materials'
    helper.TEXTURES = '/Game/Art/Canyon/Textures'
    report = {'success': False, 'textures': [], 'source_sha256': helper.sha256(__file__)}
    target = ROOT / 'artifacts/canyon-hud/ground-import.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        architecture_spec = importlib.util.spec_from_file_location('cinder_ground_paving', ROOT / 'scripts/unreal_map_architecture.py')
        architecture = importlib.util.module_from_spec(architecture_spec)
        architecture_spec.loader.exec_module(architecture)
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
        coarse_ground_path = '/Game/Art/VisualTarget/Materials/T_VT_DryGroundRocks_Color'
        coarse_ground = unreal.load_asset(coarse_ground_path)
        has_coarse_ground = isinstance(coarse_ground, unreal.Texture2D)
        if not has_coarse_ground:
            coarse_ground = textures['Color']
        g = helper.Graph('M_CinderCanyonGround')
        uv = g.world_uv(420.0)
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
        # A restrained coarse octave survives the game-camera mip level, with
        # more chips near known rock. Missing optional artwork falls back to
        # the original sand; the fetch count stays unchanged.
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
        detail = g.sample('GroundDetail', coarse_ground, detail_uv, 'COLOR',
                          sampler_source=helper.WRAP_SAMPLER_GROUP)
        route_smoothing = g.node('Worn service route smoothing', 'Multiply', const_b=0.82)
        g.link(layers, route_smoothing, 'A', 'A')
        quiet_routes = g.node('Outside worn service routes', 'OneMinus')
        g.link(route_smoothing, quiet_routes, 'Input')
        toe_gain = g.node('Cliff toe coarse ground gain', 'Multiply', const_b=0.50)
        g.link(layers, toe_gain, 'A', 'R')
        coarse_strength = g.node('Natural coarse ground strength', 'Add', const_b=0.22)
        g.link(toe_gain, coarse_strength, 'A')
        coarse_weight = g.node('Untraveled coarse ground weight', 'Multiply')
        g.link(coarse_strength, coarse_weight, 'A')
        g.link(quiet_routes, coarse_weight, 'B')
        broken = g.node('Worn routes against rocky soil', 'LinearInterpolate')
        g.link(grain, broken, 'A', 'RGB')
        g.link(detail, broken, 'B', 'RGB')
        g.link(coarse_weight, broken, 'Alpha')
        ground_neutral = g.node('Neutral soil grain', 'Desaturation')
        g.link(broken, ground_neutral, 'Input')
        g.link(g.scalar('Desaturation', 0.93), ground_neutral, 'Fraction')
        # The source fine albedo averages 0.20 linear. Keep that calibration and
        # enough contrast to survive mipping at the actual gameplay camera.
        # Bounds suppress isolated black crevices and white pebbles; slight
        # coarse texture remains even on worn paths so they do not look painted.
        ground_gain = g.node('Quiet soil grain gain', 'Multiply', const_b=0.85)
        g.link(ground_neutral, ground_gain, 'A')
        ground_bias = g.node('Quiet soil grain center', 'Add', const_b=0.03)
        g.link(ground_gain, ground_bias, 'A')
        neutral = g.node('Bounded soil grain', 'Clamp', min_default=0.10, max_default=0.33)
        g.link(ground_bias, neutral, 'Input')
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
        side_uv = g.node('Cliff side projection at 360 cm', 'Multiply', const_b=1.0 / 360.0)
        g.link(side_xy, side_uv, 'A')
        cliff_grain = g.sample('CliffGrain', coarse_ground, side_uv, 'COLOR',
                               sampler_source=helper.WRAP_SAMPLER_GROUP)
        # Soil and side rock keep separate albedo samples; mixing their UVs
        # would drag texture coordinates across the transition and make streaks.
        base = g.node('Muted olive soil', 'Multiply')
        g.link(neutral, base, 'A')
        g.link(g.color('SandTint', (0.21, 0.26, 0.13)), base, 'B', 'RGB')
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
        # Closely related earth tints give paths a worn center without white
        # ribbons. Cool mineral staining stays subordinate to the actual ore.
        color = tint_layer('Windblown dust', base, (0.34, 0.29, 0.17), 'B', 0.45)
        color = tint_layer('Exposed sandstone', color, (0.31, 0.29, 0.19), 'R', 0.80)
        color = tint_layer('Mineral staining', color, (0.20, 0.28, 0.29), 'G', 0.36)
        color = tint_layer('Packed service ground', color, (0.34, 0.30, 0.21), 'A', 0.68)

        # The mesh kit supplies real fractures and ledges. Keep remaining
        # Landscape faces in the same warm slate family, with restrained texture
        # variation and no procedural bedding, drawn joints or bright cap rim.
        side_neutral = g.node('Neutral slate grain', 'Desaturation')
        g.link(cliff_grain, side_neutral, 'Input', 'RGB')
        g.link(g.scalar('SlateDesaturation', 1.0), side_neutral, 'Fraction')
        shadow_tint = g.color('CanyonShadow', (0.065, 0.082, 0.078))
        sandstone_tint = g.color('CanyonSandstone', (0.135, 0.15, 0.12))
        slate_tint = g.node('Warm slate face tint', 'LinearInterpolate', const_alpha=0.68)
        g.link(shadow_tint, slate_tint, 'A', 'RGB')
        g.link(sandstone_tint, slate_tint, 'B', 'RGB')
        grain_gain = g.node('Slate grain gain', 'Multiply', const_b=0.40)
        g.link(side_neutral, grain_gain, 'A')
        grain_bias = g.node('Slate grain center', 'Add', const_b=0.82)
        g.link(grain_gain, grain_bias, 'A')
        bounded_grain = g.node('Bounded slate grain', 'Clamp', min_default=0.84, max_default=1.08)
        g.link(grain_bias, bounded_grain, 'Input')
        face_rock = g.node('Textured slate face', 'Multiply')
        g.link(slate_tint, face_rock, 'A')
        g.link(bounded_grain, face_rock, 'B')
        layer_color = color
        color = g.node('Ground against cliff face', 'LinearInterpolate')
        g.link(layer_color, color, 'A')
        g.link(face_rock, color, 'B')
        g.link(cliff, color, 'Alpha')

        # Shallow relief retains a small cool-to-warm shift. Multipliers remain
        # near unity so crests and troughs do not become painted light and shade.
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
        g.link(g.color('ValleyShade', (0.82, 0.90, 0.80)), altitude_tint, 'A', 'RGB')
        # Steep faces share the prop palette; avoid double-darkening the side.
        crest_split = g.node('Cap against face', 'LinearInterpolate')
        g.link(g.color('CrestShade', (1.08, 1.06, 0.97)), crest_split, 'A', 'RGB')
        g.link(g.color('CliffFaceShade', (0.98, 0.99, 0.97)), crest_split, 'B', 'RGB')
        g.link(cliff, crest_split, 'Alpha')
        g.link(crest_split, altitude_tint, 'B')
        g.link(altitude_mask, altitude_tint, 'Alpha')
        layered_color = color
        color = g.node('Relief shaded ground', 'Multiply')
        g.link(layered_color, color, 'A')
        g.link(altitude_tint, color, 'B')
        # The authored map reserves the top of A for constructed pavement.
        # Old maps keep AuthoredMap=0, including service masks that reach A=1.
        # All paving detail is arithmetic and reuses the existing soil grain;
        # the 7 pixel / 1 vertex sample budget is unchanged.
        paving_fraction = architecture.smoothstep(g, 'Authored paving mask', layers, 0.72, 0.90, 'A')
        authored_map = g.node('Bounded authored map enable', 'Clamp', min_default=0.0, max_default=1.0)
        g.link(g.scalar('AuthoredMap', 0.0), authored_map, 'Input')
        enabled_paving = g.node('Authored map paving only', 'Multiply')
        g.link(paving_fraction, enabled_paving, 'A')
        g.link(authored_map, enabled_paving, 'B')
        outside_cliff = g.node('Paving outside steep rock', 'OneMinus')
        g.link(cliff, outside_cliff, 'Input')
        paving_mask = g.node('Walkable authored paving', 'Multiply')
        g.link(enabled_paving, paving_mask, 'A')
        g.link(outside_cliff, paving_mask, 'B')
        paving_xy = g.node('Paving metric world XY', 'ComponentMask', r=True, g=True, b=False, a=False)
        g.link(altitude, paving_xy, 'Input')
        paving_uv = g.node('Paving slabs 120 by 90 cm', 'Multiply')
        g.link(paving_xy, paving_uv, 'A')
        g.link(g.node('Paving reciprocal slab dimensions', 'Constant2Vector', r=1.0 / 120.0, g=1.0 / 90.0), paving_uv, 'B')
        pavement, pavement_joint, _ = architecture.slab_pattern(
            g, 'Paving', paving_uv, g.color('PavingTint', (0.078, 0.082, 0.071)), neutral)
        paved_color = g.node('Earth against constructed pavement', 'LinearInterpolate')
        g.link(color, paved_color, 'A')
        g.link(pavement, paved_color, 'B')
        g.link(paving_mask, paved_color, 'Alpha')
        color = paved_color
        fog_sample = g.sample('FogMask', fog, world_uv, 'LINEAR_COLOR')
        visible = architecture.remembered_surface_fraction(g, 'Authored paving', fog_sample, paving_mask)
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
        # XY normal detail is valid on ground/caps, but its tangent basis does
        # not follow a vertical side projection. Neutralize it on known steep
        # faces so their geometry is not obscured by stretched normal-map ribs.
        # Keep NormalStrength and the existing single sample for runtime compatibility.
        ground_normal = g.sample('GroundNormal', textures['Normal'], uv, 'NORMAL',
                                 sampler_source=helper.WRAP_SAMPLER_GROUP)
        flat_normal = g.node('Flat tangent normal', 'Constant3Vector',
                             constant=unreal.LinearColor(0, 0, 1, 0))
        normal_ground_weight = g.node('Ground normal outside cliff faces', 'OneMinus')
        g.link(cliff, normal_ground_weight, 'Input')
        normal_strength = g.node('Slope gated normal strength', 'Multiply')
        g.link(g.scalar('NormalStrength', 0.40), normal_strength, 'A')
        g.link(normal_ground_weight, normal_strength, 'B')
        pavement_normal_weight = g.node('Soil normals outside constructed pavement', 'OneMinus')
        g.link(paving_mask, pavement_normal_weight, 'Input')
        surface_normal_strength = g.node('Natural surface normal strength', 'Multiply')
        g.link(normal_strength, surface_normal_strength, 'A')
        g.link(pavement_normal_weight, surface_normal_strength, 'B')
        normal_mix = g.node('Restrained normal detail', 'LinearInterpolate')
        g.link(flat_normal, normal_mix, 'A')
        g.link(ground_normal, normal_mix, 'B', 'RGB')
        g.link(surface_normal_strength, normal_mix, 'Alpha')
        normalized_normal = g.node('Normalized tangent normal', 'Normalize')
        g.link(normal_mix, normalized_normal, 'VectorInput')
        g.output(normalized_normal, 'NORMAL')
        roughness = g.node('Dry ground roughness', 'LinearInterpolate', const_b=0.96, const_alpha=0.70)
        g.link(g.sample('GroundRoughness', textures['Roughness'], uv, 'MASKS',
                        sampler_source=helper.WRAP_SAMPLER_GROUP), roughness, 'A', 'R')
        pavement_roughness = g.node('Concrete and dry recessed joint roughness', 'LinearInterpolate', const_a=0.87, const_b=0.96)
        g.link(pavement_joint, pavement_roughness, 'Alpha')
        surface_roughness = g.node('Ground and pavement roughness', 'LinearInterpolate')
        g.link(roughness, surface_roughness, 'A')
        g.link(pavement_roughness, surface_roughness, 'B')
        g.link(paving_mask, surface_roughness, 'Alpha')
        g.output(surface_roughness, 'ROUGHNESS')
        # SPECULAR is deliberately left unconnected so the engine default of 0.5
        # applies. Forcing it to zero removed every grazing highlight from the
        # battlefield and left the ground reading as lambert construction paper
        # under the one shadow-casting light the budget allows. METALLIC stays 0.
        g.output(g.node('Nonmetal canyon ground', 'Constant', r=0.0), 'METALLIC')
        # The first ambient occlusion of any kind on iOS in this project: SSAO is
        # Mac-only, static lighting is off, and distance fields are off. It costs
        # zero new samplers because it reuses the TerrainLayers fetch already
        # taken above, so the count stays at seven pixel plus one vertex. R is the
        # known-stone channel, so contact darkening only ever appears where
        # explored stone already appears - no unobserved state becomes visible.
        ao = g.node('Contact occlusion', 'LinearInterpolate', const_a=1.0, const_b=0.80)
        g.link(layers, ao, 'Alpha', 'R')
        g.output(ao, 'AMBIENT_OCCLUSION')
        report['material'] = g.finish()
        report['texture_samples'] = {'pixel': 7, 'vertex': 1}
        report['authored_paving'] = {'enable_parameter': 'AuthoredMap', 'default': 0.0,
                                    'mask': 'AuthoredMap * smoothstep(0.72, 0.90, TerrainLayers A) * (1 - cliff)',
                                    'slab_size_cm': [120, 90], 'tint_linear_rgb': [0.078, 0.082, 0.071],
                                    'weathering_parameters': {'PanelVariation': 0.24, 'EdgeWear': 0.22, 'CrackStrength': 0.24},
                                    'roughness_bounds': [0.87, 0.96], 'normal_detail_on_full_paving': 0.0,
                                    'added_texture_fetches': 0, 'legacy_maps': 'AuthoredMap=0 preserves their existing service masks'}
        report['albedo_anti_tiling'] = {'base_tile_width_cm': 420.0,
                                        'second_octave_rotation_degrees': 37.0,
                                        'second_octave_tiling_ratio': 0.61803399,
                                        'combine': 'linear fine/coarse blend, desaturation, compressed bounded soil grain',
                                        'coarse_albedo': coarse_ground.get_path_name(),
                                        'coarse_albedo_fallback': not has_coarse_ground,
                                        'soil_grain': 'clamp(0.03 + 0.85 * desaturated blended albedo, 0.10, 0.33)',
                                        'soil_grain_bounds': [0.10, 0.33],
                                        'coarse_weight': '(0.22 + 0.50 * known stone R) * (1 - 0.82 * service A)',
                                        'added_fetches': 0}
        report['cliff_surface'] = {
            'style': 'restrained warm slate; fractures and ledges supplied by mesh geometry',
            'procedural_bedding': False,
            'procedural_vertical_joints': False,
            'bright_cap_shoulder': False,
            'face_tint': 'lerp(CanyonShadow, CanyonSandstone, 0.68)',
            'grain': 'clamp(0.82 + 0.40 * desaturated side albedo, 0.84, 1.08)',
            'grain_bounds': [0.84, 1.08],
            'mask': 'known-stone R times slope; full fog gate remains downstream',
            'walkable_normal_z_floor': 0.965,
            'side_albedo': coarse_ground.get_path_name(),
            'side_albedo_tile_cm': 360.0,
            'normal_detail': 'NormalStrength * (1 - known cliff face); neutral tangent normal on full face',
            'ground_normal_strength_default': 0.40,
            'added_fetches': 0,
            'geometry_displacement': False,
        }
        report['layer_tint_strengths'] = {'dust': 0.45, 'stone': 0.80, 'mineral': 0.36, 'service': 0.68}
        report['ambient_occlusion'] = {'source': 'TerrainLayers R', 'floor': 0.80,
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
