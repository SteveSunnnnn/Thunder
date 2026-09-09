"""Subpixel shoreline seeds and bounded, deterministic JFA+1 propagation.

The supplied mask includes the page halo. Nearest sites are located halfway
between unlike raster samples; all-land/all-water tiles saturate explicitly.
Distances are metres and positive on land. Raster resolution still limits the
shoreline: JFA is not a substitute for a high-resolution vector source.
"""
import numpy as np


def coast_distance_jfa(mask, pixel_m, maximum_m=16383.5):
    land = np.asarray(mask, dtype=bool)
    if land.ndim != 2 or not land.size or pixel_m <= 0 or not np.isfinite(pixel_m):
        raise ValueError("JFA requires a nonempty 2D mask and positive finite resolution")
    if land.all() or not land.any():
        return np.full(land.shape, maximum_m if land.all() else -maximum_m, dtype=np.float64)
    h, w = land.shape
    yy, xx = np.indices((h, w), dtype=np.float64)
    sx = np.full((h, w), np.inf)
    sy = np.full((h, w), np.inf)
    best = np.full((h, w), np.inf)

    def seed(y, x, site_y, site_x):
        distance = (yy[y, x] - site_y)**2 + (xx[y, x] - site_x)**2
        improve = distance < best[y, x]
        sx[y, x] = np.where(improve, site_x, sx[y, x])
        sy[y, x] = np.where(improve, site_y, sy[y, x])
        best[y, x] = np.minimum(best[y, x], distance)

    y, x = np.nonzero(land[:, 1:] != land[:, :-1])
    seed(y, x, y, x + 0.5)
    seed(y, x + 1, y, x + 0.5)
    y, x = np.nonzero(land[1:, :] != land[:-1, :])
    seed(y, x, y + 0.5, x)
    seed(y + 1, x, y + 0.5, x)
    step = 1 << (max(h, w) - 1).bit_length()
    steps = []
    while step >= 1:
        steps.append(step)
        step //= 2
    steps.append(1)  # JFA+1 local refinement
    for step in steps:
        old_x, old_y = sx.copy(), sy.copy()
        for dy in (-step, 0, step):
            for dx in (-step, 0, step):
                if (dx == 0 and dy == 0) or abs(dy) >= h or abs(dx) >= w:
                    continue
                dest = (slice(max(0, -dy), min(h, h - dy)), slice(max(0, -dx), min(w, w - dx)))
                src = (slice(max(0, dy), min(h, h + dy)), slice(max(0, dx), min(w, w + dx)))
                cx, cy = old_x[src], old_y[src]
                distance = (xx[dest] - cx)**2 + (yy[dest] - cy)**2
                improve = distance < best[dest]
                sx[dest] = np.where(improve, cx, sx[dest])
                sy[dest] = np.where(improve, cy, sy[dest])
                best[dest] = np.minimum(best[dest], distance)
    distance = np.minimum(np.sqrt(best) * pixel_m, maximum_m)
    return np.where(land, distance, -distance)
