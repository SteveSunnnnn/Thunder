#!/usr/bin/env python3
"""Build normalized world GIS inputs from freshly locked sources.

The output is an authoring interchange for ``tools/thunder_gis_compile.py``. It
does not import any previous legacy map or files from another game project.
Natural Earth admin-1 polygons become stable Locations, admin-0 groupings
become Provinces, and geographic subregions become Areas. Scenario ownership
is applied as data and never changes the Location geometry.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import unicodedata
from collections import defaultdict
from pathlib import Path

import geopandas as gpd
import numpy as np
import pandas as pd
import rasterio
from rasterio.transform import from_bounds
from rasterio.windows import from_bounds as window_from_bounds
from shapely import make_valid
from shapely.geometry import MultiPolygon, Polygon, box
from shapely.ops import unary_union


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CATALOG = ROOT / "data" / "geography" / "sources.lock.json"
DEFAULT_SCENARIO = ROOT / "data" / "geography" / "ownership_1836.json"
DEFAULT_OUTPUT = ROOT / "generated" / "world_map" / "source"
WORLD_CLIP = box(-180.0, -60.0, 180.0, 85.0)
MICROSTATE_MAX_AREA_KM2 = 10_000.0


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_key(value: object, fallback: str) -> str:
    text = unicodedata.normalize("NFKD", str(value or ""))
    text = text.encode("ascii", "ignore").decode("ascii").lower()
    text = re.sub(r"[^a-z0-9]+", ".", text).strip(".")
    return text or fallback


def text_value(row: pd.Series, *names: str, default: str = "") -> str:
    for name in names:
        if name in row and pd.notna(row[name]):
            value = str(row[name]).strip()
            if value and value.lower() not in {"nan", "none", "-99"}:
                return value
    return default


def stable_admin0_code(row: pd.Series) -> str:
    for name in ("ADM0_A3", "adm0_a3", "ADM0_A3_US", "ISO_A3", "SOV_A3", "sov_a3"):
        value = text_value(row, name)
        if value:
            return safe_key(value, "unknown").upper()
    return f"NE{int(row.get('NE_ID', row.get('ne_id', 0))):010d}"


def read_zip(path: Path) -> gpd.GeoDataFrame:
    return gpd.read_file(f"zip://{path}")


def polygon_parts(geometry) -> list[Polygon]:
    if geometry is None or geometry.is_empty:
        return []
    if geometry.geom_type == "Polygon":
        return [geometry]
    if geometry.geom_type == "MultiPolygon":
        return list(geometry.geoms)
    return [part for part in getattr(geometry, "geoms", []) if part.geom_type == "Polygon"]


def as_multi(geometry) -> MultiPolygon:
    parts = polygon_parts(geometry)
    return MultiPolygon(parts)


def load_catalog(path: Path) -> tuple[dict, dict[str, Path]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("unsupported geography source catalog")
    resolved: dict[str, Path] = {}
    for source in document.get("sources", []):
        source_path = path.parent / str(source["file"])
        if not source_path.is_file():
            raise FileNotFoundError(f"missing locked geography source: {source_path}")
        actual = sha256(source_path)
        if actual != str(source["sha256"]).lower():
            raise ValueError(f"geography source checksum mismatch: {source_path.name}")
        resolved[str(source["id"])] = source_path
    return document, resolved


def load_scenario(path: Path) -> dict:
    if not path.is_file():
        return {"schema_version": 1, "default_policy": "unassigned",
                "admin0_owners": {}, "admin1_owners": {}}
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("unsupported 1836 ownership schema")
    return document


def owner_for(scenario: dict, adm0: str, adm1: str) -> str:
    if adm1 in scenario.get("explicit_unassigned", []):
        return ""
    specific = scenario.get("admin1_owners", {}).get(adm1)
    if specific:
        return str(specific)
    country = scenario.get("admin0_owners", {}).get(adm0)
    if country:
        return str(country)
    if scenario.get("default_policy") == "unassigned":
        return ""
    return f"country.{safe_key(adm0, 'unassigned')}"


def build_locations(admin0: gpd.GeoDataFrame, admin1: gpd.GeoDataFrame,
                    ocean_geometry, scenario: dict) -> gpd.GeoDataFrame:
    country_rows: dict[str, pd.Series] = {}
    for _, row in admin0.iterrows():
        country_rows[stable_admin0_code(row)] = row

    by_country: dict[str, list[pd.Series]] = defaultdict(list)
    for _, row in admin1.iterrows():
        by_country[stable_admin0_code(row)].append(row)

    records: list[dict] = []
    used_keys: set[str] = set()
    for adm0, country in sorted(country_rows.items()):
        country_geometry = make_valid(country.geometry).intersection(WORLD_CLIP)
        if country_geometry.is_empty:
            continue
        area_name = text_value(country, "SUBREGION", "REGION_UN", "CONTINENT",
                               default="World")
        area_key = f"area.{safe_key(area_name, 'world')}"
        province_key = f"province.{safe_key(adm0, 'unknown')}"
        country_area_km2 = float(
            gpd.GeoSeries([country_geometry], crs=4326).to_crs(6933).iloc[0].area /
            1_000_000.0)
        candidates = by_country.get(adm0, [])
        if country_area_km2 <= MICROSTATE_MAX_AREA_KM2:
            # A microstate is already a meaningful Location. Subdividing its
            # tiny islands or municipal parts creates identities that cannot
            # survive a practical global raster and adds no strategy value.
            candidates = []
        clipped_parts = []
        for sequence, row in enumerate(sorted(
                candidates,
                key=lambda item: (text_value(item, "adm1_code"),
                                  int(item.get("ne_id", 0))))):
            geometry = make_valid(row.geometry).intersection(country_geometry)
            if geometry.is_empty:
                continue
            adm1_code = text_value(row, "adm1_code", "iso_3166_2", "gn_a1_code",
                                   default=f"{adm0}.{int(row.get('ne_id', sequence))}")
            base = f"location.{safe_key(adm1_code, safe_key(adm0, 'unknown'))}"
            key = base
            suffix = 2
            while key in used_keys:
                key = f"{base}.{suffix}"
                suffix += 1
            used_keys.add(key)
            clipped_parts.append(geometry)
            owner = owner_for(scenario, adm0, adm1_code)
            records.append({
                "province_key": key,
                "location_key": key,
                "trade_province_key": province_key,
                "state_key": province_key,
                "area_key": area_key,
                "country_tag": owner,
                "market_key": f"market.{safe_key(owner, 'unassigned')}" if owner else "",
                "name": text_value(row, "name_en", "name", default=adm1_code),
                "source_class": "modern_administrative_reference",
                "source_id": adm1_code,
                "territory_code": adm0,
                "coastal": bool(geometry.boundary.intersects(ocean_geometry.boundary)),
                "impassable": adm0 == "ATA",
                "constraint_flags": (1 << 1) | (1 << 4),
                "geometry": as_multi(geometry),
            })

        # Natural Earth explicitly lacks admin-1 coverage for some small
        # states. Preserve those countries (including Hong Kong, Macau and
        # Singapore) as meaningful Locations instead of dropping them.
        covered = unary_union(clipped_parts) if clipped_parts else None
        remainder = country_geometry if covered is None else country_geometry.difference(covered)
        remainder_parts = polygon_parts(remainder)
        if not candidates or remainder_parts:
            projected = gpd.GeoSeries(remainder_parts, crs=4326).to_crs(6933) if remainder_parts else []
            # Retain all uncovered land. Tiny pieces may share one leaf, but
            # deleting them creates topology holes and invents ocean inland.
            retained = remainder_parts
            if retained:
                # One fragmented polity remains one stable Location. Splitting
                # every tiny island into a separate raster identity made small
                # archipelagos disappear at practical world-page resolutions.
                geometry = as_multi(unary_union(retained))
                source_id = f"{adm0}.remainder"
                base = f"location.{safe_key(adm0, 'unknown')}.remainder"
                key = base
                suffix = 2
                while key in used_keys:
                    key = f"{base}.{suffix}"
                    suffix += 1
                used_keys.add(key)
                owner = owner_for(scenario, adm0, source_id)
                records.append({
                    "province_key": key,
                    "location_key": key,
                    "trade_province_key": province_key,
                    "state_key": province_key,
                    "area_key": area_key,
                    "country_tag": owner,
                    "market_key": f"market.{safe_key(owner, 'unassigned')}" if owner else "",
                    "name": text_value(country, "NAME_EN", "NAME", default=adm0),
                    "source_class": "modern_country_fallback",
                    "source_id": source_id,
                    "territory_code": adm0,
                    "coastal": bool(geometry.boundary.intersects(ocean_geometry.boundary)),
                    "impassable": adm0 == "ATA",
                    "constraint_flags": (1 << 1) | (1 << 5),
                    "geometry": geometry,
                })

    result = gpd.GeoDataFrame(records, geometry="geometry", crs=4326)
    if result.empty:
        raise ValueError("normalization produced no Locations")
    return result.sort_values("province_key", kind="stable").reset_index(drop=True)


def build_provinces(locations: gpd.GeoDataFrame) -> gpd.GeoDataFrame:
    records = []
    for state_key, group in locations.groupby("state_key", sort=True):
        owners = sorted({str(value) for value in group.country_tag if str(value)})
        markets = sorted({str(value) for value in group.market_key if str(value)})
        largest = group.to_crs(6933).area.idxmax()
        records.append({
            "state_key": state_key,
            "country_tag": owners[0] if len(owners) == 1 else "",
            "market_key": markets[0] if len(markets) == 1 else "",
            "capital_province": str(locations.loc[largest, "province_key"]),
            "geometry": as_multi(unary_union(group.geometry)),
        })
    return gpd.GeoDataFrame(records, geometry="geometry", crs=4326)


def build_areas(locations: gpd.GeoDataFrame) -> gpd.GeoDataFrame:
    records = []
    for area_key, group in locations.groupby("area_key", sort=True):
        records.append({"area_key": area_key,
                        "geometry": as_multi(unary_union(group.geometry))})
    return gpd.GeoDataFrame(records, geometry="geometry", crs=4326)


def build_architecture_regions(locations: gpd.GeoDataFrame) -> gpd.GeoDataFrame:
    families = {
        "africa": "african", "australia.and.new.zealand": "oceanic",
        "caribbean": "caribbean", "central.america": "latin_american",
        "central.asia": "central_asian", "eastern.asia": "east_asian",
        "eastern.europe": "eastern_european", "melanesia": "oceanic",
        "micronesia": "oceanic", "northern.africa": "north_african",
        "northern.america": "north_american", "northern.europe": "northern_european",
        "polynesia": "oceanic", "south.america": "latin_american",
        "south.eastern.asia": "south_east_asian", "southern.africa": "southern_african",
        "southern.asia": "south_asian", "southern.europe": "southern_european",
        "western.africa": "west_african", "western.asia": "west_asian",
        "western.europe": "western_european",
    }
    records = []
    for area_key, group in locations.groupby("area_key", sort=True):
        suffix = area_key.removeprefix("area.")
        records.append({"region_key": area_key,
                        "architecture_family": families.get(suffix, "default"),
                        "geometry": as_multi(unary_union(group.geometry))})
    return gpd.GeoDataFrame(records, geometry="geometry", crs=4326)


def normalize_water(source: gpd.GeoDataFrame, prefix: str) -> gpd.GeoDataFrame:
    records = []
    for sequence, row in source.sort_values(
            [name for name in ("scalerank", "ne_id") if name in source.columns],
            kind="stable").iterrows():
        geometry = make_valid(row.geometry).intersection(WORLD_CLIP)
        if geometry.is_empty:
            continue
        source_id = text_value(row, "ne_id", default=str(sequence))
        name = text_value(row, "name_en", "name", default=f"{prefix} {sequence}")
        records.append({"province_key": f"{prefix}.{safe_key(name, source_id)}.{source_id}",
                        "name": name, "sea_start": prefix == "sea",
                        "geometry": as_multi(geometry)})
    return gpd.GeoDataFrame(records, geometry="geometry", crs=4326)


def normalize_hubs(source: gpd.GeoDataFrame,
                   locations: gpd.GeoDataFrame) -> gpd.GeoDataFrame:
    population = pd.to_numeric(source.get("POP_MAX", 0), errors="coerce").fillna(0)
    rank = pd.to_numeric(source.get("SCALERANK", 99), errors="coerce").fillna(99)
    capital = pd.to_numeric(source.get("ADM0CAP", 0), errors="coerce").fillna(0)
    selected = source[(population >= 50_000) | (rank <= 5) | (capital == 1)].copy()
    selected["source_order"] = selected.index.astype(int)
    selected["key"] = [f"settlement.{int(value)}" for value in selected["NE_ID"]]
    selected["name"] = selected["NAME_EN"].fillna(selected["NAMEASCII"]).fillna(selected["NAME"])
    selected["hub_kind"] = np.where(capital.loc[selected.index] == 1, "city", "city")
    selected["importance"] = np.clip(
        np.log10(np.maximum(population.loc[selected.index], 1.0)) * 1000.0,
        500.0, 65000.0).astype(int)
    joined = gpd.sjoin(
        selected,
        locations[["province_key", "state_key", "geometry"]],
        predicate="within", how="inner")
    # A point exactly on coincident polygons can produce more than one row.
    # Stable key order makes the assignment reproducible.
    joined = joined.sort_values(["source_order", "province_key"], kind="stable")
    joined = joined.drop_duplicates("source_order", keep="first")
    return joined[["key", "name", "hub_kind", "importance",
                   "province_key", "state_key", "geometry"]].reset_index(drop=True)


def normalize_lines(source: gpd.GeoDataFrame, maximum_rank: int) -> gpd.GeoDataFrame:
    if "scalerank" in source.columns:
        rank = pd.to_numeric(source["scalerank"], errors="coerce").fillna(99)
        source = source[rank <= maximum_rank].copy()
    source = source[source.geometry.notna() & ~source.geometry.is_empty].copy()
    source.geometry = source.geometry.make_valid()
    return source.reset_index(drop=True)


def write_relief(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with rasterio.open(source) as src:
        source_transform = src.transform
        window = window_from_bounds(-180.0, -60.0, 180.0, 85.0,
                                    transform=source_transform)
        window = window.round_offsets().round_lengths()
        data = src.read(1, window=window, masked=True).astype(np.float32)
        data = (data * src.scales[0] + src.offsets[0]).filled(-32768)
        profile = src.profile.copy()
        destination_transform = src.window_transform(window)
    profile.update(driver="GTiff", crs="EPSG:4326",
                   transform=destination_transform,
                   width=data.shape[1], height=data.shape[0], count=1,
                   dtype=str(data.dtype), nodata=-32768,
                   compress="deflate", predictor=2, tiled=True,
                   blockxsize=256, blockysize=256)
    with rasterio.open(destination, "w", **profile) as dst:
        dst.write(data, 1)


def write_gpkg(frame: gpd.GeoDataFrame, path: Path, layer: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frame.to_file(path, layer=layer, driver="GPKG", index=False)


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare world geography from locked public sources")
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--scenario", type=Path, default=DEFAULT_SCENARIO)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--game", type=str, default="thunder",
                        help="游戏标识，写入 prepare_report.json 的 game 字段")
    args = parser.parse_args()

    catalog, sources = load_catalog(args.catalog)
    scenario = load_scenario(args.scenario)
    admin0 = read_zip(sources["natural_earth_admin_0_countries_10m"])
    admin1 = read_zip(sources["natural_earth_admin_1_states_provinces_10m"])
    ocean = read_zip(sources["natural_earth_ocean_10m"])
    lakes = read_zip(sources["natural_earth_lakes_10m"])
    rivers = read_zip(sources["natural_earth_rivers_10m"])
    roads = read_zip(sources["natural_earth_roads_10m"])
    rails = read_zip(sources["natural_earth_railroads_10m"])
    places = read_zip(sources["natural_earth_populated_places_10m"])

    ocean_geometry = unary_union(ocean.geometry)
    locations = build_locations(admin0, admin1, ocean_geometry, scenario)
    water_geometry = unary_union([ocean_geometry, unary_union(lakes.geometry)])
    locations.geometry = locations.geometry.map(lambda g: as_multi(make_valid(g).difference(water_geometry)))
    locations = locations[~locations.geometry.is_empty].reset_index(drop=True)
    provinces = build_provinces(locations)
    areas = build_areas(locations)
    architecture = build_architecture_regions(locations)
    seas = normalize_water(ocean, "sea")
    lake_locations = normalize_water(lakes, "lake")
    hubs = normalize_hubs(places, locations)
    river_lines = normalize_lines(rivers, 7)
    road_lines = normalize_lines(roads, 5)
    rail_lines = normalize_lines(rails, 5)

    args.out.mkdir(parents=True, exist_ok=True)
    write_gpkg(locations, args.out / "locations.gpkg", "locations")
    write_gpkg(provinces, args.out / "provinces.gpkg", "provinces")
    write_gpkg(areas, args.out / "areas.gpkg", "areas")
    write_gpkg(seas, args.out / "seas.gpkg", "seas")
    write_gpkg(lake_locations, args.out / "lakes.gpkg", "lakes")
    write_gpkg(hubs, args.out / "settlements.gpkg", "settlements")
    write_gpkg(river_lines, args.out / "rivers.gpkg", "rivers")
    write_gpkg(road_lines, args.out / "roads.gpkg", "roads")
    write_gpkg(rail_lines, args.out / "railroads.gpkg", "railroads")
    write_gpkg(architecture, args.out / "architecture_regions.gpkg", "architecture_regions")
    relief = sources.get("gebco_earth_relief_06m", sources.get("gmt_earth_relief_05m"))
    if relief is None:
        raise ValueError("locked elevation source is missing")
    write_relief(relief, args.out / "elevation.tif")

    report = {
        "schema_version": 1,
        "game": args.game,
        "scenario_year": 1836 if args.scenario.is_file() else None,
        "historical_border_constraints": False,
        "geography_class": "modern_reference_preview",
        "source_catalog_sha256": sha256(args.catalog),
        "source_count": len(catalog["sources"]),
        "location_count": len(locations),
        "province_count": locations["state_key"].nunique(),
        "area_count": locations["area_key"].nunique(),
        "country_identity_count": locations["country_tag"].nunique(),
        "sea_count": len(seas),
        "lake_count": len(lake_locations),
        "settlement_count": len(hubs),
        "river_features": len(river_lines),
        "road_features": len(road_lines),
        "rail_features": len(rail_lines),
        "ownership_default_policy": scenario.get("default_policy", "admin0_seed"),
    }
    (args.out / "prepare_report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
