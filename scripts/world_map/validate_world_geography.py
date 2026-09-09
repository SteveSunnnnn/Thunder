#!/usr/bin/env python3
"""Strict source-geography validation for a compiled world map."""
from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path

import geopandas as gpd
import numpy as np
from shapely.ops import unary_union


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / "generated" / "world_map" / "source"


def load(path: Path, layer: str) -> gpd.GeoDataFrame:
    frame = gpd.read_file(path, layer=layer)
    if frame.crs is None:
        raise ValueError(f"{path.name}:{layer} has no CRS")
    return frame


def normalized_area_delta(left, right) -> float:
    left_area = max(float(left.area), 1.0)
    return float(left.symmetric_difference(right).area) / left_area


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Area/Province/Location geography")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--overlap-tolerance-km2", type=float, default=1.0)
    parser.add_argument("--union-relative-tolerance", type=float, default=1.0e-7)
    args = parser.parse_args()

    locations = load(args.source / "locations.gpkg", "locations")
    provinces = load(args.source / "provinces.gpkg", "provinces")
    areas = load(args.source / "areas.gpkg", "areas")
    seas = load(args.source / "seas.gpkg", "seas")
    lakes = load(args.source / "lakes.gpkg", "lakes")

    errors: list[str] = []
    warnings: list[str] = []
    for field in ("province_key", "location_key", "state_key", "area_key"):
        if field not in locations.columns:
            errors.append(f"locations missing field {field}")
            continue
        values = locations[field].astype(str)
        if (values.str.len() == 0).any():
            errors.append(f"locations contain empty {field}")
        duplicate = values[values.duplicated()].head(1)
        if not duplicate.empty and field in {"province_key", "location_key"}:
            errors.append(f"duplicate {field}: {duplicate.iloc[0]}")
    if "province_key" in locations and "location_key" in locations:
        if not np.array_equal(locations.province_key.values, locations.location_key.values):
            errors.append("raster province compatibility keys do not match Location keys")

    invalid = locations[~locations.geometry.is_valid]
    empty = locations[locations.geometry.is_empty | locations.geometry.isna()]
    if not invalid.empty:
        errors.append(f"invalid Location polygons: {len(invalid)}")
    if not empty.empty:
        errors.append(f"empty Location polygons: {len(empty)}")

    # Every Province belongs to exactly one Area because all of its child
    # Locations must agree on the parent Area.
    province_area_counts = locations.groupby("state_key")["area_key"].nunique()
    bad_parent = province_area_counts[province_area_counts != 1]
    if not bad_parent.empty:
        errors.append(f"Provinces with multiple/no Areas: {len(bad_parent)}")

    projected_locations = locations.to_crs(6933)
    projected_provinces = provinces.to_crs(6933)
    projected_areas = areas.to_crs(6933)
    projected_locations.geometry = projected_locations.geometry.make_valid()
    projected_provinces.geometry = projected_provinces.geometry.make_valid()
    projected_areas.geometry = projected_areas.geometry.make_valid()
    province_geometry = dict(zip(projected_provinces.state_key, projected_provinces.geometry))
    area_geometry = dict(zip(projected_areas.area_key, projected_areas.geometry))
    for state_key, group in projected_locations.groupby("state_key", sort=False):
        expected = unary_union(group.geometry)
        actual = province_geometry.get(state_key)
        if actual is None:
            errors.append(f"Province geometry missing: {state_key}")
        elif normalized_area_delta(expected, actual) > args.union_relative_tolerance:
            errors.append(f"Province union mismatch: {state_key}")
    for area_key, group in projected_provinces.groupby(
            projected_provinces.state_key.map(
                locations.drop_duplicates("state_key").set_index("state_key").area_key),
            sort=False):
        expected = unary_union(group.geometry)
        actual = area_geometry.get(area_key)
        if actual is None:
            errors.append(f"Area geometry missing: {area_key}")
        elif normalized_area_delta(expected, actual) > args.union_relative_tolerance:
            errors.append(f"Area union mismatch: {area_key}")

    # Detect substantive overlaps. Boundary-only intersection is allowed and
    # is exactly what creates adjacency.
    overlap_tolerance = args.overlap_tolerance_km2 * 1_000_000.0
    overlap_pairs = 0
    overlap_area_m2 = 0.0
    spatial_index = projected_locations.sindex
    for left, geometry in enumerate(projected_locations.geometry):
        for right in spatial_index.query(geometry, predicate="intersects"):
            right = int(right)
            if right <= left:
                continue
            area = float(geometry.intersection(projected_locations.geometry.iloc[right]).area)
            if area > overlap_tolerance:
                overlap_pairs += 1
                overlap_area_m2 += area
                if overlap_pairs <= 10:
                    errors.append(
                        f"Location overlap {locations.location_key.iloc[left]} / "
                        f"{locations.location_key.iloc[right]}: {area / 1_000_000.0:.3f} km2")

    # Representative-point picking must identify exactly its own Location.
    points = locations.copy()
    points.geometry = locations.geometry.representative_point()
    picked = gpd.sjoin(points[["location_key", "geometry"]],
                       locations[["location_key", "geometry"]],
                       predicate="within", how="left",
                       lsuffix="sample", rsuffix="polygon")
    pick_counts = Counter(picked.location_key_sample)
    wrong = picked[picked.location_key_sample != picked.location_key_polygon]
    if not wrong.empty or any(value != 1 for value in pick_counts.values()):
        errors.append(f"representative-point Location picking failures: {len(wrong)}")

    # Preserve the explicit small-territory exceptions as distinct Locations.
    for code in ("HKG", "MAC", "SGP", "LUX"):
        token = code.lower()
        if not locations.source_id.astype(str).str.lower().str.startswith(token).any():
            errors.append(f"required small territory missing: {code}")
        else:
            group = locations[locations.source_id.astype(str).str.lower().str.startswith(token)]
            if len(group) != 1 or locations[locations.state_key == group.iloc[0].state_key].shape[0] != 1:
                errors.append(f"required territory is not a single-Location State: {code}")

    water = unary_union(gpd.GeoDataFrame(
        geometry=[*seas.to_crs(6933).geometry, *lakes.to_crs(6933).geometry], crs=6933).geometry)
    water_overlap = sum(float(g.intersection(water).area) for g in projected_locations.geometry)
    if water_overlap > overlap_tolerance:
        errors.append(f"land/water overlap: {water_overlap / 1e6:.6f} km2")

    # Coarse adjacency count is an audit signal; disconnected island Locations
    # are valid and therefore not rejected.
    adjacency_pairs = 0
    for left, geometry in enumerate(projected_locations.geometry):
        for right in spatial_index.query(geometry, predicate="touches"):
            if int(right) > left:
                adjacency_pairs += 1
    if adjacency_pairs == 0:
        errors.append("Location adjacency graph is empty")

    if len(seas) == 0:
        errors.append("world has no sea polygon")
    if len(lakes) == 0:
        errors.append("world has no lake polygons")

    report = {
        "schema_version": 1,
        "status": "PASS" if not errors else "FAIL",
        "location_count": len(locations),
        "province_count": locations.state_key.nunique(),
        "area_count": locations.area_key.nunique(),
        "invalid_location_count": len(invalid),
        "empty_location_count": len(empty),
        "overlap_pair_count": overlap_pairs,
        "overlap_area_km2": overlap_area_m2 / 1_000_000.0,
        "land_water_overlap_km2": water_overlap / 1_000_000.0,
        "adjacency_pair_count": adjacency_pairs,
        "pick_sample_count": len(points),
        "sea_count": len(seas),
        "lake_count": len(lakes),
        "warnings": warnings,
        "errors": errors,
    }
    output = json.dumps(report, indent=2, sort_keys=True) + "\n"
    report_path = args.report or args.source / "validation_report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
