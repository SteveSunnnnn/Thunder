"""Acquire BKG-derived, commercially reusable public district geometry.

This is modern physical subdivision data, not an 1836 sovereignty source.
Historical ownership and cuts are independently authored against the Wiki.
"""
import json
from pathlib import Path
from urllib.request import urlopen
from fetch_public_sources import fetch_one

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / "data/geography"
LOCK = DIRECTORY / "germany_detail.lock.json"


def main():
    if LOCK.exists():
        document = json.loads(LOCK.read_text(encoding="utf-8"))
    else:
        endpoint = "https://www.geoboundaries.org/api/current/gbOpen/DEU/ADM3/"
        with urlopen(endpoint, timeout=30) as response:
            metadata = json.load(response)
        if metadata["boundaryLicense"] != "Data license Germany - Attribution - Version 2.0":
            raise ValueError("district source license changed; review before adding to asset pipeline")
        document = {"schema_version": 1, "sources": [{
            "id": "bkg_germany_districts_2021", "file": "raw/germany_districts.geojson",
            "url": metadata["simplifiedGeometryGeoJSON"],
            "license": "dl-de/by-2-0", "license_url": "https://www.govdata.de/dl-de/by-2-0",
            "attribution": "© GeoBasis-DE / BKG (2021); supplied through geoBoundaries gbOpen",
            "role": "modern_subdivision_geometry_only", "metadata_url": endpoint,
            "provider_source_url": metadata["boundarySourceURL"]
        }]}
    document["sources"] = [fetch_one(source, DIRECTORY) for source in document["sources"]]
    LOCK.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(document, indent=2))


if __name__ == "__main__":
    main()
