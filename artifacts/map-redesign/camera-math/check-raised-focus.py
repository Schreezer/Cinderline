"""Standalone projection check for the six raised-home CameraBoundary cases."""
import math
from itertools import product

def dot(a, b): return sum(x*y for x, y in zip(a, b))
def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def mul(a, v): return tuple(x*v for x in a)
def clamp(x, lo, hi): return max(lo, min(hi, x))

pitch, yaw = math.radians(-54), math.radians(-45)
F = (math.cos(pitch)*math.cos(yaw), math.cos(pitch)*math.sin(yaw), math.sin(pitch))
R = (-math.sin(yaw), math.cos(yaw), 0)
U = (-math.sin(pitch)*math.cos(yaw), -math.sin(pitch)*math.sin(yaw), math.cos(pitch))
anchor = (720, 680, 179+92.5)
extent = (125, 125, 92.5)
corners = [tuple(x*y for x, y in zip(extent, s)) for s in product((-1, 1), repeat=3)]
tan_x = math.tan(math.radians(26))

for aspect in (1440/900, 1170/540):
    tan_y = tan_x/aspect
    rays = [add(F, add(mul(R, x*tan_x), mul(U, y*tan_y))) for x, y in product((-1, 1), repeat=2)]
    ground = [add(mul(F, -1), mul(ray, F[2]/ray[2])) for ray in rays]
    fmin = tuple(min(v[i] for v in ground) for i in range(2))
    fmax = tuple(max(v[i] for v in ground) for i in range(2))
    max_distance = min(3200, (4800-16)/max(sub(fmax, fmin))-anchor[2]/-F[2])
    min_distance = min(max_distance, max([520]+[abs(dot(c, a))/(t*.8)-dot(c, F) for c in corners for a, t in ((R, tan_x), (U, tan_y))])+.1)

    def bounds(d, full_border=False):
        hd = anchor[2]/-F[2]
        pd = d+hd
        shift = mul(F[:2], hd)
        span = mul(sub(fmax, fmin), pd)
        margin = 360 if full_border else clamp(min(span)*.15, 72, 360)
        return sub(sub((-margin, -margin), shift), mul(fmin, pd)), sub(sub((4800+margin,)*2, shift), mul(fmax, pd))

    def solve(d, full_border=False):
        def retry(): return None if full_border else solve(d, True)
        low, high = bounds(d, full_border)
        polygon = [(low[0], low[1]), (high[0], low[1]), (high[0], high[1]), (low[0], high[1])]
        for corner in corners:
            relative = sub(add(add(anchor, corner), mul(F, d)), (0, 0, anchor[2]))
            for sign in (-1, 1):
                for axis, tan in ((R, tan_x), (U, tan_y)):
                    n = sub(mul(axis, sign), mul(F, tan*.79999))
                    limit = dot(n, relative)
                    if not polygon: return retry()
                    clipped = []
                    previous = polygon[-1]
                    previous_side = dot(previous, n[:2])-limit
                    for current in polygon:
                        current_side = dot(current, n[:2])-limit
                        if (current_side >= 0) != (previous_side >= 0):
                            clipped.append(add(previous, mul(sub(current, previous), previous_side/(previous_side-current_side))))
                        if current_side >= 0: clipped.append(current)
                        previous, previous_side = current, current_side
                    polygon = clipped
        if not polygon: return retry()
        inside, best, best_dist = len(polygon) >= 3, None, math.inf
        for a, b in zip(polygon, polygon[1:]+polygon[:1]):
            edge = sub(b, a)
            delta = sub(anchor[:2], a)
            inside = inside and edge[0]*delta[1]-edge[1]*delta[0] >= -.000001
            t = clamp(dot(delta, edge)/max(dot(edge, edge), .000001), 0, 1)
            candidate = add(a, mul(edge, t))
            ds = dot(sub(candidate, anchor[:2]), sub(candidate, anchor[:2]))
            if ds < best_dist: best, best_dist = candidate, ds
        return anchor if inside else (*best, anchor[2])

    for requested in (520, 980, 3200):
        d = clamp(requested, min_distance, max_distance)
        position = solve(d)
        if position is None:
            low, high = min_distance, d
            if solve(low) is not None:
                for _ in range(18):
                    mid = (low+high)/2
                    if solve(mid) is not None: low = mid
                    else: high = mid
            d, position = low, solve(low)
        assert position is not None, (aspect, requested, min_distance, 'empty minimum region')
        eye = sub(position, mul(F, d))
        projections = []
        for c in corners:
            delta = sub(add(anchor, c), eye)
            depth = dot(delta, F)
            assert depth > 0
            projections.append((dot(delta, R)/(depth*tan_x), dot(delta, U)/(depth*tan_y)))
        ndc = max(abs(v) for p in projections for v in p)
        low, high = bounds(d, True)
        assert all(l-.001 <= p <= h+.001 for p, l, h in zip(position, low, high))
        tick_position = (*[clamp(p, l, h) for p, l, h in zip(position, low, high)], position[2])
        assert tick_position == position, 'rendered Tick must not undo the focus solution'
        assert ndc <= .8, (aspect, requested, ndc)
        assert 0 < position[0] < 1490 and 0 < position[1] < 1550, 'height remains on the raised main'
        print(f'PASS aspect={aspect:.6f} requested={requested} distance={d:.3f} pivot={position} max_ndc={ndc:.6f}')
