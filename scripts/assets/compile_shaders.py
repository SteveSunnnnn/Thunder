#!/usr/bin/env python3
"""Compile every Thunder GLSL shader to validated SPIR-V.

Requires Vulkan SDK tools `glslc` and `spirv-val`. No network access is used.
"""
from __future__ import annotations
import argparse, pathlib, shutil, subprocess, sys

def need(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"required tool not found: {name}")
    return path

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="shaders")
    ap.add_argument("--out", default="build-shaders")
    ap.add_argument("--debug", action="store_true")
    ns = ap.parse_args()
    src = pathlib.Path(ns.src)
    out = pathlib.Path(ns.out)
    out.mkdir(parents=True, exist_ok=True)
    glslc, validator = need("glslc"), need("spirv-val")
    shaders = sorted(p for p in src.rglob("*") if p.suffix in {".vert", ".frag", ".comp"})
    if not shaders:
        raise SystemExit(f"no GLSL shaders found under {src}")
    for shader in shaders:
        rel = shader.relative_to(src)
        target = out / (str(rel).replace("/", "_") + ".spv")
        cmd = [glslc, str(shader), "-o", str(target), "-g" if ns.debug else "-O"]
        subprocess.run(cmd, check=True)
        subprocess.run([validator, str(target)], check=True)
        print(f"PASS {rel} -> {target}")
    print(f"shader_count={len(shaders)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
