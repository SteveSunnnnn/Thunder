"""Compile independent scenario ownership onto existing public-GIS geometry.

No commercial game directory is accepted or read. Only authored JSON, the GIS
source layer and a previously compiled page manifest enter this pipeline.
Province/raster identities are verified before reusing immutable geometry pages.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import colorsys
from pathlib import Path

import geopandas as gpd
import pandas as pd
from shapely.ops import unary_union
from shapely.geometry import LineString

import thunder_gis_compile as gis
from prepare_world_geography import owner_for


def province_keys(payload):
    magic, count = struct.unpack_from("<II", payload)
    if magic != gis.PROVINCE_MAGIC:
        raise ValueError("scenario overlay requires PRV2 definitions")
    offset = 8
    keys = []
    for _ in range(count):
        size, = struct.unpack_from("<H", payload, offset)
        offset += 2
        keys.append(payload[offset:offset + size].decode("utf-8"))
        offset += size + 36  # state/owner/market, centre x/y, area, kind/flags/pad
    if offset != len(payload):
        raise ValueError("province definition stride mismatch")
    return keys


def load_location_overrides(path, source_ids):
    if path is None:
        return {}
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema_version") != 1 or value.get("scenario_year") != 1836:
        raise ValueError("expected schema 1 Location overrides for 1836")
    owners = value["owners"]
    if set(owners) - set(source_ids):
        raise ValueError("Location override refers to missing source identities")
    if any(not isinstance(v, str) or (v and not v.startswith("country.")) for v in owners.values()):
        raise ValueError("invalid Location owner tag")
    return owners


def presentation(out, land, ids, scenario, metadata, base_labels):
    colors = bytearray(struct.pack("<II", 0x314c5043, len(ids.countries)))
    labels = []
    xmin, ymin, xmax, ymax = metadata["bounds_world_m"]
    for tag in ids.countries:
        key = tag.removeprefix("country.")
        authored = scenario.get("countries", {}).get(key, {})
        hashed = int.from_bytes(hashlib.sha256(key.encode()).digest()[:4], "little")
        fallback = [round(v * 255) for v in colorsys.hsv_to_rgb((hashed % 360) / 360.0, 0.28, 0.72)]
        color = authored.get("color", fallback)
        colors += bytes([*color, 255])
        group = land[land.country_tag == tag]
        if group.empty:
            continue
        geometry = unary_union(group.geometry)
        label_group = group[group.territory_code == authored.get("label_territory", "")]
        label_geometry = unary_union(label_group.geometry) if not label_group.empty else geometry
        parts = list(label_geometry.geoms) if label_geometry.geom_type == "MultiPolygon" else [label_geometry]
        largest = max(parts, key=lambda g: g.area)
        point = largest.representative_point()
        u = (point.x - xmin) / (xmax - xmin)
        v = (ymax - point.y) / (ymax - ymin)
        # Place the label on the largest connected body, never in its overseas
        # centroid. The renderer handles screen-space collision and scaling.
        corners = list(largest.minimum_rotated_rectangle.exterior.coords)
        a, b = max(zip(corners, corners[1:]), key=lambda pair: (pair[1][0]-pair[0][0])**2 + (pair[1][1]-pair[0][1])**2)
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx < 0:
            dx, dy = -dx, -dy
        # A bounding-box axis may cross another country inside a concavity.
        # Keep the visible label on the owned connected segment through its
        # anchor, so Württemberg's text does not run across Hohenzollern.
        axis = LineString([(point.x-dx*.28, point.y-dy*.28),
                           (point.x+dx*.28, point.y+dy*.28)])
        clipped = axis.intersection(largest)
        segments = [clipped] if clipped.geom_type == "LineString" else [
            g for g in getattr(clipped, "geoms", []) if g.geom_type == "LineString"]
        segments = [g for g in segments if g.distance(point) < 1e-5 and g.length > 0]
        if not segments:
            continue
        segment = max(segments, key=lambda g: g.length)
        start, end = segment.interpolate(.04, normalized=True), segment.interpolate(.96, normalized=True)
        ends = ((start.x-xmin)/(xmax-xmin), (ymax-start.y)/(ymax-ymin),
                (end.x-xmin)/(xmax-xmin), (ymax-end.y)/(ymax-ymin))
        labels.append((tag, authored.get("name", key.upper()), geometry.area / 1e6, ends))
    (out / "definitions/country_presentation.bin").write_bytes(colors)
    old_magic, old_count = struct.unpack_from("<II", base_labels)
    if old_magic != 0x314c424c:
        raise ValueError("expected LBL1 location labels")
    payload = bytearray(struct.pack("<II", 0x314c424c, len(labels) + old_count))
    for tag, name, area, ends in labels:
        payload += struct.pack("<BBHffd", 0, 220, 2, 0.0, 1.0, area)
        payload += gis._u16_string(tag) + gis._u16_string(name)
        payload += struct.pack("<ffff", *ends)
    payload += base_labels[8:]
    (out / "definitions/map_labels.bin").write_bytes(payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--scenario", type=Path, required=True)
    parser.add_argument("--base-chunks", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--location-overrides", type=Path)
    args = parser.parse_args()
    scenario = json.loads(args.scenario.read_text(encoding="utf-8"))
    if scenario.get("schema_version") != 1 or scenario.get("scenario_year") != 1836:
        raise ValueError("expected an authored schema 1 scenario for 1836")
    if scenario.get("default_policy") != "unassigned":
        raise ValueError("1836 cannot fall back to a modern-country owner")
    land = gis._normalise_land(gis._read_polygons(args.source / "locations.gpkg", None, "land"))
    overrides = load_location_overrides(args.location_overrides, land.source_id)
    seas = gis._normalise_water(gis._read_polygons(args.source / "seas.gpkg", None, "seas"), "sea", "sea")
    lakes = gis._normalise_water(gis._read_polygons(args.source / "lakes.gpkg", None, "lakes"), "lake", "lake")
    states = gis._read_polygons(args.source / "provinces.gpkg", None, "states")
    # State defaults from a different date must never override leaf ownership.
    states["country_tag"] = ""
    states["market_key"] = ""
    for i, row in land.iterrows():
        territory = str(row.get("territory_code", str(row.source_id).split("-")[0].split(".")[0]))
        owner = overrides.get(str(row.source_id), owner_for(scenario, territory, str(row.source_id)))
        land.at[i, "country_tag"] = owner
        land.at[i, "market_key"] = f"market.{owner.removeprefix('country.')}" if owner else ""
    ids = gis._build_ids(land, seas, lakes, states, {}, {})
    original_keys = province_keys((args.base_chunks / "definitions/provinces.bin").read_bytes())
    if original_keys != list(ids.province_id):
        raise ValueError("scenario changed raster identity ordering; rebuild geography instead")
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "manifest.txt").write_text("# INCOMPLETE SCENARIO BUILD\n", encoding="utf-8")
    water = gpd.GeoDataFrame(pd.concat([seas, lakes], ignore_index=True), geometry="geometry", crs=4326)
    gis._write_definitions(args.out, gis._project_rows(land), gis._project_rows(water), ids, states, {}, {})
    gis._write_map_hierarchy(args.out, gis._project_rows(land), gis._project_rows(water), ids)
    for filename in ('areas.bin', 'trade_provinces.bin'):
        if (args.out / 'definitions' / filename).read_bytes() != (args.base_chunks / 'definitions' / filename).read_bytes():
            raise ValueError('scenario overlay changed geographic parent identities; rebuild geography')
    metadata = json.loads((args.base_chunks / "metadata.json").read_text())
    metadata.update(country_count=len(ids.countries), scenario_year=1836,
                    scenario_status=scenario.get("status", "authored_seed"),
                    scenario_sha256=hashlib.sha256(args.scenario.read_bytes()).hexdigest(),
                    geography_class="public_gis_with_independent_1836_ownership",
                    historical_geometry_constraints=False)
    # Market count is not part of the runtime metadata contract, but useful to
    # offline audit tools. Country identities are part of the checked contract.
    metadata["market_count"] = len(ids.markets)
    # The caller's source layer has its own preparation record; no directory
    # discovery of external installations is performed here.
    source_lock = Path(__file__).resolve().parents[2] / "data/geography/sources.lock.json"
    if source_lock.is_file():
        from prepare_world_geography import load_catalog
        metadata["source_provenance"], _ = load_catalog(source_lock)
    metadata["scenario_subjects_authoring"] = scenario.get("subjects", {})
    refinement_path = args.source / "refinement_report.json"
    if refinement_path.is_file():
        metadata["geographic_refinement"] = json.loads(refinement_path.read_text(encoding="utf-8"))
        for territory in metadata["geographic_refinement"].get("complete_ownership_territories", []):
            required = land[land.territory_code == territory]
            if required.empty or (required.country_tag == "").any():
                raise ValueError("required complete territory has ownerless Locations: " + territory)
    if args.location_overrides:
        metadata["location_overrides_sha256"] = hashlib.sha256(args.location_overrides.read_bytes()).hexdigest()
    presentation(args.out, gis._project_rows(land), ids, scenario, metadata,
                 (args.base_chunks / "definitions/map_labels.bin").read_bytes())
    (args.out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    changed_types = {"country_definitions", "market_definitions", "state_definitions",
                     "province_definitions", "historical_setup", "metadata", "map_label_definitions",
                     "area_definitions", "trade_province_definitions", "location_definitions"}
    manifest = []
    for line in (args.base_chunks / "manifest.txt").read_text().splitlines():
        if not line.strip() or line.startswith(("#", "@")):
            manifest.append(line)
            continue
        fields = line.split(maxsplit=5)
        if len(fields) != 6:
            raise ValueError("invalid base manifest")
        if fields[0] not in changed_types:
            fields[5] = Path(os.path.relpath((args.base_chunks / fields[5]).resolve(), args.out.resolve())).as_posix()
        manifest.append(" ".join(fields))
    manifest.append("country_presentation 0 0 0 0 definitions/country_presentation.bin")
    (args.out / "manifest.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    unresolved = land[land.country_tag == ""]
    report = {"scenario_year": 1836, "country_count": len(ids.countries), "land_locations": len(land),
              "assigned_locations": int((land.country_tag != "").sum()), "unassigned_locations": len(unresolved),
              "unassigned_by_territory": unresolved.groupby("territory_code").size().to_dict(),
              "geometry_rebuilt": False, "raster_ids_unchanged": True,
              "location_override_count": len(overrides),
              "geography_refinement": metadata.get("geographic_refinement"),
              "border_accuracy": "administrative_seed_requires_historical_constraint_splits"}
    (args.out / "scenario_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    land[["province_key", "source_id", "territory_code", "country_tag", "market_key"]].to_csv(
        args.out / "ownership_audit.csv", index=False)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
