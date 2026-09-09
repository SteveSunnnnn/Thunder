"""Offline polygon arrangement. Political masks are inputs, never inferred history.

All overlays use one metric fixed-precision grid. Output rings reference a single
canonical edge table; a shared edge is stored once, in opposite directions.
Precision is an authoring quantization, not a runtime crack tolerance.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

import geopandas as gpd
from shapely import set_precision, line_merge
from shapely.geometry import LineString, Polygon
from shapely.ops import polygonize, unary_union

SOVEREIGN_1836 = 1
MODERN_BORDER = 2
HISTORIC_TRANSFER = 4
DISPUTED = 8
SINGLE_LOCATION_CODES = {"HKG", "MAC", "SGP", "LUX"}


def metric(frame, grid_m=0.01):
    if frame.crs is None or frame.empty:
        raise ValueError("constraint layer must be nonempty and have a CRS")
    if frame.geometry.isna().any() or not frame.geometry.is_valid.all():
        raise ValueError("repair invalid source geometry before constraint compilation")
    result = frame.to_crs(3857).copy()
    result.geometry = result.geometry.map(lambda g: set_precision(g, grid_m))
    if result.geometry.is_empty.any():
        raise ValueError("source feature collapsed on the topology precision grid")
    return result


def assert_partition(frame, domain, label):
    """Require complete, non-overlapping coverage; do not hide slivers in tolerances."""
    index = frame.sindex
    for i, geometry in enumerate(frame.geometry):
        for j in index.query(geometry, predicate="intersects"):
            if j > i and geometry.intersection(frame.geometry.iloc[j]).area > 0:
                raise ValueError(f"{label}: overlapping polygons {i}/{j}")
    covered = unary_union(frame.geometry)
    if domain.difference(covered).area > 0:
        raise ValueError(f"{label}: missing coverage ({domain.difference(covered).area} m2)")


def constrain_locations(locations, coast_land, lakes, modern, historical,
                        transfers=None, grid_m=0.01):
    """Intersect both sovereignty partitions with dry land and candidate Locations.

    Modern/historical polygons require sovereign_key; transfer polygons require
    transfer_key. This deliberately rejects incomplete history instead of assigning
    present-day sovereignty to 1836. Water is removed before IDs are assigned.
    """
    if grid_m <= 0:
        raise ValueError("grid_m must be positive")
    candidates, coast, modern, historical = [metric(f, grid_m) for f in
                                            (locations, coast_land, modern, historical)]
    for label, frame in (("modern", modern), ("1836", historical)):
        if "sovereign_key" not in frame or frame.sovereign_key.isna().any() or (frame.sovereign_key == "").any():
            raise ValueError(f"{label}: sovereign_key required for every polygon")
    if "year" not in historical or not (historical.year == 1836).all():
        raise ValueError("historical boundary features must explicitly declare year=1836")
    domain = unary_union(coast.geometry)
    if lakes is not None and not lakes.empty:
        domain = domain.difference(unary_union(metric(lakes, grid_m).geometry))
    for frame, label in ((modern, "modern"), (historical, "1836"), (candidates, "Locations")):
        assert_partition(frame, domain, label)

    # Overlay only carries geography; ownership is copied to scenario columns.
    candidates.geometry = candidates.geometry.intersection(domain)
    candidates = candidates[~candidates.geometry.is_empty].copy()
    original_area = candidates.geometry.area.sum()
    for frame, column in ((modern, "modern_sovereign"), (historical, "sovereign_1836")):
        mask = frame[["sovereign_key", "geometry"]].rename(columns={"sovereign_key": column})
        candidates = gpd.overlay(candidates, mask, how="intersection", keep_geom_type=True)
    if transfers is not None and not transfers.empty:
        regions = metric(transfers, grid_m)
        if "transfer_key" not in regions:
            raise ValueError("transfer polygons require transfer_key")
        # identity retains the surrounding cities as well as each transfer zone.
        candidates = gpd.overlay(candidates, regions[["transfer_key", "geometry"]],
                                 how="identity", keep_geom_type=True)
    candidates = candidates.explode(index_parts=False).reset_index(drop=True)
    candidates.geometry = candidates.geometry.map(lambda g: set_precision(g, grid_m))
    candidates = candidates[~candidates.geometry.is_empty].copy()
    candidates["constraint_flags"] = SOVEREIGN_1836 | MODERN_BORDER
    if "transfer_key" in candidates:
        selected = candidates.transfer_key.notna() & (candidates.transfer_key != "")
        candidates.loc[selected, "constraint_flags"] |= HISTORIC_TRANSFER | DISPUTED
    candidates["country_tag"] = candidates.sovereign_1836
    assert_partition(candidates, domain, "constrained Locations")
    if unary_union(candidates.geometry).symmetric_difference(domain).area != 0:
        raise ValueError("constraint overlay changed dry-land coverage")
    candidates.attrs["source_area_m2"] = original_area
    return candidates


def bind_historical_settlements(locations, settlements):
    """Require an evidenced 1836 town inside every leaf; never synthesize a city.

    Input points carry ascii_key (loc_<region>_<town>), name, year, elevation_m,
    and optional priority. Mountain towns/oases may author a larger local limit
    through max_elevation_m instead of relocating the town to a fictitious site.
    """
    points = settlements.to_crs(locations.crs).copy()
    required = {"ascii_key", "name", "year", "elevation_m"}
    if not required.issubset(points):
        raise ValueError(f"historical settlements missing fields: {sorted(required - set(points))}")
    points = points[points.year == 1836].copy()
    points["priority"] = points.get("priority", 0)
    points = points.sort_values(["priority", "ascii_key"], ascending=[False, True])
    output = locations.copy()
    used = set()
    for i, row in output.iterrows():
        hits = points[points.geometry.within(row.geometry)]
        hits = hits[hits.elevation_m <= hits.get("max_elevation_m", 1500)]
        if hits.empty:
            raise ValueError(f"no evidenced habitable 1836 settlement for leaf {i}")
        town = hits.iloc[0]
        key = str(town.ascii_key)
        if not re.fullmatch(r"loc_[a-z0-9]+(?:_[a-z0-9]+)+", key) or key in used:
            raise ValueError(f"invalid or duplicate historical Location key: {key}")
        used.add(key)
        output.at[i, "location_key"] = key
        output.at[i, "province_key"] = key
        output.at[i, "name"] = str(town["name"])
        output.at[i, "settlement_x_m"] = town.geometry.x
        output.at[i, "settlement_y_m"] = town.geometry.y
        lo_x, lo_y, hi_x, hi_y = row.geometry.bounds
        output.at[i, "geometric_center_x_m"] = (lo_x + hi_x) / 2
        output.at[i, "geometric_center_y_m"] = (lo_y + hi_y) / 2
    return output


def consolidate_strategic_regions(locations, require_special_states=False):
    """Treaty masks override administrative seeds, but never sovereignty masks.

    territory_code is an authoring ISO-like identity, not a built-in country in
    the simulation. Microstate aggregation changes containers only; intact
    constrained leaves remain available for diplomacy.
    """
    result = locations.copy()
    if "transfer_key" in result:
        for key in sorted(result.transfer_key.dropna().unique()):
            if not key:
                continue
            selected = result[result.transfer_key == key]
            groups = list(selected.groupby(["modern_sovereign", "sovereign_1836"]))
            if len(groups) > 2:
                raise ValueError(f"transfer {key} needs {len(groups)} sovereign atoms; revise its authored mask")
            merged = []
            for _, group in groups:
                row = group.iloc[0].copy()
                if group.area_key.nunique() != 1:
                    raise ValueError(f"transfer {key} spans strategic Areas; author one parent")
                row.geometry = unary_union(group.geometry)
                row["state_key"] = f"state_transfer_{key}"
                row["trade_province_key"] = row.state_key
                merged.append(row.to_dict())
            result = gpd.GeoDataFrame([*result[result.transfer_key != key].to_dict("records"), *merged],
                                      geometry="geometry", crs=locations.crs)
    if require_special_states and "territory_code" not in result:
        raise ValueError("global special-State validation requires territory_code")
    if "territory_code" in result:
        for code in sorted(SINGLE_LOCATION_CODES):
            group = result[result.territory_code == code]
            if group.empty:
                if require_special_states:
                    raise ValueError(f"required single-Location State missing: {code}")
                continue
            if len(group.groupby(["modern_sovereign", "sovereign_1836"])) != 1:
                raise ValueError(f"{code} cannot be one Location without crossing a sovereign constraint")
            if group.area_key.nunique() != 1:
                raise ValueError(f"{code} must have one strategic Area")
            row = group.iloc[0].copy()
            row.geometry = unary_union(group.geometry)
            row["state_key"] = f"state_{code.lower()}"
            row["trade_province_key"] = row.state_key
            result = gpd.GeoDataFrame([*result[result.territory_code != code].to_dict("records"), row.to_dict()],
                                      geometry="geometry", crs=locations.crs)
    return result.reset_index(drop=True)


def chaikin_open(coordinates, iterations=2):
    """Smooth a canonical unlocked chain once, preserving junction endpoints."""
    points = list(coordinates)
    for _ in range(iterations):
        refined = [points[0]]
        for a, b in zip(points, points[1:]):
            refined.extend(((0.75*a[0]+0.25*b[0], 0.75*a[1]+0.25*b[1]),
                            (0.25*a[0]+0.75*b[0], 0.25*a[1]+0.75*b[1])))
        points = refined + [points[-1]]
    return points


def smooth_shared_boundaries(locations, locked_lines, iterations=2, grid_m=0.01):
    """Smooth each unlocked maximal chain once, then rebuild the entire coverage.

    Coast/lake/sovereignty edges and junctions are immutable. Reject any result
    that loses a Location or crosses its political partition, rather than
    accepting independently smoothed overlapping polygons.
    """
    if iterations not in range(5):
        raise ValueError("Chaikin iterations must be between zero and four")
    if iterations == 0:
        return locations.copy()
    network = unary_union([g.boundary for g in locations.geometry])
    locked = set_precision(locked_lines, grid_m)
    free = line_merge(network.difference(locked))
    smooth = []
    for line in getattr(free, "geoms", [free]):
        if not line.is_empty:
            smooth.append(set_precision(LineString(chaikin_open(line.coords, iterations)), grid_m))
    faces = list(polygonize(unary_union([network.intersection(locked), *smooth])))
    domain = unary_union(locations.geometry)
    groups = [[] for _ in range(len(locations))]
    index = locations.sindex
    for face in faces:
        face = face.intersection(domain)
        if face.is_empty or face.area == 0:
            continue
        candidates = index.query(face, predicate="intersects")
        if len(candidates) == 0:
            raise ValueError("smoothed face has no source Location")
        owner = max(candidates, key=lambda i: (face.intersection(locations.geometry.iloc[i]).area, -int(i)))
        groups[owner].append(face)
    if any(not parts for parts in groups):
        raise ValueError("smoothing collapsed a Location; reduce iterations or author a larger leaf")
    result = locations.copy()
    result.geometry = [unary_union(parts) for parts in groups]
    assert_partition(result, domain, "smoothed coverage")
    if unary_union(result.geometry).symmetric_difference(domain).area != 0:
        raise ValueError("smoothing changed the coastline")
    # This tests the whole partition, permitting internal city boundaries to move.
    for column in ("modern_sovereign", "sovereign_1836", "transfer_key"):
        if column not in locations:
            continue
        for key, original in locations.groupby(column):
            expected = unary_union(original.geometry)
            actual = unary_union(result[result[column] == key].geometry)
            if expected.symmetric_difference(actual).area != 0:
                raise ValueError(f"smoothing crossed locked {column}: {key}")
    return result


def shared_edge_document(locations):
    """Node the complete ring network before assigning canonical edge indices."""
    network = unary_union([g.boundary for g in locations.geometry])
    segments = set()
    for line in getattr(network, "geoms", [network]):
        for a, b in zip(line.coords, list(line.coords)[1:]):
            if a != b:
                segments.add(tuple(sorted((tuple(a), tuple(b)))))
    edges = sorted(segments)
    # Rebuild each polygon ring with all T-junction vertices from the noded graph.
    edge_frame = gpd.GeoSeries([LineString(e) for e in edges], crs=locations.crs)
    leaves = []
    incidence = [[] for _ in edges]
    for leaf, row in locations.reset_index(drop=True).iterrows():
        ids = []
        boundary = row.geometry.boundary
        for edge in sorted(edge_frame.sindex.query(boundary, predicate="intersects")):
            line = edge_frame.iloc[edge]
            if boundary.covers(line):
                ids.append(int(edge))
                incidence[edge].append(int(leaf))
        rebuilt = list(polygonize([edge_frame.iloc[e] for e in ids]))
        if not rebuilt:
            raise ValueError(f"leaf {leaf} does not form closed edge loops")
        leaves.append({"key": str(row.get("location_key", leaf)), "edge_ids": ids,
                       "constraint_flags": int(row.get("constraint_flags", 0))})
    if any(len(owners) not in (1, 2) for owners in incidence):
        raise ValueError("non-manifold topology: edge incidence must be one or two")
    return {"schema_version": 1, "crs": "EPSG:3857", "edges": edges,
            "edge_locations": incidence, "locations": leaves}


def write_shared_edges(locations, path: Path):
    path.write_text(json.dumps(shared_edge_document(locations), separators=(",", ":")), encoding="utf-8")
