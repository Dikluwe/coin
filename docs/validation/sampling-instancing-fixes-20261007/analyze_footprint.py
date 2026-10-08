"""Independent double-precision formula for the authored 16-pixel projective quad.

Diagnostic models, not an accepted production contract or a replacement gate.
Use: python3 analyze_footprint.py > analytic-footprint.json
"""
import json
import math


def uv(x, y):
    s = .03 + .94 * (x - 24) / 16
    t = .02 + .96 * (40 - y) / 16
    q = 1 + .3 * s
    return s/q, t/q


def footprint(x, y):
    left, top = (x & ~1) + .5, (y & ~1) + .5
    a, b = uv(left, y+.5), uv(left+1, y+.5)
    c, d = uv(x+.5, top), uv(x+.5, top+1)
    gx = [(b[i]-a[i])*128 for i in range(2)]
    gy = [(d[i]-c[i])*128 for i in range(2)]
    euclidean = max(math.hypot(*gx), math.hypot(*gy))
    # Largest singular value, from the independently formed J J^T.
    aa, cc = gx[0]**2 + gy[0]**2, gx[1]**2 + gy[1]**2
    bb = gx[0]*gx[1] + gy[0]*gy[1]
    major = math.sqrt((aa+cc+math.hypot(aa-cc, 2*bb))/2)
    return math.log2(euclidean), math.log2(major)


def sample(u, v, lod, quantized):
    def level(l):
        if l >= 4:
            return 100 # 8x8 checker blocks average to 100 at mip 4 onward.
        size = 128 >> l
        def index(coord):
            texel = coord*size
            if quantized:
                texel = math.floor(texel*256+.5)/256
            return math.floor(texel) % size
        x, y = index(u), index(v)
        return 160 if ((x//(8 >> l) + y//(8 >> l)) & 1) else 40
    lo = math.floor(lod)
    value = level(lo) + (level(lo+1)-level(lo))*(lod-lo)
    return math.floor((.8*value + .2*.1*255)+.5)


rows = []
for x, y in [(33, y) for y in range(27, 37)] + [(35, 31)]:
    u, v = uv(x+.5, y+.5)
    lod, major_lod = footprint(x, y)
    boundaries = []
    for l in [math.floor(lod), math.floor(lod)+1]:
        size = 128 >> l
        for axis, coord in [('s', u), ('t', v)]:
            at = coord*size
            distance = abs(at-round(at))
            if distance < 1/512:
                boundaries.append(dict(mip=l, axis=axis, texel=at,
                                       distance_to_integer=distance,
                                       half_quantum_8bit=1/512))
    rows.append(dict(pixel=[x,y], uv=[u,v], lod_max_norm=lod, lod_major_singular=major_lod,
        ideal_red=sample(u,v,lod,False), round256_red=sample(u,v,lod,True),
        round256_svd_red=sample(u,v,major_lod,True), nearest_boundaries=boundaries))
print(json.dumps(rows, indent=2))
