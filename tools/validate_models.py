"""Validate shipped PGM1 geometry/material/texture data, and write an asset manifest."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def inspect(path):
    data = path.read_bytes()
    at = 0

    def take(n):
        nonlocal at
        if n < 0 or at + n > len(data):
            raise ValueError(f"{path.name}: truncated data at {at}")
        result = memoryview(data)[at:at + n]
        at += n
        return result

    def unpack(fmt):
        return struct.unpack(fmt, take(struct.calcsize(fmt)))

    magic, ns, nt, *bounds = unpack('<4sII6f')
    assert magic == b'PGM1' and 0 < ns <= 128 and nt <= 128
    assert all(math.isfinite(v) for v in bounds)
    assert all(bounds[i + 3] > bounds[i] for i in range(3))
    dimensions = []
    for _ in range(nt):
        w, h = unpack('<II')
        assert 0 < w <= 4096 and 0 < h <= 4096
        take(w * h * 4)
        dimensions.append([w, h])
    triangles = 0
    untextured = 0
    for _ in range(ns):
        ti, *color, nv = unpack('<i4fI')
        assert -1 <= ti < nt and nv > 0 and nv % 3 == 0
        assert all(math.isfinite(v) for v in color)
        untextured += ti == -1
        for row in struct.iter_unpack('<8f', take(nv * 32)):
            assert all(math.isfinite(v) for v in row)
            for i in range(3):
                assert bounds[i] - .001 <= row[i] <= bounds[i + 3] + .001
        triangles += nv // 3
    assert at == len(data), f"{path.name}: unexpected trailing bytes"
    return dict(file=path.name, bytes=len(data), triangles=triangles, submeshes=ns,
                textures=dimensions, untextured_submeshes=untextured,
                bounds=bounds, sha256=hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path)
    args = parser.parse_args()
    assets = [inspect(p) for p in sorted(args.folder.glob('*.pgm'))]
    assert len(assets) == 6, 'Expected all six character models'
    for a in assets:
        print(f"{a['file']}: {a['triangles']:,} triangles, {len(a['textures'])} textures, OK")
    (args.folder / 'manifest.json').write_text(json.dumps(assets, indent=2) + '\n', encoding='utf-8')
