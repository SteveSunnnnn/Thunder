"""Acquire public reference data and freeze byte-level provenance for offline builds.

This is the only network step. Existing lock files are verified, never refreshed
silently. No historical ownership is inferred from this modern reference set.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import urllib.request
import subprocess
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NE_LICENSE = "https://www.naturalearthdata.com/about/terms-of-use/"
THEMES = {
    "admin_0_countries": ("cultural", "admin_0_countries"),
    "admin_1_states_provinces": ("cultural", "admin_1_states_provinces"),
    "populated_places": ("cultural", "populated_places"),
    "roads": ("cultural", "roads"),
    "railroads": ("cultural", "railroads"),
    "ocean": ("physical", "ocean"),
    "land": ("physical", "land"),
    "lakes": ("physical", "lakes"),
    "rivers": ("physical", "rivers_lake_centerlines"),
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch_one(spec, directory):
    path = directory / spec["file"]
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        partial = path.with_suffix(path.suffix + ".partial")
        request = urllib.request.Request(spec["url"], headers={"User-Agent": "ThunderWorldCompiler/1.0"})
        try:
            with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as output:
                while block := response.read(1024 * 1024):
                    output.write(block)
        except OSError:
            # Windows curl uses the system TLS/proxy stack. Keep certificate
            # verification on; this fallback never downgrades to HTTP.
            subprocess.run(["curl.exe", "--fail", "--location", "--silent", "--show-error",
                            "--retry", "2", "--max-time", "120", "--output", str(partial),
                            spec["url"]], check=True)
        partial.replace(path)
    actual = digest(path)
    if spec.get("sha256") and spec["sha256"] != actual:
        raise ValueError(f"locked source checksum mismatch: {path.name}")
    return {**spec, "sha256": actual, "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=ROOT / "data/geography")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    lock_path = args.out / "sources.lock.json"
    if lock_path.exists():
        catalog = json.loads(lock_path.read_text(encoding="utf-8"))
        specs = catalog["sources"]
    else:
        specs = [{"id": f"natural_earth_{key}_10m", "file": f"raw/ne_10m_{theme}.zip",
                  "url": f"https://naciscdn.org/naturalearth/10m/{kind}/ne_10m_{theme}.zip",
                  "license": "public-domain", "license_url": NE_LICENSE,
                  "role": "modern_reference", "historical_year": None}
                 for key, (kind, theme) in THEMES.items()]
        specs.append({"id": "gebco_earth_relief_06m", "file": "raw/earth_gebco_06m_p.grd",
                      "url": "https://oceania.generic-mapping-tools.org/server/earth/earth_gebco/earth_gebco_06m_p.grd",
                      "license": "public-domain-with-attribution",
                      "license_url": "https://www.gebco.net/data-products/gridded-bathymetry/terms-of-use",
                      "attribution": "GEBCO Compilation Group; GEBCO grid resampled by GMT. See source metadata for release.",
                      "role": "preview_elevation_6_arc_minutes"})
        catalog = {"schema_version": 1, "acquired_utc": datetime.now(timezone.utc).isoformat(),
                   "historical_1836_available": False, "modern_policy": "Natural Earth de facto reference"}
    with ThreadPoolExecutor(max_workers=4) as pool:
        records = list(pool.map(lambda spec: fetch_one(spec, args.out), specs))
    catalog["sources"] = records
    lock_path.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"source_count": len(records), "total_bytes": sum(s["bytes"] for s in records),
                      "lock": str(lock_path), "historical_1836_available": False}))


if __name__ == "__main__":
    main()
