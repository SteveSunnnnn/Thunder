"""单帧/多帧渲染体检：亮度统计 + 细线绝对检测 + 方向聚类 + ASCII 预览。

用法:
  python frame_review.py A.bmp [B.bmp ...]

系统 Python（有 numpy/scipy/PIL）:
  C:/Users/22546/AppData/Local/Programs/Python/Python311/python.exe
"""
import sys
import numpy as np
from PIL import Image
from scipy import ndimage


def luma(path):
    im = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    return 0.2126 * im[..., 0] + 0.7152 * im[..., 1] + 0.0722 * im[..., 2], im


def line_scan(y):
    """细线绝对检测：core = |2y - y^- - y^+| - |y^+ - y^-|，局部均值归一化。"""
    ym = ndimage.uniform_filter(y, 3)
    yp = np.roll(ym, -1, axis=1)
    ymn = np.roll(ym, 1, axis=1)
    core = np.abs(2 * ym - ymn - yp) - np.abs(yp - ymn)
    core = np.maximum(core, 0)
    core[:, :2] = 0
    core[:, -2:] = 0
    core[:2, :] = 0
    core[-2:, :] = 0
    bg = ndimage.uniform_filter(core, 31) + 1e-6
    norm = core / bg
    return norm


def direction_hist(mask, bins=36):
    """对二值掩膜做连通域，统计长条分量（span>=60, area/span<=3）的主方向。"""
    lab, n = ndimage.label(mask)
    if n == 0:
        return [], 0
    objs = ndimage.find_objects(lab)
    out = []
    long_thin = 0
    for i, sl in enumerate(objs):
        if sl is None:
            continue
        sub = lab[sl] == (i + 1)
        h, w = sub.shape
        span = max(h, w)
        area = int(sub.sum())
        if span < 60 or area / max(span, 1) > 3.0:
            continue
        long_thin += 1
        ys, xs = np.nonzero(sub)
        if len(xs) < 8:
            continue
        # PCA 主方向
        cx, cy = xs.mean(), ys.mean()
        cov = np.cov(np.vstack([xs - cx, ys - cy]))
        ev, evec = np.linalg.eigh(cov)
        ang = np.degrees(np.arctan2(evec[0, -1], evec[1, -1]))
        out.append((ang % 90.0, span, area))
    return out, long_thin


def ascii_preview(y, cols=76, rows=26):
    h, w = y.shape
    ys = np.linspace(0, h - 1, rows).astype(int)
    xs = np.linspace(0, w - 1, cols).astype(int)
    small = y[np.ix_(ys, xs)]
    lo, hi = np.percentile(small, 2), np.percentile(small, 98)
    if hi - lo < 1e-6:
        hi = lo + 1
    q = np.clip((small - lo) / (hi - lo), 0, 1)
    ramp = " .:-=+*#%@"
    return "\n".join("".join(ramp[int(v * (len(ramp) - 1))] for v in row) for row in q)


def main(paths):
    for p in paths:
        y, rgb = luma(p)
        print("=" * 78)
        print(f"FILE {p}   {y.shape[1]}x{y.shape[0]}")
        print(f"  luma  mean={y.mean():7.3f}  std={y.std():7.3f}  min={y.min():6.1f}  max={y.max():6.1f}")
        print(f"  RGB   mean R={rgb[...,0].mean():6.2f} G={rgb[...,1].mean():6.2f} B={rgb[...,2].mean():6.2f}")
        print(f"  sat   mean={ (rgb.max(2)-rgb.min(2)).mean():6.2f}   dark px(<8)={ (y<8).mean()*100:5.2f}%  blown(>250)={ (y>250).mean()*100:5.2f}%")
        norm = line_scan(y)
        thr = np.percentile(norm, 99.9)
        mask = norm > max(thr, 4.0)
        comps, nlt = direction_hist(mask)
        print(f"  lines mask={mask.mean()*100:5.3f}%  long-thin comps={nlt}")
        if comps:
            angs = np.array([c[0] for c in comps])
            for a, s, ar in sorted(comps, key=lambda c: -c[1])[:8]:
                print(f"     ang={a:5.1f}deg(mod90) span={s:4d} area={ar:5d}")
            # 晶格轴预测 0 / 20.6 / 36.9 / 57.5 / 73.7
            for axis in (0.0, 20.6, 36.9, 57.5, 73.7):
                near = np.minimum(np.abs(angs - axis), 90 - np.abs(angs - axis))
                print(f"     near axis {axis:5.1f}: {(near < 6).sum()}")
        print(ascii_preview(y))


if __name__ == "__main__":
    main(sys.argv[1:])
