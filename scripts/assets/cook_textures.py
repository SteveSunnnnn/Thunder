#!/usr/bin/env python3
"""Cook source images into mipmapped KTX2 textures.

This is an orchestration layer around Khronos KTX-Software `toktx`. It intentionally
contains no image decoder in Thunder itself: texture transcoding is an offline content task.
"""
from __future__ import annotations
import argparse, pathlib, shutil, subprocess

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=("uastc", "etc1s"), default="uastc")
    ap.add_argument("--normal-map", action="store_true")
    ns = ap.parse_args()
    toktx = shutil.which("toktx")
    if not toktx:
        raise SystemExit("required tool not found: toktx (KTX-Software)")
    out_dir = pathlib.Path(ns.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    for raw in ns.inputs:
        src = pathlib.Path(raw)
        if not src.is_file():
            raise SystemExit(f"texture input not found: {src}")
        dst = out_dir / f"{src.stem}.ktx2"
        cmd = [toktx, "--t2", "--genmipmap", "--encode", ns.mode]
        if ns.normal_map:
            cmd += ["--normal-mode"]
        cmd += [str(dst), str(src)]
        subprocess.run(cmd, check=True)
        print(f"PASS {src} -> {dst}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
