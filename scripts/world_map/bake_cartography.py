"""Bake long-range printed-chart shore distance from the public-GIS raster.

The optional 128x128 int16 layer uses 32 m/unit (about 1048 km range), while
the physical coast bundle remains at 0.5 m/unit. This layer affects ink only.
EDT is evaluated on the full wrapped coverage so page borders cannot seed it.
"""
import argparse
import json
import math
import os
import struct
from pathlib import Path

import numpy as np
from scipy.ndimage import distance_transform_edt


def signed_chart_distance(mask, pixel_m, wrap=True):
    if mask.all() or not mask.any():
        return np.full(mask.shape, 32767 if mask.all() else -32767, dtype='<i2')
    margin = min(mask.shape[1], int(math.ceil(1048544.0 / pixel_m)) + 2)
    extended = np.pad(mask, ((0, 0), (margin, margin)), mode='wrap' if wrap else 'edge')
    extended = np.pad(extended, ((margin, margin), (0, 0)), mode='edge')
    inside = distance_transform_edt(extended)
    outside = distance_transform_edt(~extended)
    # Nearest unlike samples are one pixel apart; coast is halfway between.
    distance = np.where(extended, inside - 0.5, -(outside - 0.5)) * pixel_m
    distance = distance[margin:margin + mask.shape[0], margin:margin + mask.shape[1]]
    return np.clip(np.rint(distance / 32.0), -32767, 32767).astype('<i2')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--chunks', type=Path, required=True)
    args = parser.parse_args()
    root = args.chunks.resolve()
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    lines = (root / 'manifest.txt').read_text(encoding='utf-8').splitlines()
    page_paths = {}
    province_path = None
    for line in lines:
        fields = line.split(maxsplit=5)
        if len(fields) != 6:
            continue
        if fields[0] == 'province_coast':
            page_paths[tuple(map(int, fields[1:4]))] = root / fields[5]
        elif fields[0] == 'province_definitions':
            province_path = root / fields[5]
    if province_path is None:
        raise ValueError('missing province definitions')
    payload = province_path.read_bytes()
    magic, count = struct.unpack_from('<II', payload)
    if magic != 0x32565250:
        raise ValueError('expected PRV2')
    is_land = np.zeros(count + 1, dtype=bool)
    offset = 8
    for i in range(count):
        n, = struct.unpack_from('<H', payload, offset)
        offset += 2 + n
        is_land[i + 1] = payload[offset + 32] == 0
        offset += 36
    if offset != len(payload):
        raise ValueError('malformed PRV2')
    emitted = []
    out = root / 'cartography'
    out.mkdir(exist_ok=True)
    world = metadata['bounds_world_m']
    for level in range(metadata['clip_levels']):
        divisor = 1 << level
        nx = math.ceil(metadata['base_page_count_x'] / divisor)
        ny = math.ceil(metadata['base_page_count_y'] / divisor)
        pixel_m = metadata['base_page_world_size_m'] * divisor / 128
        width = min(nx * 128, math.ceil((world[2] - world[0]) / pixel_m))
        height = min(ny * 128, math.ceil((world[3] - world[1]) / pixel_m))
        north_pad = ny * 128 - height
        mask = np.zeros((ny * 128, nx * 128), dtype=bool)
        for y in range(ny):
            for x in range(nx):
                codes = np.fromfile(page_paths[level, x, y], dtype='<u2', count=128 * 128).reshape(128, 128)
                mask[(ny - 1 - y)*128:(ny - y)*128, x*128:(x+1)*128] = is_land[codes]
        distance = signed_chart_distance(mask[north_pad:, :width], pixel_m, metadata['horizontal_wrap'])
        expanded = np.pad(distance, ((north_pad, 0), (0, 0)), mode='edge')
        expanded = np.pad(expanded, ((0, 0), (0, nx * 128 - width)), mode='wrap')
        for y in range(ny):
            for x in range(nx):
                path = out / f'l{level}_{x}_{y}.bin'
                expanded[(ny-1-y)*128:(ny-y)*128, x*128:(x+1)*128].astype('<i2').tofile(path)
                emitted.append(f'cartographic_coast {level} {x} {y} 0 {path.relative_to(root).as_posix()}')
        print(f'cartographic coast level={level} pages={nx*ny}', flush=True)
    lines = [line for line in lines if not line.startswith('cartographic_coast ')] + emitted
    (root / 'manifest.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    metadata['cartographic_coast_step_m'] = 32
    (root / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
