"""End-to-end synthetic constraint fixture; invokes the native pack validator.

Usage: python scripts/world_map/test_compile_pipeline.py --tools build
The fixture is intentionally synthetic and is never a historical world asset.
"""
import argparse
import subprocess
import sys
from pathlib import Path

import geopandas as gpd
from shapely.geometry import box, Point


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tools", type=Path, default=Path("build"))
    args = parser.parse_args()
    work = args.tools.resolve() / "geometry_fixture"
    work.mkdir(parents=True, exist_ok=True)

    def write(layer_name, shapes, **data):
        path = work / (layer_name + ".gpkg")
        gpd.GeoDataFrame(data, geometry=shapes, crs=3857).to_file(path, driver="GPKG", index=False)
        return path

    coast = write("coast", [box(0, 0, 8000, 8000)])
    locations = write("land", [box(0, 0, 8000, 8000)], province_key=["seed"],
                      state_key=["state_test"], area_key=["area_test"])
    modern = write("modern", [box(0, 0, 4000, 8000), box(4000, 0, 8000, 8000)], sovereign_key=["a", "b"])
    history = write("history", [box(0, 0, 8000, 4000), box(0, 4000, 8000, 8000)],
                    sovereign_key=["h1", "h2"], year=[1836, 1836])
    towns = write("towns", [Point(1000, 1000), Point(5000, 1000), Point(1000, 5000), Point(5000, 5000)],
                  ascii_key=["loc_test_sw", "loc_test_se", "loc_test_nw", "loc_test_ne"],
                  name=["Test SW", "Test SE", "Test NW", "Test NE"], year=[1836] * 4, elevation_m=[20] * 4)
    chunks = work / "chunks"
    command = [sys.executable, str(Path(__file__).with_name("thunder_gis_compile.py")),
               "--provinces", str(locations), "--coast-land", str(coast),
               "--modern-borders", str(modern), "--historical-borders", str(history),
               "--historical-settlements", str(towns), "--levels", "2",
               "--page-world-size-m", "8000", "--out", str(chunks)]
    subprocess.run(command, check=True)
    pack = work / "synthetic.thunderworld"
    subprocess.run([str(args.tools.resolve() / "thunder_world_compiler.exe"),
                    str(chunks / "manifest.txt"), str(pack)], check=True)
    subprocess.run([str(args.tools.resolve() / "thunder_world_validate.exe"), str(pack)], check=True)


if __name__ == "__main__":
    main()
