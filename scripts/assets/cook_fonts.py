#!/usr/bin/env python3
"""Create a Thunder MSDF font package without bundling any font file in Thunder.

Requires ``msdf-atlas-gen`` and Pillow. The input font remains user-supplied.
Outputs ``<name>.thunderfont`` metrics plus an uncompressed
``<name>_atlas.thunderimg`` consumed directly by the Vulkan desktop backend.
When ``toktx`` is available a production KTX2 copy is emitted as well.
"""
from __future__ import annotations
import argparse, json, pathlib, shutil, struct, subprocess, tempfile
from PIL import Image

MAGIC=b"THUNDRF1"
VERSION=1
RECORD_BYTES=40

def fnv1a64(text: str) -> int:
    value=14695981039346656037
    for b in text.encode("utf-8"):
        value ^= b
        value = (value * 1099511628211) & 0xffffffffffffffff
    return value

def bounds(obj, key):
    b=obj.get(key) or {}
    return (float(b.get("left",0.0)), float(b.get("bottom",0.0)),
            float(b.get("right",0.0)), float(b.get("top",0.0)))

def fnv_bytes(parts: list[bytes]) -> int:
    value=14695981039346656037
    for part in parts:
        for b in part:
            value ^= b
            value=(value*1099511628211)&0xffffffffffffffff
    return value

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--atlas-key", required=True, help="AssetPack key for the KTX2 atlas")
    ap.add_argument("--charset", help="Optional msdf-atlas-gen charset file")
    ap.add_argument("--size", type=int, default=48)
    ap.add_argument("--px-range", type=float, default=4.0)
    ns=ap.parse_args()
    msdf=shutil.which("msdf-atlas-gen")
    if not msdf: raise SystemExit("required tool not found: msdf-atlas-gen")
    toktx=shutil.which("toktx")
    font=pathlib.Path(ns.font)
    if not font.is_file(): raise SystemExit(f"font not found: {font}")
    out=pathlib.Path(ns.out_dir);out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="thunder-font-") as tmp_raw:
        tmp=pathlib.Path(tmp_raw); png=tmp/"atlas.png"; meta=tmp/"atlas.json"
        cmd=[msdf,"-font",str(font),"-type","msdf","-format","png","-imageout",str(png),
             "-json",str(meta),"-size",str(ns.size),"-pxrange",str(ns.px_range)]
        if ns.charset: cmd += ["-charset",str(ns.charset)]
        else: cmd += ["-chars", "[0x20,0x7E]"]
        subprocess.run(cmd,check=True)
        data=json.loads(meta.read_text(encoding="utf-8"))
        atlas=data.get("atlas",{})
        width=int(atlas.get("width",0));height=int(atlas.get("height",0))
        if width<=0 or height<=0: raise SystemExit("msdf metadata missing atlas dimensions")
        glyphs=[]
        for g in data.get("glyphs",[]):
            cp=int(g.get("unicode",-1))
            if not 0<=cp<=0x10ffff: continue
            advance=float(g.get("advance",0.0))
            plane=bounds(g,"planeBounds"); atlas_bounds=bounds(g,"atlasBounds")
            glyphs.append((cp,advance,*plane,*atlas_bounds))
        glyphs.sort(key=lambda x:x[0])
        if not glyphs: raise SystemExit("msdf-atlas-gen produced no glyphs")
        runtime_atlas=out/f"{ns.name}_atlas.thunderimg"
        rgba=Image.open(png).convert("RGBA")
        if rgba.size != (width,height): raise SystemExit("MSDF PNG dimensions disagree with metadata")
        pixels=rgba.tobytes()
        with runtime_atlas.open("wb") as f:
            f.write(b"THUNDRIM")
            f.write(struct.pack("<IIQ",width,height,len(pixels)))
            f.write(pixels)
        atlas_out=None
        if toktx:
            atlas_out=out/f"{ns.name}_atlas.ktx2"
            subprocess.run([toktx,"--t2","--genmipmap","--encode","uastc",str(atlas_out),str(png)],check=True)
        texture_hash=fnv1a64(ns.atlas_key)
        # Match C++ FontAtlas::checksum: raw little-endian host bytes for integers/floats.
        checksum_parts=[struct.pack("<II f Q Q",width,height,ns.px_range,texture_hash,len(glyphs))]
        for glyph in glyphs: checksum_parts.append(struct.pack("<I9f",*glyph))
        checksum=fnv_bytes(checksum_parts)
        metrics_out=out/f"{ns.name}.thunderfont"
        with metrics_out.open("wb") as f:
            f.write(MAGIC);f.write(struct.pack("<IIII f Q Q I",VERSION,len(glyphs),width,height,
                                              ns.px_range,texture_hash,checksum,RECORD_BYTES))
            for glyph in glyphs:f.write(struct.pack("<I9f",*glyph))
        print(f"PASS glyphs={len(glyphs)} metrics={metrics_out} runtime_atlas={runtime_atlas} ktx2={atlas_out or 'skipped'}")
    return 0

if __name__=="__main__":raise SystemExit(main())
