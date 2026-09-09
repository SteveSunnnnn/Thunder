"""帧诊断：扫描线剖面 + 2D FFT 周期性 + 分块统计，用于判断'平坦/空白/规则纹理'。"""
import sys
import numpy as np
from PIL import Image


def main(path):
    im = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    y = 0.2126 * im[..., 0] + 0.7152 * im[..., 1] + 0.0722 * im[..., 2]
    h, w = y.shape
    print(f"FILE {path}  {w}x{h}")
    print(f"  luma mean={y.mean():.2f} std={y.std():.2f} p1={np.percentile(y,1):.1f} "
          f"p50={np.percentile(y,50):.1f} p99={np.percentile(y,99):.1f}")

    # 扫描线剖面
    for row in (h // 4, h // 2, 3 * h // 4):
        line = y[row]
        sl = line[:: max(1, w // 40)]
        print(f"  row{row:5d}: " + " ".join(f"{v:5.1f}" for v in sl))

    # 分块统计（8x8 网格）看是否有大片死区
    print("  block std (8x8):")
    for r in range(8):
        ys = slice(r * h // 8, (r + 1) * h // 8)
        vals = []
        for c in range(8):
            xs = slice(c * w // 8, (c + 1) * w // 8)
            vals.append(y[ys, xs].std())
        print("    " + " ".join(f"{v:6.2f}" for v in vals))

    # FFT 周期性（去均值后）
    f = np.abs(np.fft.fftshift(np.fft.fft2(y - y.mean())))
    f[0, 0] = 0
    cy, cx = h // 2, w // 2
    idx = np.argsort(f.ravel())[::-1][:8]
    print("  top FFT peaks (period_px, angle_deg, power%):")
    tot = f.sum()
    for i in idx:
        py, px = np.unravel_index(i, f.shape)
        dy, dx = py - cy, px - cx
        if dy == 0 and dx == 0:
            continue
        period = 1.0 / (np.hypot(dy / h, dx / w) + 1e-9)
        ang = np.degrees(np.arctan2(dy, dx)) % 180
        print(f"    period={period:8.2f}px  ang={ang:6.1f}  power={100*f[py,px]/tot:5.2f}%")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
        print()
