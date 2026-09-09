#!/usr/bin/env python3
"""Convert the P0 grass material pack PNGs into raw RGB8 files for the
Thunder grass showcase executable.

Usage: python tools/convert_grass_textures.py <pack_dir> <out_dir>
Expects <pack_dir>/textures/tex_ground_temperate_grass_{albedo,normal,orm}.png
and writes grass_albedo.raw / grass_normal.raw / grass_orm.raw (1024x1024
RGB8, row-major, top-left origin).
"""
import sys
from pathlib import Path

from PIL import Image

SOURCES = {
    "grass_albedo.raw": "tex_ground_temperate_grass_albedo.png",
    "grass_normal.raw": "tex_ground_temperate_grass_normal.png",
    "grass_orm.raw": "tex_ground_temperate_grass_orm.png",
}


def main(pack_dir: Path, out_dir: Path) -> int:
    textures = pack_dir / "textures"
    out_dir.mkdir(parents=True, exist_ok=True)
    for out_name, source_name in SOURCES.items():
        image = Image.open(textures / source_name).convert("RGB")
        if image.size != (1024, 1024):
            raise SystemExit(f"{source_name}: expected 1024x1024, got {image.size}")
        (out_dir / out_name).write_bytes(image.tobytes())
        print(f"wrote {out_dir / out_name}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    raise SystemExit(main(Path(sys.argv[1]), Path(sys.argv[2])))
