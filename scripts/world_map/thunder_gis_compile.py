#!/usr/bin/env python3
"""Compile the authored GIS layers into one streamable Thunder world pack.

This tool is intentionally an offline compiler. The desktop and simulation
never read these GIS files; they consume the ``world.thunderworld`` produced by
``thunder_world_compiler``.  The compiler emits page families for every clipmap
level, explicit coordinate metadata, stable province IDs, historical setup,
topology, authored placement and vector source chunks.

The minimum land schema is ``province_key`` and ``state_key``. Ownership and
market columns are accepted as authoring conveniences, but are copied into
the HistoricalSetup chunk rather than used as economic values in the pack.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import struct
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import geopandas as gpd
import numpy as np
import pandas as pd
import rasterio
from rasterio.enums import Resampling
from rasterio.features import rasterize
from rasterio.transform import from_origin
from rasterio.vrt import WarpedVRT
from rasterio.windows import Window
from scipy import ndimage
from shapely import make_valid
from shapely.affinity import translate
from shapely.geometry import LineString, Point, box
from shapely.ops import split, transform, unary_union, polylabel

from constrained_topology import (constrain_locations, bind_historical_settlements,
                                  write_shared_edges, smooth_shared_boundaries, metric,
                                  consolidate_strategic_regions)
from coast_distance import coast_distance_jfa

PAGE = 128
CHUNK_M = 64_000.0
DEFAULT_RESOLUTION_M = CHUNK_M / PAGE
MAX_COAST_M = 16_000.0
MIN_HEIGHT_M = -12_000.0
HEIGHT_STEP_M = 0.5
MAX_HEIGHT_M = MIN_HEIGHT_M + 65_535.0 * HEIGHT_STEP_M

PROJECTION = "mercator"
TARGET_CRS = "EPSG:3857"

PLC_MAGIC = 0x31434C50
ANC_MAGIC = 0x31434E41
STATE_MAGIC = 0x32535453
PROVINCE_MAGIC = 0x32565250
HISTORY_MAGIC = 0x31534948
COUNTRY_MAGIC = 0x32544E43
ARCHITECTURE_MAGIC = 0x31435241  # ARC1
RESOURCE_DISTRIBUTION_MAGIC = 0x31534452  # RDS1

PLACEMENT_BUILDABLE = 0
PLACEMENT_URBAN = 1
PLACEMENT_RURAL = 2
PLACEMENT_INDUSTRIAL = 3
PLACEMENT_FARM = 4
PLACEMENT_MINE = 5
PLACEMENT_PORT = 6
PLACEMENT_VEGETATION = 7
FLAG_BUILDABLE = 1
FLAG_COASTAL = 2

PROVINCE_LAND = 0
PROVINCE_SEA = 1
PROVINCE_LAKE = 2

ADJACENCY_LAND = 1 << 0
ADJACENCY_RIVER = 1 << 1
ADJACENCY_STRAIT = 1 << 2
ADJACENCY_IMPASSABLE = 1 << 3
ADJACENCY_SEA = 1 << 4
ADJACENCY_CANAL = 1 << 5
ADJACENCY_LAKE = 1 << 6


@dataclass
class IdTables:
    countries: list[str]
    country_id: dict[str, int]
    markets: list[str]
    market_id: dict[str, int]
    states: list[str]
    state_id: dict[str, int]
    province_id: dict[str, int]


@dataclass
class WorldBounds:
    wgs84: tuple[float, float, float, float]
    world: tuple[float, float, float, float]
    horizontal_wrap: bool


def _u16_string(text: str) -> bytes:
    encoded = str(text).encode("utf-8")
    if len(encoded) > 65_535:
        raise ValueError(f"key too long: {str(text)[:80]}")
    return struct.pack("<H", len(encoded)) + encoded


def _write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _value(row, *names: str, default=""):
    for name in names:
        if name in row and pd.notna(row[name]) and str(row[name]).strip() not in ("", "nan", "None"):
            return row[name]
    return default


def _as_text(value) -> str:
    return str(value).strip()


def _as_bool(value) -> bool:
    if isinstance(value, bool):
        return value
    return _as_text(value).lower() in {"1", "true", "yes", "y", "on"}


def _polygonal(geometry):
    if geometry.geom_type in {"Polygon", "MultiPolygon"}:
        return geometry
    return unary_union([_polygonal(part) for part in getattr(geometry, "geoms", [])
                        if part.geom_type in {"Polygon", "MultiPolygon", "GeometryCollection"}])


def _read_polygons(path: Path | None, layer: str | None, kind: str, required: Iterable[str] = ()) -> gpd.GeoDataFrame:
    if path is None:
        return gpd.GeoDataFrame(columns=[*required, "geometry"], geometry="geometry", crs="EPSG:4326")
    gdf = gpd.read_file(path, layer=layer)
    if gdf.empty:
        raise ValueError(f"{kind} layer is empty: {path}")
    if gdf.crs is None:
        raise ValueError(f"{kind} layer has no CRS: {path}")
    gdf = gdf[gdf.geometry.notna() & ~gdf.geometry.is_empty].copy()
    gdf.geometry = gdf.geometry.make_valid().map(_polygonal)
    gdf = gdf[gdf.geometry.geom_type.isin(["Polygon", "MultiPolygon"])].copy()
    if gdf.empty:
        raise ValueError(f"{kind} layer has no polygon geometry after repair")
    gdf = gdf.to_crs(4326)
    gdf.geometry = gdf.geometry.map(_repair_dateline_geometry)
    gdf.geometry = gdf.geometry.make_valid().map(_polygonal)
    gdf = gdf[~gdf.geometry.is_empty].reset_index(drop=True)
    missing = [name for name in required if name not in gdf.columns]
    if missing:
        raise ValueError(f"{kind} layer missing fields: {', '.join(missing)}")
    return gdf.reset_index(drop=True)


def _repair_dateline_geometry(geometry):
    """Split a polygon crossing +/-180 instead of projecting a 358-degree bow."""
    if geometry is None or geometry.is_empty or geometry.geom_type not in {"Polygon", "MultiPolygon"}:
        return geometry
    if geometry.geom_type == "MultiPolygon":
        return unary_union([_repair_dateline_geometry(part) for part in geometry.geoms])
    # A wide bounding box is not evidence of a dateline crossing (Russia,
    # archipelagos and the global ocean are legitimate wide geometries).
    # Only an actual ring jump needs unwrapping. +/-180 world-clip edges are
    # explicit domain boundaries and must remain intact.
    def crosses(ring):
        return any(abs(a[0] - b[0]) > 180.0 and
                   not (abs(a[0]) == 180.0 and abs(b[0]) == 180.0)
                   for a, b in zip(ring.coords, list(ring.coords)[1:]))
    if not any(crosses(ring) for ring in [geometry.exterior, *geometry.interiors]):
        return geometry
    # GeoJSON commonly encodes a dateline ring as 179 -> -179. Unwrap the
    # negative side, split at 180, then wrap the eastern piece back.
    def unwrap_x(x, y, z=None):
        if np.isscalar(x):
            shifted = x + 360.0 if x < 0.0 else x
        else:
            shifted = [value + 360.0 if value < 0.0 else value for value in x]
        return (shifted, y) if z is None else (shifted, y, z)

    def wrap_x(x, y, z=None):
        if np.isscalar(x):
            shifted = x - 360.0 if x >= 180.0 else x
        else:
            shifted = [value - 360.0 if value >= 180.0 else value for value in x]
        return (shifted, y) if z is None else (shifted, y, z)

    unwrapped = transform(unwrap_x, geometry)
    try:
        pieces = split(unwrapped, LineString([(180.0, -90.0), (180.0, 90.0)])).geoms
    except Exception:
        return geometry
    wrapped = []
    for piece in pieces:
        if piece.is_empty:
            continue
        if piece.bounds[0] >= 180.0:
            piece = transform(wrap_x, piece)
        wrapped.append(piece)
    return unary_union(wrapped) if wrapped else geometry


def _normalise_land(gdf: gpd.GeoDataFrame) -> gpd.GeoDataFrame:
    required = ["province_key", "state_key"]
    missing = [name for name in required if name not in gdf.columns]
    if missing:
        raise ValueError(f"province layer missing fields: {', '.join(missing)}")
    result = gdf.copy()
    for name in ["province_key", "state_key", "country_tag", "market_key"]:
        if name not in result.columns:
            result[name] = ""
        result[name] = result[name].map(_as_text)
    # The leaf geometry is the future Location unit.  Keep the historical
    # province_key spelling as a compatibility alias while exposing explicit
    # parent keys for the Area -> Trade Province -> Location hierarchy.
    for name in ["location_key", "trade_province_key", "area_key"]:
        if name not in result.columns:
            result[name] = ""
        result[name] = result[name].map(_as_text)
    result.loc[result["location_key"] == "", "location_key"] = result["province_key"]
    result.loc[result["trade_province_key"] == "", "trade_province_key"] = result["state_key"]
    result.loc[result["trade_province_key"] == "", "trade_province_key"] = result["location_key"]
    result.loc[result["area_key"] == "", "area_key"] = result["country_tag"]
    result.loc[result["area_key"] == "", "area_key"] = "area.neutral"
    if (result["province_key"] == "").any() or (result["state_key"] == "").any():
        raise ValueError("land province_key and state_key must not be empty")
    if result["province_key"].duplicated().any():
        key = result.loc[result["province_key"].duplicated(), "province_key"].iloc[0]
        raise ValueError(f"duplicate province_key: {key}")
    result["kind"] = PROVINCE_LAND
    result["impassable"] = result.get("impassable", False).map(_as_bool) if "impassable" in result else False
    result["coastal"] = result.get("coastal", False).map(_as_bool) if "coastal" in result else False
    if "constraint_flags" not in result.columns:
        result["constraint_flags"] = 0
    result["constraint_flags"] = result["constraint_flags"].map(lambda value: int(value) if str(value).strip() else 0)
    result.loc[result["constraint_flags"] == 0, "constraint_flags"] = 1 << 5
    return result.sort_values("province_key", kind="stable").reset_index(drop=True)


def _normalise_water(gdf: gpd.GeoDataFrame, kind: str, prefix: str, start_index: int = 0) -> gpd.GeoDataFrame:
    result = gdf.copy()
    key_name = "province_key" if "province_key" in result.columns else "sea_key" if kind == "sea" and "sea_key" in result.columns else "lake_key" if "lake_key" in result.columns else None
    if key_name is None:
        result["province_key"] = [f"{prefix}_{i + start_index:05d}" for i in range(len(result))]
    else:
        result["province_key"] = result[key_name].map(_as_text)
    result["state_key"] = ""
    result["country_tag"] = ""
    result["market_key"] = ""
    result["kind"] = PROVINCE_SEA if kind == "sea" else PROVINCE_LAKE
    result["coastal"] = False
    result["impassable"] = kind == "lake"
    result["sea_start"] = result.get("sea_start", False).map(_as_bool) if "sea_start" in result else False
    result["location_key"] = result["province_key"]
    result["trade_province_key"] = "province.water"
    result["area_key"] = "area.water"
    result["constraint_flags"] = 0
    result["province_key"] = result["province_key"].map(_as_text)
    if (result["province_key"] == "").any():
        raise ValueError(f"{kind} province_key must not be empty")
    if result["province_key"].duplicated().any():
        key = result.loc[result["province_key"].duplicated(), "province_key"].iloc[0]
        raise ValueError(f"duplicate water province_key: {key}")
    return result.sort_values("province_key", kind="stable").reset_index(drop=True)


def _read_history(path: Path | None) -> tuple[list[dict], dict[str, dict], dict[str, dict]]:
    if path is None:
        return [], {}, {}
    rows: list[dict] = []
    if path.is_dir():
        candidates = sorted(path.glob("history_1836.*"))
        if not candidates:
            candidates = sorted(path.glob("*.csv"))
        if not candidates:
            raise ValueError(f"history directory has no readable history_1836 file: {path}")
        path = candidates[0]
    if path.suffix.lower() in {".csv", ".tsv"}:
        with path.open("r", encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t" if path.suffix.lower() == ".tsv" else ","))
    elif path.suffix.lower() in {".json", ".geojson", ".gpkg", ".shp"}:
        if path.suffix.lower() in {".json", ".geojson", ".gpkg", ".shp"}:
            source = gpd.read_file(path)
            rows = source.drop(columns=["geometry"], errors="ignore").to_dict("records")
    else:
        raise ValueError(f"unsupported history input: {path}")

    state_rows: dict[str, dict] = {}
    province_rows: dict[str, dict] = {}
    for raw in rows:
        row = {str(k): v for k, v in raw.items()}
        state_key = _as_text(_value(row, "state_key", "state"))
        province_key = _as_text(_value(row, "province_key", "province"))
        owner = _as_text(_value(row, "country_tag", "owner", "owner_tag", "country"))
        market = _as_text(_value(row, "market_key", "market"))
        normal = {"state_key": state_key, "province_key": province_key, "country_tag": owner, "market_key": market, **row}
        if state_key:
            state_rows[state_key] = normal
        if province_key:
            province_rows[province_key] = normal
    return rows, state_rows, province_rows


def _state_layer_rows(states: gpd.GeoDataFrame) -> dict[str, dict]:
    result: dict[str, dict] = {}
    if states.empty:
        return result
    if "state_key" not in states.columns:
        raise ValueError("states layer must contain state_key")
    for _, row in states.iterrows():
        key = _as_text(row["state_key"])
        if not key:
            continue
        result[key] = {str(k): row[k] for k in states.columns if k != "geometry"}
    return result


def _apply_state_attributes(land: gpd.GeoDataFrame, states: gpd.GeoDataFrame) -> gpd.GeoDataFrame:
    """Project state-level coastal/impassable authoring onto land provinces."""
    if states.empty:
        return land
    state_rows = _state_layer_rows(states)
    result = land.copy()
    for index, row in result.iterrows():
        state = _as_text(row.state_key)
        meta = state_rows.get(state)
        if not meta:
            continue
        if "coastal" in meta and pd.notna(meta["coastal"]):
            result.at[index, "coastal"] = _as_bool(meta["coastal"])
        raw_impassable = _value(meta, "impassable_provinces", "impassable")
        if raw_impassable:
            names = {_as_text(value) for value in str(raw_impassable).replace("|", ",").replace(";", ",").split(",")}
            if _as_text(row.province_key) in names or _as_bool(raw_impassable):
                result.at[index, "impassable"] = True
    return result


def _build_ids(land: gpd.GeoDataFrame, seas: gpd.GeoDataFrame, lakes: gpd.GeoDataFrame,
               states: gpd.GeoDataFrame, history_states: dict[str, dict], history_provinces: dict[str, dict]) -> IdTables:
    all_states = set(land["state_key"].tolist())
    if not states.empty:
        all_states.update(_as_text(x) for x in states["state_key"].tolist() if _as_text(x))
    all_states.update(key for key in history_states if key)
    state_names = sorted(all_states)

    country_values = set(x for x in land["country_tag"].tolist() if x)
    if not states.empty and "country_tag" in states.columns:
        country_values.update(_as_text(x) for x in states["country_tag"].tolist() if _as_text(x))
    country_values.update(row["country_tag"] for row in history_states.values() if row.get("country_tag"))
    country_values.update(row["country_tag"] for row in history_provinces.values() if row.get("country_tag"))
    countries = sorted(country_values)

    market_values = set(x for x in land["market_key"].tolist() if x)
    if not states.empty and "market_key" in states.columns:
        market_values.update(_as_text(x) for x in states["market_key"].tolist() if _as_text(x))
    market_values.update(row["market_key"] for row in history_states.values() if row.get("market_key"))
    market_values.update(row["market_key"] for row in history_provinces.values() if row.get("market_key"))
    markets = sorted(market_values)

    province_keys = [*land["province_key"].tolist(), *seas["province_key"].tolist(), *lakes["province_key"].tolist()]
    if len(province_keys) > 65_534:
        raise ValueError(f"province count {len(province_keys)} exceeds the 65534 R16 limit")
    if len(set(province_keys)) != len(province_keys):
        raise ValueError("province_key must be unique across land, sea and lake layers")
    return IdTables(
        countries=countries,
        country_id={key: i for i, key in enumerate(countries)},
        markets=markets,
        market_id={key: i for i, key in enumerate(markets)},
        states=state_names,
        state_id={key: i for i, key in enumerate(state_names)},
        province_id={key: i for i, key in enumerate(province_keys)},
    )


def _id_or_invalid(table: dict[str, int], key: str | None) -> int:
    if not key or key not in table:
        return 0xFFFF_FFFF
    return table[key]


def _state_metadata(land: gpd.GeoDataFrame, states: gpd.GeoDataFrame, ids: IdTables,
                    history_states: dict[str, dict], history_provinces: dict[str, dict]):
    state_rows = _state_layer_rows(states)
    metadata: dict[str, dict] = {}
    for state in ids.states:
        group = land[land["state_key"] == state]
        layer = state_rows.get(state, {})
        history = history_states.get(state, {})
        marked = group[group.get("state_capital", False).map(_as_bool)] if "state_capital" in group else group.iloc[0:0]
        capital = _as_text(_value(history, "capital_province", "capital", "province_key"))
        if not capital and "capital_province" in layer:
            capital = _as_text(layer["capital_province"])
        if not capital and len(marked) == 1:
            capital = _as_text(marked.iloc[0]["province_key"])
        if not capital and not group.empty:
            # This is a capital fallback only; it is not used to create a city
            # anchor. Authored hubs are generated separately below.
            capital = _as_text(group.sort_values("province_key").iloc[0]["province_key"])
        owner_values = [
            _as_text(_value(history, "country_tag", "owner", "owner_tag", "country")),
            _as_text(_value(layer, "country_tag", "owner", "owner_tag", "country")),
        ]
        owner_values += [_as_text(x) for x in group["country_tag"].tolist() if _as_text(x)]
        owner_values += [_as_text(_value(history_provinces.get(_as_text(row.province_key), {}),
                                         "country_tag", "owner", "owner_tag", "country"))
                         for _, row in group.iterrows()
                         if _as_text(_value(history_provinces.get(_as_text(row.province_key), {}),
                                            "country_tag", "owner", "owner_tag", "country"))]
        market_values = [
            _as_text(_value(history, "market_key", "market")),
            _as_text(_value(layer, "market_key", "market")),
        ]
        market_values += [_as_text(x) for x in group["market_key"].tolist() if _as_text(x)]
        market_values += [_as_text(_value(history_provinces.get(_as_text(row.province_key), {}),
                                          "market_key", "market"))
                          for _, row in group.iterrows()
                          if _as_text(_value(history_provinces.get(_as_text(row.province_key), {}),
                                             "market_key", "market"))]
        owners = sorted(set(x for x in owner_values if x))
        markets = sorted(set(x for x in market_values if x))
        metadata[state] = {
            "owner": owners[0] if len(owners) == 1 else "",
            "market": markets[0] if len(markets) == 1 else "",
            "capital": capital,
        }
    province_history: dict[str, dict] = {}
    land_by_key = {str(row.province_key): row for _, row in land.iterrows()}
    for key, row in land_by_key.items():
        history = history_provinces.get(key, {})
        province_history[key] = {
            "owner": _as_text(_value(history, "country_tag", "owner", "owner_tag", "country", default=_as_text(row.country_tag))),
            "market": _as_text(_value(history, "market_key", "market", default=_as_text(row.market_key))),
        }
    return metadata, province_history


def _representative_point(row):
    geometry = row.geometry
    if _as_text(row.get('representative_method', '')) != 'interior':
        return geometry.representative_point()
    pieces = list(geometry.geoms) if geometry.geom_type == 'MultiPolygon' else [geometry]
    largest = max(pieces, key=lambda g: g.area)
    # This is a geometric focus point, not an authored town coordinate.
    point = polylabel(largest, tolerance=100.0)
    if not geometry.covers(point):
        raise ValueError('interior representative is outside its Location')
    return point


def _write_definitions(out: Path, land: gpd.GeoDataFrame, water: gpd.GeoDataFrame,
                       ids: IdTables, states: gpd.GeoDataFrame,
                       history_states: dict[str, dict], history_provinces: dict[str, dict]):
    state_meta, province_history = _state_metadata(land, states, ids, history_states, history_provinces)
    state_lookup = {key: i for i, key in enumerate(ids.states)}
    rows = pd.concat([land, water], ignore_index=True)
    reps = gpd.GeoSeries([_representative_point(row) for _, row in rows.iterrows()], crs=land.crs)

    country_bytes = bytearray(struct.pack("<II", COUNTRY_MAGIC, len(ids.countries)))
    # CNT2 is identity-only. Population, GDP, treasury, tax and all other
    # country economics are supplied by mounted content scripts at bootstrap.
    for country in ids.countries:
        country_bytes += _u16_string(country)
    _write(out / "definitions/countries.bin", bytes(country_bytes))

    # A market may span multiple current owners. Do not force a single GIS
    # owner into the definition; historical state/province setup owns it.
    market_bytes = bytearray(struct.pack("<I", len(ids.markets)))
    market_bytes += b"".join(struct.pack("<I", 0xFFFF_FFFF) for _ in ids.markets)
    _write(out / "definitions/markets.bin", bytes(market_bytes))

    state_bytes = bytearray(struct.pack("<II", STATE_MAGIC, len(ids.states)))
    for state in ids.states:
        meta = state_meta[state]
        state_bytes += _u16_string(state)
        state_bytes += struct.pack("<IIII", 0xFFFF_FFFF, 0xFFFF_FFFF, _id_or_invalid(ids.province_id, meta["capital"]), 0)
        state_bytes += struct.pack("<BBH", 0, 0, 0)
    _write(out / "definitions/states.bin", bytes(state_bytes))

    province_bytes = bytearray(struct.pack("<II", PROVINCE_MAGIC, len(rows)))
    for index, row in rows.iterrows():
        key = _as_text(row.province_key)
        kind = int(row.kind)
        state_key = _as_text(row.state_key) if kind == PROVINCE_LAND else ""
        province_bytes += _u16_string(key)
        province_bytes += struct.pack("<III", _id_or_invalid(state_lookup, state_key), 0xFFFF_FFFF, 0xFFFF_FFFF)
        province_bytes += struct.pack("<ddI", float(reps.iloc[index].x), float(reps.iloc[index].y),
                                      min(max(1, int(round(row.geometry.area / 1_000_000.0))), 0xFFFF_FFFF))
        flags = (1 if _as_bool(row.coastal) else 0) | (2 if _as_bool(row.impassable) else 0)
        province_bytes += struct.pack("<BBH", kind, flags, 0)
    _write(out / "definitions/provinces.bin", bytes(province_bytes))

    history_states_wire: list[tuple[int, int, int, int]] = []
    for state in ids.states:
        meta = state_meta[state]
        history_states_wire.append((
            ids.state_id[state],
            _id_or_invalid(ids.country_id, meta["owner"]),
            _id_or_invalid(ids.market_id, meta["market"]),
            _id_or_invalid(ids.province_id, meta["capital"]),
        ))
    history_provinces_wire: list[tuple[int, int, int]] = []
    for key in land["province_key"].tolist():
        setup = province_history[str(key)]
        history_provinces_wire.append((
            ids.province_id[str(key)],
            _id_or_invalid(ids.country_id, setup["owner"]),
            _id_or_invalid(ids.market_id, setup["market"]),
        ))
    sea_rows = water[water.kind == PROVINCE_SEA]
    explicit_starts = sea_rows[sea_rows.sea_start.map(_as_bool)]
    start_rows = explicit_starts if not explicit_starts.empty else sea_rows
    sea_ids = [ids.province_id[str(key)] for key in start_rows["province_key"].tolist()]
    history_bytes = bytearray(struct.pack("<II", HISTORY_MAGIC, len(history_states_wire)))
    for record in history_states_wire:
        history_bytes += struct.pack("<IIII", *record)
    history_bytes += struct.pack("<I", len(history_provinces_wire))
    for record in history_provinces_wire:
        history_bytes += struct.pack("<III", *record)
    history_bytes += struct.pack("<I", len(sea_ids))
    for province in sea_ids:
        history_bytes += struct.pack("<I", province)
    _write(out / "definitions/history_1836.bin", bytes(history_bytes))
    return reps, state_meta, province_history


def _write_map_hierarchy(out: Path, land: gpd.GeoDataFrame, water: gpd.GeoDataFrame,
                         ids: IdTables) -> dict[str, int]:
    """Emit one explicit Area -> TradeProvince -> Location hierarchy.

    The existing ProvinceDefinitions chunk remains the stable leaf/raster
    identity used by the simulation compatibility layer.  Every leaf gets one
    Location record here, while authored parent keys allow several Locations
    to share a trade Province and several trade Provinces to share an Area.
    """
    rows = pd.concat([land, water], ignore_index=True)
    rows_by_key = {str(row.province_key): row for _, row in rows.iterrows()}

    def text(row, name, fallback):
        value = _as_text(row.get(name, "")) if hasattr(row, "get") else ""
        return value or fallback

    parent_by_location: dict[str, tuple[str, str]] = {}
    for key, row in rows_by_key.items():
        kind = int(row.kind)
        if kind == PROVINCE_LAND:
            area = text(row, "area_key", "area.neutral")
            trade = text(row, "trade_province_key", text(row, "state_key", key))
        elif kind == PROVINCE_LAKE:
            area, trade = "area.water", "province.water"
        else:
            area, trade = "area.water", "province.water"
        # A trade province has exactly one strategic parent. If source rows
        # reuse a key across areas, qualify it deterministically instead of
        # silently creating an invalid cross-area hierarchy.
        parent_by_location[key] = (area, trade)

    pairs = sorted(set(parent_by_location.values()))
    area_keys = sorted({area for area, _ in pairs})
    area_ids = {key: i for i, key in enumerate(area_keys)}
    trade_keys = [f"{area}::{trade}" for area, trade in pairs]
    trade_ids = {key: i for i, key in enumerate(trade_keys)}
    area_records = [{"key": key} for key in area_keys]
    trade_records = [{"key": key, "area": area_ids[area]}
                     for (area, trade), key in zip(pairs, trade_keys)]

    location_records = []
    for location_key, raster_id in sorted(ids.province_id.items(), key=lambda item: item[1]):
        row = rows_by_key.get(location_key)
        if row is None:
            raise ValueError(f"hierarchy cannot find raster leaf geometry: {location_key}")
        area_key, trade_key = parent_by_location[location_key]
        qualified_trade = f"{area_key}::{trade_key}"
        point = _representative_point(row)
        kind = int(row.kind)
        terrain = 0 if kind == PROVINCE_LAND else (4 if kind == PROVINCE_LAKE else 9)
        flags = int(row.get("constraint_flags", 0) or 0)
        if kind == PROVINCE_LAND and flags == 0:
            flags = 1 << 5
        location_records.append({
            "key": location_key,
            "province": trade_ids[qualified_trade],
            "area": area_ids[area_key],
            "raster": int(raster_id),
            "x": float(point.x),
            "y": float(point.y),
            "area_km2": max(1, int(round(row.geometry.area / 1_000_000.0))),
            "terrain": terrain,
            "coastal": 1 if _as_bool(row.get("coastal", False)) else 0,
            "constraint_flags": flags,
        })

    area_bytes = bytearray(struct.pack("<II", 0x32415241, len(area_records)))
    for record in area_records:
        area_bytes += _u16_string(record["key"])
    _write(out / "definitions/areas.bin", bytes(area_bytes))

    trade_bytes = bytearray(struct.pack("<II", 0x3250564D, len(trade_records)))
    for record in trade_records:
        trade_bytes += _u16_string(record["key"])
        trade_bytes += struct.pack("<I", record["area"])
    _write(out / "definitions/trade_provinces.bin", bytes(trade_bytes))

    location_bytes = bytearray(struct.pack("<II", 0x32434F4C, len(location_records)))
    for record in location_records:
        location_bytes += _u16_string(record["key"])
        location_bytes += struct.pack("<III", record["province"], record["area"], record["raster"])
        location_bytes += struct.pack("<ddI", record["x"], record["y"], record["area_km2"])
        location_bytes += struct.pack("<BBH", record["terrain"], record["coastal"], record["constraint_flags"])
    _write(out / "definitions/locations.bin", bytes(location_bytes))
    return {"area_count": len(area_records), "trade_province_count": len(trade_records),
            "location_count": len(location_records)}


def _shared_border_length(a, b, tolerance_m=0.05):
    """Recover common edges after CRS round trips without joining point contacts.

    Transforming newly inserted edge vertices can create nanometre slivers.
    GEOS `touches` then rejects a real border; use a bounded metric tolerance.
    Substantial overlap remains invalid contact, rather than becoming a road.
    """
    if a.distance(b) > tolerance_m:
        return 0.0
    if a.touches(b):
        exact = float(a.boundary.intersection(b.boundary).length)
        if exact > 1.0:
            return exact
        # One exact endpoint can make touches() true while the shared edge is
        # displaced by a submillimetre CRS error. Continue the metric check.
    overlap = a.intersection(b)
    if overlap.area > 0.0 and not overlap.buffer(-tolerance_m).is_empty:
        return 0.0
    return min(float(a.boundary.intersection(b.boundary.buffer(tolerance_m)).length),
               float(b.boundary.intersection(a.boundary.buffer(tolerance_m)).length))


def _adjacency(out: Path, all_rows: gpd.GeoDataFrame, ids: IdTables, bounds: WorldBounds,
               straits: Path | None, rivers: Path | None) -> tuple[int, int]:
    n = len(all_rows)
    edges: dict[tuple[int, int], tuple[int, int]] = {}
    sindex = all_rows.sindex

    def add(a: int, b: int, flags: int, cost: int = 256):
        if a == b or not (0 <= a < n and 0 <= b < n):
            return
        key = (min(a, b), max(a, b))
        old = edges.get(key)
        edges[key] = (flags if old is None else old[0] | flags, cost if old is None else min(old[1], cost))

    for i, geom in enumerate(all_rows.geometry):
        if geom is None or geom.is_empty:
            continue
        kind_a = int(all_rows.kind.iloc[i])
        if kind_a != PROVINCE_LAND:
            continue
        for candidate in sindex.query(box(*geom.bounds).buffer(0.05)):
            j = int(candidate)
            if j == i:
                continue
            kind_b = int(all_rows.kind.iloc[j])
            if kind_b == PROVINCE_LAKE:
                continue
            other = all_rows.geometry.iloc[j]
            if other is None or other.is_empty:
                continue
            water_contact = (kind_b == PROVINCE_SEA)
            if water_contact:
                # Ocean polygons can contain the entire world's coastline.
                # Do not buffer that geometry separately for every land leaf.
                if not geom.intersects(other) and geom.distance(other) > 0.05:
                    continue
                flags = ADJACENCY_SEA
            else:
                if _shared_border_length(geom, other) <= 1.0:
                    continue
                flags = ADJACENCY_LAND
            if _as_bool(all_rows.impassable.iloc[i]) or _as_bool(all_rows.impassable.iloc[j]):
                flags |= ADJACENCY_IMPASSABLE
            add(i, j, flags)

    # Add the pair that crosses the horizontal seam by querying shifted copies.
    # This keeps dateline topology explicit without duplicating province IDs.
    if bounds.horizontal_wrap:
        width = bounds.world[2] - bounds.world[0]
        near_left = [i for i, geom in enumerate(all_rows.geometry)
                     if int(all_rows.kind.iloc[i]) == PROVINCE_LAND and geom.bounds[0] < bounds.world[0] + CHUNK_M]
        near_right = set(i for i, geom in enumerate(all_rows.geometry)
                         if int(all_rows.kind.iloc[i]) == PROVINCE_LAND and geom.bounds[2] > bounds.world[2] - CHUNK_M)
        for i in near_left:
            shifted = translate(all_rows.geometry.iloc[i], xoff=width)
            for candidate in sindex.query(box(*shifted.bounds).buffer(0.05)):
                j = int(candidate)
                if j not in near_right:
                    continue
                other = all_rows.geometry.iloc[j]
                shared_boundary_m = _shared_border_length(shifted, other)
                if shared_boundary_m <= 1.0:
                    continue
                flags = ADJACENCY_LAND
                if _as_bool(all_rows.impassable.iloc[i]) or _as_bool(all_rows.impassable.iloc[j]):
                    flags |= ADJACENCY_IMPASSABLE
                add(i, j, flags)

    key_to_id = ids.province_id
    if straits is not None:
        with straits.open("r", encoding="utf-8-sig", newline="") as stream:
            for raw in csv.DictReader(stream):
                a_key = _as_text(_value(raw, "from", "from_province", "a", "province_a"))
                b_key = _as_text(_value(raw, "to", "to_province", "b", "province_b"))
                if a_key not in key_to_id or b_key not in key_to_id:
                    raise ValueError(f"strait references unknown province: {a_key}, {b_key}")
                labels = _as_text(_value(raw, "flags", "type", default="strait")).lower().replace("|", ",").split(",")
                flags = ADJACENCY_STRAIT
                if "canal" in labels:
                    flags |= ADJACENCY_CANAL
                if "river" in labels:
                    flags |= ADJACENCY_RIVER
                if "impassable" in labels:
                    flags |= ADJACENCY_IMPASSABLE
                cost = int(round(float(_value(raw, "base_cost", "cost", default=1.0)) * 256.0))
                add(key_to_id[a_key], key_to_id[b_key], flags, max(1, min(cost, 65_535)))

    if rivers is not None:
        # A river line crossing adjacent land provinces marks the edge as a
        # river crossing. The full line remains in RiverPolyline below.
        river_lines = gpd.read_file(rivers)
        if river_lines.crs is None:
            raise ValueError("rivers layer has no CRS")
        river_lines = river_lines.to_crs(TARGET_CRS)
        for line in river_lines.geometry:
            if line is None or line.is_empty:
                continue
            candidates = [int(idx) for idx in sindex.query(box(*line.bounds))
                          if int(all_rows.kind.iloc[int(idx)]) == PROVINCE_LAND]
            hits = [idx for idx in candidates if all_rows.geometry.iloc[idx].intersects(line)]
            for left, a in enumerate(hits):
                for b in hits[left + 1:]:
                    edge = (min(a, b), max(a, b))
                    if edge in edges:
                        add(a, b, ADJACENCY_RIVER)

    neighbors: list[list[tuple[int, int, int]]] = [[] for _ in range(n)]
    for (a, b), (flags, cost) in sorted(edges.items()):
        neighbors[a].append((b, flags, cost))
        neighbors[b].append((a, flags, cost))
    offsets = [0]
    flat = bytearray()
    for row in neighbors:
        row.sort(key=lambda item: item[0])
        for province, flags, cost in row:
            flat += struct.pack("<IHH", province, flags, cost)
        offsets.append(offsets[-1] + len(row))
    _write(out / "definitions/adjacency_offsets.bin", struct.pack("<I", len(offsets)) + struct.pack(f"<{len(offsets)}I", *offsets))
    _write(out / "definitions/adjacency_neighbors.bin", struct.pack("<I", offsets[-1]) + bytes(flat))
    return len(edges), offsets[-1]


def _placement_payload(records: list[tuple]) -> bytes:
    b = bytearray(struct.pack("<II", PLC_MAGIC, len(records)))
    for province, x, y, z, cls, flags, weight in records:
        b += struct.pack("<IHHhBBHH", province, x, y, z, cls, flags, weight, 0)
    return bytes(b)


def _anchor_payload(records: list[tuple]) -> bytes:
    b = bytearray(struct.pack("<II", ANC_MAGIC, len(records)))
    for province, x, y, z, importance, key_hash in records:
        b += struct.pack("<IHHhHQ", province, x, y, z, importance, key_hash)
    return bytes(b)


def _fnv1a64(text: str) -> int:
    value = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFF_FFFF_FFFF_FFFF
    return value


def _project_rows(rows: gpd.GeoDataFrame) -> gpd.GeoDataFrame:
    result = rows.to_crs(TARGET_CRS).copy()
    result.geometry = result.geometry.make_valid().map(_polygonal)
    return result


def _resolve_bounds(rows_wgs: gpd.GeoDataFrame, _rows_world: gpd.GeoDataFrame,
                    bounds_text: str | None, horizontal_wrap: bool, allow_polar_clip: bool) -> WorldBounds:
    if bounds_text:
        values = [float(x.strip()) for x in bounds_text.split(",")]
        if len(values) != 4:
            raise ValueError("--bounds must be min_lon,min_lat,max_lon,max_lat")
        min_lon, min_lat, max_lon, max_lat = values
    else:
        min_lon, min_lat, max_lon, max_lat = map(float, rows_wgs.total_bounds)
        margin_lon = max(0.01, (max_lon - min_lon) * 0.002)
        margin_lat = max(0.01, (max_lat - min_lat) * 0.002)
        min_lon -= margin_lon
        max_lon += margin_lon
        min_lat -= margin_lat
        max_lat += margin_lat
    if max_lon <= min_lon or max_lat <= min_lat:
        raise ValueError("world bounds must have positive width and height")
    if horizontal_wrap and not math.isclose(max_lon - min_lon, 360.0, rel_tol=0.0, abs_tol=1.0e-6):
        raise ValueError("--horizontal-wrap requires an explicit 360-degree longitude span")

    radius = 6_378_137.0
    if PROJECTION == "gall_stereographic":
        if min_lat <= -89.9 or max_lat >= 89.9:
            min_lat = max(min_lat, -89.9)
            max_lat = min(max_lat, 89.9)
        inv_sqrt2 = 1.0 / math.sqrt(2.0)
        y_scale = 1.0 + inv_sqrt2
        min_x = radius * math.radians(min_lon) * inv_sqrt2
        max_x = radius * math.radians(max_lon) * inv_sqrt2
        min_y = radius * y_scale * math.tan(math.radians(min_lat) * 0.5)
        max_y = radius * y_scale * math.tan(math.radians(max_lat) * 0.5)
    else:
        if min_lat <= -85.051129 or max_lat >= 85.051129:
            if not allow_polar_clip:
                raise ValueError("Mercator world bounds must stay inside +/-85.051129 degrees; pass --allow-polar-clip explicitly")
            min_lat = max(min_lat, -85.051129)
            max_lat = min(max_lat, 85.051129)
        min_x = radius * math.radians(min_lon)
        max_x = radius * math.radians(max_lon)
        min_y = radius * math.log(math.tan(math.pi / 4.0 + math.radians(min_lat) / 2.0))
        max_y = radius * math.log(math.tan(math.pi / 4.0 + math.radians(max_lat) / 2.0))
    # The same explicit WGS84 bounds drive both coordinate representations.
    # Do not replace the projected bounds with the un-margined source extent:
    # that would make metadata UVs disagree with page keys at the outer margin.
    return WorldBounds((min_lon, min_lat, max_lon, max_lat), (min_x, min_y, max_x, max_y), horizontal_wrap)


def _page_key(x: float, y: float, bounds: WorldBounds) -> tuple[int, int, int, int]:
    cx = math.floor((x - bounds.world[0]) / CHUNK_M)
    cy = math.floor((y - bounds.world[1]) / CHUNK_M)
    lx = int(round(x - (bounds.world[0] + cx * CHUNK_M)))
    ly = int(round(y - (bounds.world[1] + cy * CHUNK_M)))
    max_local = max(1, int(round(CHUNK_M)) - 1)
    return cx, cy, min(max(lx, 0), max_local), min(max(ly, 0), max_local)


def _state_hubs(land_world: gpd.GeoDataFrame, all_ids: IdTables, bounds: WorldBounds,
                authored: gpd.GeoDataFrame | None, allow_synthetic: bool = True) -> list[dict]:
    hubs: list[dict] = []
    if authored is not None and not authored.empty:
        points = authored.to_crs(TARGET_CRS).copy()
        if "province_key" not in points.columns:
            joined = gpd.sjoin(points, land_world[["province_key", "state_key", "geometry"]], predicate="within", how="left")
            points["province_key"] = joined["province_key"].values
            points["state_key"] = joined["state_key"].values
        for index, row in points.iterrows():
            province = _as_text(_value(row, "province_key"))
            state = _as_text(_value(row, "state_key"))
            if province not in all_ids.province_id or state not in all_ids.state_id:
                raise ValueError(f"hub {index} references unknown province/state: {province}, {state}")
            kind = _as_text(_value(row, "hub_kind", "kind", "type", default="city")).lower()
            hubs.append({"state": state, "province": province, "x": float(row.geometry.x), "y": float(row.geometry.y),
                         "kind": kind, "name": _as_text(_value(row, "key", "name", default=f"{state}_{kind}_{index}")),
                         "importance": int(float(_value(row, "importance", default=1000))), "authored": True})

    if not allow_synthetic:
        return hubs
    for state in all_ids.states:
        group = land_world[land_world.state_key == state]
        if group.empty:
            continue
        union = unary_union(group.geometry)
        base = union.representative_point()
        province_rows = {str(row.province_key): row for _, row in group.iterrows()}
        coastal_rows = group[group.coastal.map(_as_bool)]
        # Five semantic anchors per state. They are authored records in the
        # compiled layer even when a source file did not provide a point; they
        # are deliberately not province-centre city markers.
        defaults = [("city", 0, 2200), ("farm", 1, 1000), ("mine", 2, 900), ("wood", 3, 850), ("port", 4, 1200)]
        existing = {str(h["kind"]) for h in hubs if h["state"] == state}
        for kind, offset, importance in defaults:
            if kind in existing:
                continue
            point = base
            if kind == "port" and not coastal_rows.empty:
                point = coastal_rows.geometry.iloc[0].representative_point()
            else:
                # Deterministic offsets keep the hubs visually distinct while
                # retaining a point inside the state polygon.
                dx = (offset - 2) * 0.012 * CHUNK_M
                dy = ((offset * 3) % 5 - 2) * 0.009 * CHUNK_M
                candidate = Point(base.x + dx, base.y + dy)
                if union.contains(candidate):
                    point = candidate
            hit = group.sindex.query(point, predicate="within")
            if len(hit) == 0:
                distances = [point.distance(geom) for geom in group.geometry]
                province = str(group.iloc[int(np.argmin(distances))].province_key)
            else:
                province = str(group.iloc[int(hit[0])].province_key)
            hubs.append({"state": state, "province": province, "x": float(point.x), "y": float(point.y),
                         "kind": kind, "name": f"{state}_{kind}", "importance": importance, "authored": False})
    return hubs


def _hub_class(kind: str) -> int:
    return {"city": PLACEMENT_URBAN, "farm": PLACEMENT_FARM, "mine": PLACEMENT_MINE,
            "wood": PLACEMENT_VEGETATION, "port": PLACEMENT_PORT}.get(kind, PLACEMENT_BUILDABLE)


def _compile_pages(out: Path, all_world: gpd.GeoDataFrame, land_world: gpd.GeoDataFrame,
                   ids: IdTables, bounds: WorldBounds, resolution: float,
                   dem: Path | None, levels: int, hubs: list[dict], architecture: gpd.GeoDataFrame) -> tuple[list[str], dict]:
    # A page is always the runtime's 64-km spatial unit. ``--resolution`` is
    # the source/DEM sampling hint; the emitted wire remains 128 samples per
    # page and is resampled to the stable 64-km page contract below. It is not
    # a hidden validation restriction on global source data.
    if not math.isfinite(resolution) or resolution <= 0.0:
        raise ValueError("--resolution must be a positive finite source sampling size")
    base_count_x = max(1, math.ceil((bounds.world[2] - bounds.world[0]) / CHUNK_M))
    base_count_y = max(1, math.ceil((bounds.world[3] - bounds.world[1]) / CHUNK_M))
    manifest: list[str] = []
    page_count = 0
    candidate_count = 0
    represented = np.zeros(len(ids.province_id) + 1, dtype=bool)
    level_counts: list[int] = []
    water_sindex = all_world.sindex
    land_sindex = land_world.sindex
    hub_by_page: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for hub in hubs:
        cx, cy, lx, ly = _page_key(hub["x"], hub["y"], bounds)
        hub = dict(hub)
        hub["cx"], hub["cy"], hub["lx"], hub["ly"] = cx, cy, lx, ly
        hub_by_page[(cx, cy)].append(hub)

    dem_src = rasterio.open(dem) if dem else None
    dem_vrt = WarpedVRT(dem_src, crs=TARGET_CRS, resampling=Resampling.bilinear) if dem_src else None
    try:
        for level in range(levels):
            scale = 1 << level
            page_world = CHUNK_M * scale
            pixel_resolution = page_world / PAGE
            level_pages = 0
            count_x = max(1, math.ceil((bounds.world[2] - bounds.world[0]) / page_world))
            count_y = max(1, math.ceil((bounds.world[3] - bounds.world[1]) / page_world))
            for cy in range(count_y):
                for cx in range(count_x):
                    x0 = bounds.world[0] + cx * page_world
                    y0 = bounds.world[1] + cy * page_world
                    x1, y1 = min(x0 + page_world, bounds.world[2]), min(y0 + page_world, bounds.world[3])
                    # The final page keeps the full wire dimensions; its
                    # transform still uses page_world so no hidden resample is
                    # performed in the runtime.
                    page_box = box(x0, y0, x1, y1)
                    hits = sorted(
                        map(int, water_sindex.query(page_box, predicate="intersects")),
                        # Draw the sea substrate first, then land, then lakes
                        # so a global ocean rectangle never overwrites land.
                        key=lambda index: (
                            0 if int(all_world.kind.iloc[index]) == PROVINCE_SEA else
                            2 if int(all_world.kind.iloc[index]) == PROVINCE_LAKE else 1,
                            ids.province_id[str(all_world.province_key.iloc[index])]))
                    shapes = [(all_world.geometry.iloc[i], ids.province_id[str(all_world.province_key.iloc[i])] + 1) for i in hits]
                    transform = from_origin(x0, y0 + page_world, pixel_resolution, pixel_resolution)
                    province_page = rasterize(shapes, out_shape=(PAGE, PAGE), transform=transform, fill=0, dtype="uint16", all_touched=False)

                    # Expanded land mask prevents a page edge from becoming a
                    # fake coastline. The coast bundle is valid even for a
                    # fully sea page, which lets the clipmap stream uniformly.
                    margin = int(math.ceil(MAX_COAST_M / pixel_resolution)) + 2
                    ex0, ey0 = x0 - margin * pixel_resolution, y0 - margin * pixel_resolution
                    ex1, ey1 = x1 + margin * pixel_resolution, y1 + margin * pixel_resolution
                    land_hits = sorted(
                        map(int, land_sindex.query(box(ex0, ey0, ex1, ey1), predicate="intersects")),
                        key=lambda index: ids.province_id[str(land_world.province_key.iloc[index])])
                    expanded = rasterize([(land_world.geometry.iloc[i], 1) for i in land_hits], out_shape=(PAGE + 2 * margin, PAGE + 2 * margin),
                                         transform=from_origin(ex0, ey1, pixel_resolution, pixel_resolution), fill=0, dtype="uint8", all_touched=False)
                    sdf = coast_distance_jfa(expanded, pixel_resolution, MAX_COAST_M)
                    sdf = sdf[margin:margin + PAGE, margin:margin + PAGE]
                    coast_q = np.clip(np.rint(sdf / 0.5), -32767, 32767).astype("<i2")
                    if level == 0:
                        represented[np.unique(province_page)] = True
                    base = f"pages/l{level}_{cx}_{cy}"
                    _write(out / f"{base}_province_coast.bin", province_page.astype("<u2", copy=False).tobytes() + coast_q.tobytes())
                    manifest.append(f"province_coast {level} {cx} {cy} 0 {base}_province_coast.bin")
                    spatial_mask = rasterize([(land_world.geometry.iloc[i], 1) for i in land_hits], out_shape=(PAGE, PAGE),
                                              transform=transform, fill=0, dtype="uint8", all_touched=False)
                    _write(out / f"{base}_spatial_mask.bin", spatial_mask.tobytes())
                    manifest.append(f"spatial_mask {level} {cx} {cy} 0 {base}_spatial_mask.bin")
                    lake_shapes = [(all_world.geometry.iloc[i], 1) for i in hits if int(all_world.kind.iloc[i]) == PROVINCE_LAKE]
                    lake_mask = rasterize(lake_shapes, out_shape=(PAGE, PAGE), transform=transform, fill=0, dtype="uint8", all_touched=False)
                    _write(out / f"{base}_lake_mask.bin", lake_mask.tobytes())
                    manifest.append(f"lake_mask {level} {cx} {cy} 0 {base}_lake_mask.bin")

                    land_sample = rasterize([(land_world.geometry.iloc[i], 1) for i in land_hits],
                                            out_shape=(65, 65),
                                            transform=from_origin(x0, y0 + page_world,
                                                                  page_world / 64.0, page_world / 64.0),
                                            fill=0, dtype="uint8", all_touched=False).astype(bool)
                    if dem_vrt is not None:
                        target_win = rasterio.windows.from_bounds(x0, y0, x1, y1, transform=dem_vrt.transform)
                        vrt_win = Window(0, 0, dem_vrt.width, dem_vrt.height)
                        intersected = target_win.intersection(vrt_win)
                        heights = np.full((65, 65), np.nan, dtype="float64")
                        if intersected.width > 0 and intersected.height > 0:
                            col_start = max(0, min(65, int(round((intersected.col_off - target_win.col_off) / target_win.width * 65.0))))
                            col_end = max(col_start, min(65, int(round((intersected.col_off + intersected.width - target_win.col_off) / target_win.width * 65.0))))
                            row_start = max(0, min(65, int(round((intersected.row_off - target_win.row_off) / target_win.height * 65.0))))
                            row_end = max(row_start, min(65, int(round((intersected.row_off + intersected.height - target_win.row_off) / target_win.height * 65.0))))
                            h_out = row_end - row_start
                            w_out = col_end - col_start
                            if h_out > 0 and w_out > 0:
                                chunk_data = dem_vrt.read(1, window=intersected, out_shape=(h_out, w_out),
                                                          out_dtype="float64", resampling=Resampling.bilinear)
                                heights[row_start:row_end, col_start:col_end] = chunk_data
                        valid = np.isfinite(heights)
                        if dem_vrt.nodata is not None:
                            valid &= heights != float(dem_vrt.nodata)
                        # A DEM hole on land is not sea level. Fill it from the
                        # nearest valid sample so nodata cannot flatten a
                        # trench or create a false shoreline.
                        if np.any(land_sample & ~valid):
                            if not np.any(valid):
                                heights = np.zeros((65, 65), dtype="float64")
                            else:
                                nearest = ndimage.distance_transform_edt(
                                    ~valid, return_distances=False, return_indices=True)
                                filled = heights[tuple(nearest)]
                                heights[~valid] = filled[~valid]
                        heights[~np.isfinite(heights)] = 0.0
                    else:
                        heights = np.zeros((65, 65), dtype=np.float64)
                    # Bathymetry is not terrain height for the strategy map:
                    # every non-land sample is the authored sea level. The
                    # independent land mask prevents DEM ocean depth from
                    # inventing dry land during shading.
                    heights[~land_sample] = 0.0
                    q = np.clip(np.rint((heights - MIN_HEIGHT_M) / HEIGHT_STEP_M), 0, 65_535).astype("<u2")
                    _write(out / f"{base}_height.bin", q.tobytes())
                    manifest.append(f"terrain_height {level} {cx} {cy} 0 {base}_height.bin")

                    if level == 0 and CHUNK_M <= 65_535.0:
                        # Only authored semantic anchors produce placement
                        # candidates. This keeps construction positions tied to
                        # city/farm/mine/wood/port intent instead of a province
                        # centre grid.
                        anchors = []
                        placement = []
                        for hub in hub_by_page.get((cx, cy), []):
                            if hub["lx"] >= CHUNK_M or hub["ly"] >= CHUNK_M:
                                continue
                            province = ids.province_id[hub["province"]]
                            key_hash = _fnv1a64(hub["name"])
                            anchors.append((province, hub["lx"], hub["ly"], 0, max(0, min(65_535, hub["importance"])), key_hash))
                            cls = _hub_class(hub["kind"])
                            flags = FLAG_BUILDABLE | (FLAG_COASTAL if hub["kind"] == "port" else 0)
                            placement.append((province, hub["lx"], hub["ly"], 0, cls, flags, max(1, min(65_535, hub["importance"]))))
                        if anchors:
                            _write(out / f"{base}_anchors.bin", _anchor_payload(anchors))
                            manifest.append(f"settlement_anchors 0 {cx} {cy} 0 {base}_anchors.bin")
                        if placement:
                            _write(out / f"{base}_placement.bin", _placement_payload(placement))
                            manifest.append(f"placement_candidates 0 {cx} {cy} 0 {base}_placement.bin")
                            candidate_count += len(placement)
                    page_count += 1
                    level_pages += 1
            level_counts.append(level_pages)
    finally:
        if dem_vrt is not None:
            dem_vrt.close()
        if dem_src is not None:
            dem_src.close()
    # Spatial placement records use the legacy 16-bit local-coordinate wire
    # and remain on 64-km simulation chunks. A coarser global raster page is
    # still valid for map rendering; emit explicit empty placement chunks
    # rather than writing truncated coordinates into the simulation layer.
    if not any(row.startswith("settlement_anchors ") for row in manifest):
        _write(out / "pages/empty_anchors.bin", _anchor_payload([]))
        manifest.append("settlement_anchors 0 0 0 0 pages/empty_anchors.bin")
    if not any(row.startswith("placement_candidates ") for row in manifest):
        _write(out / "pages/empty_placement.bin", _placement_payload([]))
        manifest.append("placement_candidates 0 0 0 0 pages/empty_placement.bin")
    return manifest, {"pages": page_count, "level_pages": level_counts, "placement_candidates": candidate_count,
                      "unrepresented_locations": [key for key, value in ids.province_id.items() if not represented[value + 1]],
                      "base_page_count_x": base_count_x, "base_page_count_y": base_count_y}


def _read_points(path: Path | None, layer: str | None) -> gpd.GeoDataFrame:
    if path is None:
        return gpd.GeoDataFrame(columns=["geometry"], geometry="geometry", crs="EPSG:4326")
    points = gpd.read_file(path, layer=layer)
    if points.crs is None:
        raise ValueError(f"point layer has no CRS: {path}")
    points = points[points.geometry.notna() & ~points.geometry.is_empty].copy()
    points = points[points.geometry.geom_type.isin(["Point", "MultiPoint"])].to_crs(4326)
    return points.reset_index(drop=True)


def _empty_polyline_payload(magic: int) -> bytes:
    return struct.pack("<II", magic, 0)


def _polyline_payloads(path: Path | None, magic: int, bounds: WorldBounds) -> dict[tuple[int, int], bytes]:
    """Read lines, split long segments, and bucket them by 64-km chunk.

    The runtime can therefore issue one random-access request for a river or
    transport chunk next to the camera. A line segment is subdivided before
    bucketing, which is a deterministic DDA-like traversal rather than one
    giant polyline stored under the origin chunk.
    """
    records_by_chunk: dict[tuple[int, int], list[list[tuple[float, float]]]] = defaultdict(list)
    if path is None:
        return {(0, 0): _empty_polyline_payload(magic)}
    lines = gpd.read_file(path)
    if lines.crs is None:
        raise ValueError(f"line layer has no CRS: {path}")
    # Keep longitude in WGS84 until each segment has been split at the
    # horizontal seam. Projecting a 179 -> -179 line first would create a
    # 358-degree segment through the entire world.
    lines = lines.to_crs(4326)
    world_width = bounds.world[2] - bounds.world[0]
    page_count_x = max(1, math.ceil(world_width / CHUNK_M))
    radius = 6_378_137.0

    def project(lon: float, lat: float) -> tuple[float, float]:
        if PROJECTION == "gall_stereographic":
            safe_lat = max(-89.9, min(89.9, lat))
            inv_sqrt2 = 1.0 / math.sqrt(2.0)
            return (radius * math.radians(lon) * inv_sqrt2,
                    radius * (1.0 + inv_sqrt2) * math.tan(math.radians(safe_lat) * 0.5))
        else:
            safe_lat = max(-85.051129, min(85.051129, lat))
            return (radius * math.radians(lon),
                    radius * math.log(math.tan(math.pi / 4.0 + math.radians(safe_lat) / 2.0)))

    def seam_parts(start: tuple[float, float], end: tuple[float, float]):
        lon0, lat0 = start
        lon1, lat1 = end
        if not bounds.horizontal_wrap:
            return [(start, end)]
        # Unwrap the second endpoint to the shortest longitudinal arc.
        while lon1 - lon0 > 180.0:
            lon1 -= 360.0
        while lon1 - lon0 < -180.0:
            lon1 += 360.0
        left = bounds.wgs84[0]
        low = min(lon0, lon1)
        high = max(lon0, lon1)
        cuts = [left + 360.0 * k for k in range(
            math.floor((low - left) / 360.0) + 1,
            math.floor((high - left) / 360.0) + 1)]
        parameters = [0.0, 1.0]
        delta = lon1 - lon0
        if delta != 0.0:
            parameters.extend((cut - lon0) / delta for cut in cuts
                              if 0.0 < (cut - lon0) / delta < 1.0)
        parameters = sorted(set(parameters))
        parts = []
        for t0, t1 in zip(parameters, parameters[1:]):
            a_lon = lon0 + delta * t0
            b_lon = lon0 + delta * t1
            a_lat = lat0 + (lat1 - lat0) * t0
            b_lat = lat0 + (lat1 - lat0) * t1
            midpoint = (a_lon + b_lon) * 0.5
            wrap_shift = math.floor((midpoint - left) / 360.0)
            shift = 360.0 * wrap_shift
            parts.append(((a_lon - shift, a_lat), (b_lon - shift, b_lat)))
        return parts

    def add_chunked_segment(start: tuple[float, float], end: tuple[float, float]) -> None:
        x0, y0 = start
        x1, y1 = end
        if not all(math.isfinite(value) for value in (x0, y0, x1, y1)):
            return
        parameters = [0.0, 1.0]
        dx = x1 - x0
        dy = y1 - y0
        if dx != 0.0:
            first = math.floor((min(x0, x1) - bounds.world[0]) / CHUNK_M) + 1
            last = math.floor((max(x0, x1) - bounds.world[0]) / CHUNK_M)
            for index in range(first, last + 1):
                grid = bounds.world[0] + index * CHUNK_M
                t = (grid - x0) / dx
                if 0.0 < t < 1.0:
                    parameters.append(t)
        if dy != 0.0:
            first = math.floor((min(y0, y1) - bounds.world[1]) / CHUNK_M) + 1
            last = math.floor((max(y0, y1) - bounds.world[1]) / CHUNK_M)
            for index in range(first, last + 1):
                grid = bounds.world[1] + index * CHUNK_M
                t = (grid - y0) / dy
                if 0.0 < t < 1.0:
                    parameters.append(t)
        parameters = sorted(set(parameters))
        for t0, t1 in zip(parameters, parameters[1:]):
            a = (x0 + dx * t0, y0 + dy * t0)
            b = (x0 + dx * t1, y0 + dy * t1)
            midpoint_x = (a[0] + b[0]) * 0.5
            midpoint_y = (a[1] + b[1]) * 0.5
            cx = math.floor((midpoint_x - bounds.world[0]) / CHUNK_M)
            cy = math.floor((midpoint_y - bounds.world[1]) / CHUNK_M)
            if bounds.horizontal_wrap:
                cx %= page_count_x
            records_by_chunk[(int(cx), int(cy))].append([a, b])

    for geometry in lines.geometry:
        if geometry is None or geometry.is_empty:
            continue
        geometries = list(geometry.geoms) if geometry.geom_type == "MultiLineString" else [geometry]
        for line in geometries:
            coordinates = [(float(point[0]), float(point[1])) for point in line.coords]
            if len(coordinates) < 2:
                continue
            for start, end in zip(coordinates, coordinates[1:]):
                for part_start, part_end in seam_parts(start, end):
                    projected_start = project(*part_start)
                    projected_end = project(*part_end)
                    length = math.hypot(projected_end[0] - projected_start[0],
                                        projected_end[1] - projected_start[1])
                    pieces = max(1, math.ceil(length / CHUNK_M))
                    for piece in range(pieces):
                        t0 = piece / pieces
                        t1 = (piece + 1) / pieces
                        a = (projected_start[0] + (projected_end[0] - projected_start[0]) * t0,
                             projected_start[1] + (projected_end[1] - projected_start[1]) * t0)
                        b = (projected_start[0] + (projected_end[0] - projected_start[0]) * t1,
                             projected_start[1] + (projected_end[1] - projected_start[1]) * t1)
                        add_chunked_segment(a, b)
    if not records_by_chunk:
        records_by_chunk[(0, 0)] = []
    payloads: dict[tuple[int, int], bytes] = {}
    for chunk, records in sorted(records_by_chunk.items()):
        payload = bytearray(struct.pack("<II", magic, len(records)))
        for record in records:
            payload += struct.pack("<I", len(record))
            for x, y in record:
                payload += struct.pack("<dd", x, y)
        payloads[chunk] = bytes(payload)
    return payloads


def _write_static_layers(out: Path, manifest: list[str], rivers: Path | None, roads: Path | None, rails: Path | None,
                         architecture_regions: gpd.GeoDataFrame, ids: IdTables, land_world: gpd.GeoDataFrame,
                         bounds: WorldBounds, resources: Path | None) -> int:
    vector_count = 0
    for (cx, cy), payload in _polyline_payloads(rivers, 0x31564952, bounds).items():  # RIV1
        filename = f"vectors/rivers_{cx}_{cy}.bin"
        _write(out / filename, payload)
        manifest.append(f"river_polyline 0 {cx} {cy} 0 {filename}")
        vector_count += 1

    transport_paths = [path for path in (roads, rails) if path is not None]
    if not transport_paths:
        transport_paths = [None]
    for variant, path in enumerate(transport_paths):
        for (cx, cy), payload in _polyline_payloads(path, 0x31525450, bounds).items():  # PTR1
            filename = f"vectors/transport_{variant}_{cx}_{cy}.bin"
            _write(out / filename, payload)
            manifest.append(f"transport_polyline 0 {cx} {cy} {variant} {filename}")
            vector_count += 1

    # Architecture is compiled as a province assignment, not merely as a
    # catalogue of region names. Runtime can therefore select a building-kit
    # family without parsing GIS geometry or guessing from a country tag.
    architecture_records: list[tuple[int, int, int, int]] = []
    architecture_candidates: dict[int, tuple[object, str, str]] = {}
    for i, (_, row) in enumerate(architecture_regions.iterrows()):
        geometry = row.geometry
        if geometry is None or geometry.is_empty:
            continue
        region_key = _as_text(_value(row, "region_key", "key", default=f"region_{i}"))
        family = _as_text(_value(row, "architecture_family", "family", default="default"))
        if not region_key or not family:
            raise ValueError("architecture regions require non-empty region_key and family")
        architecture_candidates[int(i)] = (geometry, region_key, family)

    architecture_sindex = architecture_regions.sindex if architecture_candidates else None
    default_region = _fnv1a64("region.default")
    default_family = _fnv1a64("default")
    for _, row in land_world.iterrows():
        province_key = _as_text(row.province_key)
        state_key = _as_text(row.state_key)
        if state_key not in ids.state_id:
            raise ValueError(f"architecture assignment references unknown state: {state_key}")
        region_key_hash = default_region
        family_hash = default_family
        if architecture_sindex is not None:
            point = row.geometry.representative_point()
            hits = list(map(int, architecture_sindex.query(point, predicate="within")))
            if hits and hits[0] in architecture_candidates:
                _, region_key, family = architecture_candidates[hits[0]]
            else:
                nearest = min(architecture_candidates.values(),
                              key=lambda candidate: point.distance(candidate[0]))
                _, region_key, family = nearest
            region_key_hash = _fnv1a64(region_key)
            family_hash = _fnv1a64(family)
        architecture_records.append((ids.province_id[province_key], ids.state_id[state_key],
                                     region_key_hash, family_hash))
    architecture_bytes = bytearray(struct.pack("<II", ARCHITECTURE_MAGIC, len(architecture_records)))
    for record in architecture_records:
        architecture_bytes += struct.pack("<IIQQ", *record)
    _write(out / "architecture/regions.bin", bytes(architecture_bytes))
    manifest.append("architecture_region 0 0 0 0 architecture/regions.bin")
    vector_count += 1

    resource_by_state: dict[str, dict[str, float]] = {}
    if resources is not None:
        if resources.suffix.lower() == ".json":
            source = json.loads(resources.read_text(encoding="utf-8"))
            source_rows = source.get("states", source) if isinstance(source, dict) else source
            if not isinstance(source_rows, list):
                raise ValueError("resource JSON must contain a states list")
            for row in source_rows:
                if not isinstance(row, dict):
                    raise ValueError("resource JSON state rows must be objects")
                state = _as_text(_value(row, "state_key", "state"))
                values = row.get("resources", {})
                if state not in ids.state_id:
                    raise ValueError(f"resource distribution references unknown state: {state}")
                if state in resource_by_state:
                    raise ValueError(f"duplicate resource distribution state: {state}")
                if not isinstance(values, dict):
                    raise ValueError(f"resource distribution for {state} must be an object")
                resource_by_state[state] = {str(key): float(value) for key, value in values.items()}
        elif resources.suffix.lower() in {".csv", ".tsv"}:
            with resources.open("r", encoding="utf-8-sig", newline="") as stream:
                for row in csv.DictReader(stream, delimiter="\t" if resources.suffix.lower() == ".tsv" else ","):
                    state = _as_text(_value(row, "state_key", "state"))
                    if state not in ids.state_id:
                        raise ValueError(f"resource distribution references unknown state: {state}")
                    if state in resource_by_state:
                        raise ValueError(f"duplicate resource distribution state: {state}")
                    resource_by_state[state] = {
                        str(key): float(value) for key, value in row.items()
                        if key not in {"state_key", "state"} and value not in (None, "")
                    }
        else:
            raise ValueError(f"unsupported resource distribution input: {resources}")
        missing_states = [state for state in ids.states if state not in resource_by_state]
        if missing_states:
            raise ValueError("resource distribution is missing states: " + ", ".join(missing_states[:8]))
    else:
        for state in ids.states:
            group = land_world[land_world.state_key == state]
            resource_by_state[state] = {
                "grain": float(len(group)), "wood": float(len(group)), "iron": 0.0, "coal": 0.0
            }

    resource_bytes = bytearray(struct.pack("<II", RESOURCE_DISTRIBUTION_MAGIC, len(ids.states)))
    for state in ids.states:
        values = resource_by_state[state]
        normalised_values: list[tuple[str, float]] = []
        for key, value in values.items():
            resource_key = _as_text(key)
            numeric = float(value)
            if not resource_key or not math.isfinite(numeric) or numeric < 0.0:
                raise ValueError(f"invalid resource distribution value in state {state}: {key}")
            normalised_values.append((resource_key, numeric))
        normalised_values.sort(key=lambda item: item[0])
        resource_bytes += struct.pack("<II", ids.state_id[state], len(normalised_values))
        for key, value in normalised_values:
            resource_bytes += struct.pack("<Qd", _fnv1a64(key), value)
    _write(out / "definitions/resources.bin", bytes(resource_bytes))
    manifest.append("resource_distribution 0 0 0 0 definitions/resources.bin")
    return vector_count


def _write_map_labels(out, land, bounds):
    payload = bytearray(struct.pack("<II", 0x314c424c, len(land)))
    width = bounds.world[2] - bounds.world[0]
    height = bounds.world[3] - bounds.world[1]
    for _, row in land.sort_values("province_key").iterrows():
        point = row.geometry.representative_point()
        x = float(_value(row, "settlement_x_m", default=point.x))
        y = float(_value(row, "settlement_y_m", default=point.y))
        u = min(1.0, max(0.0, (x - bounds.world[0]) / width))
        v = min(1.0, max(0.0, (bounds.world[3] - y) / height))
        payload += struct.pack("<BBHffd", 4, 128, 2, 0.0, 1.0, float(row.geometry.area / 1e6))
        payload += _u16_string(str(row.province_key))
        payload += _u16_string(str(_value(row, "name", default=row.province_key)))
        # A short baseline is centered exactly at the authored town anchor.
        half = min(0.0001, u, 1.0 - u)
        payload += struct.pack("<ffff", u - half, v, u + half, v)
    _write(out / "definitions/map_labels.bin", bytes(payload))


def _metadata_payload(bounds: WorldBounds, ids: IdTables, pages: dict, levels: int, hub_count: int,
                      sea_count: int, lake_count: int, hierarchy: dict[str, int]) -> bytes:
    metadata = {
        "schema_version": 1,
        "projection": PROJECTION,
        "bounds_wgs84": list(bounds.wgs84),
        "bounds_world_m": list(bounds.world),
        "horizontal_wrap": bounds.horizontal_wrap,
        "page_size": PAGE,
        "base_page_world_size_m": CHUNK_M,
        "clip_levels": levels,
        "page_origin_x": 0,
        "page_origin_y": 0,
        "base_page_count_x": pages["base_page_count_x"],
        "base_page_count_y": pages["base_page_count_y"],
        "province_count": len(ids.province_id),
        "state_count": len(ids.states),
        "country_count": len(ids.countries),
        "area_count": hierarchy["area_count"],
        "trade_province_count": hierarchy["trade_province_count"],
        "location_count": hierarchy["location_count"],
        "sea_count": sea_count,
        "lake_count": lake_count,
        "authored_hub_count": hub_count,
    }
    return (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8")


def main() -> None:
    global CHUNK_M, DEFAULT_RESOLUTION_M, PROJECTION, TARGET_CRS
    parser = argparse.ArgumentParser(description="Compile GIS layers into Thunder .thunderworld source chunks")
    parser.add_argument("--provinces", type=Path, required=True)
    parser.add_argument("--layer", default=None)
    parser.add_argument("--states", type=Path)
    parser.add_argument("--seas", type=Path)
    parser.add_argument("--lakes", type=Path)
    parser.add_argument("--rivers", type=Path)
    parser.add_argument("--straits", type=Path)
    parser.add_argument("--hubs", "--settlements", dest="hubs", type=Path)
    parser.add_argument("--roads", type=Path)
    parser.add_argument("--rails", type=Path)
    parser.add_argument("--architecture-regions", type=Path)
    parser.add_argument("--history", "--history-1836", dest="history", type=Path)
    parser.add_argument("--resources", type=Path)
    parser.add_argument("--dem", type=Path)
    parser.add_argument("--source-catalog", type=Path, help="checksum-verified source lock embedded in world metadata")
    parser.add_argument("--coast-land", type=Path, help="authoritative dry-land coastline polygons")
    parser.add_argument("--modern-borders", type=Path, help="complete sovereign_key polygon partition")
    parser.add_argument("--historical-borders", type=Path, help="complete evidenced 1836 sovereign_key partition")
    parser.add_argument("--transfer-regions", type=Path, help="independent transfer_key polygon masks")
    parser.add_argument("--historical-settlements", type=Path, help="evidenced 1836 town points; required with constraints")
    parser.add_argument("--smooth-iterations", type=int, default=2, help="shared unlocked chain Chaikin passes (0-4)")
    parser.add_argument("--require-all-locations", action="store_true", help="reject leaves that disappear from finest raster (always enabled for constrained builds)")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--resolution", type=float,
                        help="source/DEM sampling hint; defaults to page-world-size / 128")
    parser.add_argument("--page-world-size-m", type=float, default=CHUNK_M,
                        help="world metres covered by one 128x128 runtime page")
    parser.add_argument("--levels", type=int, default=4)
    parser.add_argument("--bounds", help="min_lon,min_lat,max_lon,max_lat; omit for regional source bounds")
    parser.add_argument("--projection", choices=["mercator", "gall_stereographic"], default="mercator")
    parser.add_argument("--horizontal-wrap", action="store_true")
    parser.add_argument("--allow-polar-clip", action="store_true")
    args = parser.parse_args()
    constrained = any((args.coast_land, args.modern_borders, args.historical_borders,
                       args.transfer_regions, args.historical_settlements))
    if constrained and not all((args.coast_land, args.modern_borders,
                               args.historical_borders, args.historical_settlements)):
        parser.error("dual-border compilation requires --coast-land, --modern-borders, "
                     "--historical-borders and --historical-settlements")
    if constrained and args.projection != "mercator":
        parser.error("dual-border compilation requires EPSG:3857 / mercator")
    PROJECTION = args.projection
    TARGET_CRS = "+proj=gall +datum=WGS84" if PROJECTION == "gall_stereographic" else "EPSG:3857"
    if args.levels < 1 or args.levels > 16:
        raise ValueError("--levels must be between 1 and 16")
    if not math.isfinite(args.page_world_size_m) or args.page_world_size_m <= 0.0:
        raise ValueError("--page-world-size-m must be a positive finite value")
    # The page contract is carried in metadata and consumed by runtime page
    # streaming. Keep the historical 64 km default, but allow a global pack to
    # choose a coarser base page without forking the compiler or renderer.
    CHUNK_M = float(args.page_world_size_m)
    DEFAULT_RESOLUTION_M = CHUNK_M / PAGE
    if args.resolution is None:
        args.resolution = DEFAULT_RESOLUTION_M

    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    # An interrupted/rejected rebuild must not leave a usable stale manifest
    # pointing at a mixture of previous and newly written chunks.
    (out / "manifest.txt").write_text("# INCOMPLETE BUILD\n", encoding="utf-8")
    land = _normalise_land(_read_polygons(args.provinces, args.layer, "provinces"))
    states = _read_polygons(args.states, None, "states") if args.states else gpd.GeoDataFrame(columns=["geometry"], geometry="geometry", crs="EPSG:4326")
    land = _apply_state_attributes(land, states)
    seas = _normalise_water(_read_polygons(args.seas, None, "seas") if args.seas else gpd.GeoDataFrame(columns=["geometry"], geometry="geometry", crs="EPSG:4326"), "sea", "sea")
    lakes = _normalise_water(_read_polygons(args.lakes, None, "lakes") if args.lakes else gpd.GeoDataFrame(columns=["geometry"], geometry="geometry", crs="EPSG:4326"), "lake", "lake")
    if constrained:
        land = constrain_locations(
            land, _read_polygons(args.coast_land, None, "coast land"), lakes,
            _read_polygons(args.modern_borders, None, "modern borders"),
            _read_polygons(args.historical_borders, None, "1836 borders"),
            _read_polygons(args.transfer_regions, None, "transfer regions") if args.transfer_regions else None)
        land = consolidate_strategic_regions(land, require_special_states=args.horizontal_wrap)
        locked_sources = [args.coast_land, args.modern_borders, args.historical_borders,
                          args.lakes, args.transfer_regions]
        locked = unary_union([g.boundary for path in locked_sources if path is not None
                              for g in metric(gpd.read_file(path)).geometry])
        land = smooth_shared_boundaries(land, locked, args.smooth_iterations)
        land = bind_historical_settlements(land, gpd.read_file(args.historical_settlements))
        write_shared_edges(land, out / "shared_edges.json")
        land = land.to_crs(4326)
        # Existing state capitals may refer to pre-split leaf keys; definitions
        # choose a valid child when the old key no longer exists.
    if args.horizontal_wrap and seas.empty:
        raise ValueError("a horizontally wrapped world requires an explicit seas layer so ocean has ProvinceIds")
    _, history_states, history_provinces = _read_history(args.history)
    ids = _build_ids(land, seas, lakes, states, history_states, history_provinces)
    all_wgs = pd.concat([land, seas, lakes], ignore_index=True)
    all_wgs = gpd.GeoDataFrame(all_wgs, geometry="geometry", crs="EPSG:4326")
    all_world = _project_rows(all_wgs)
    land_world = _project_rows(land)
    bounds = _resolve_bounds(all_wgs, all_world, args.bounds, args.horizontal_wrap, args.allow_polar_clip)
    all_world = gpd.GeoDataFrame(all_world, geometry="geometry", crs=TARGET_CRS)
    land_world = gpd.GeoDataFrame(land_world, geometry="geometry", crs=TARGET_CRS)
    water_world = gpd.GeoDataFrame(pd.concat([seas, lakes], ignore_index=True), geometry="geometry", crs="EPSG:4326")
    water_world = _project_rows(water_world)
    reps, _, _ = _write_definitions(out, land_world, water_world, ids, states, history_states, history_provinces)
    hierarchy = _write_map_hierarchy(out, land_world, water_world, ids)
    _write_map_labels(out, land_world, bounds)
    adjacency_edges, adjacency_directed = _adjacency(out, all_world, ids, bounds, args.straits, args.rivers)
    authored = _read_points(args.hubs, None) if args.hubs else None
    if constrained:
        authored = gpd.GeoDataFrame(
            {"province_key": land_world.province_key, "state_key": land_world.state_key,
             "key": land_world.location_key, "hub_kind": "city"},
            geometry=gpd.points_from_xy(land_world.settlement_x_m, land_world.settlement_y_m), crs=TARGET_CRS)
    hubs = _state_hubs(land_world, ids, bounds, authored, allow_synthetic=not constrained)
    architecture = _read_polygons(args.architecture_regions, None, "architecture regions") if args.architecture_regions else gpd.GeoDataFrame(columns=["geometry"], geometry="geometry", crs="EPSG:4326")
    page_manifest, page_stats = _compile_pages(out, all_world, land_world, ids, bounds, args.resolution, args.dem, args.levels, hubs, _project_rows(architecture))
    if page_stats["unrepresented_locations"] and (constrained or args.require_all_locations):
        raise ValueError("finest raster lost Locations; increase resolution or revise microentity containers: " +
                         ", ".join(page_stats["unrepresented_locations"][:20]))
    manifest = [
        "# type level x y variant file",
        f"@world horizontal_wrap={'true' if bounds.horizontal_wrap else 'false'}",
        "metadata 0 0 0 0 metadata.json",
        "country_definitions 0 0 0 0 definitions/countries.bin",
        "market_definitions 0 0 0 0 definitions/markets.bin",
        "state_definitions 0 0 0 0 definitions/states.bin",
        "province_definitions 0 0 0 0 definitions/provinces.bin",
        "area_definitions 0 0 0 0 definitions/areas.bin",
        "trade_province_definitions 0 0 0 0 definitions/trade_provinces.bin",
        "location_definitions 0 0 0 0 definitions/locations.bin",
        "map_label_definitions 0 0 0 0 definitions/map_labels.bin",
        "historical_setup 0 0 0 0 definitions/history_1836.bin",
        "adjacency_offsets 0 0 0 0 definitions/adjacency_offsets.bin",
        "adjacency_neighbors 0 0 0 0 definitions/adjacency_neighbors.bin",
    ] + page_manifest
    vector_count = _write_static_layers(out, manifest, args.rivers, args.roads, args.rails,
                                        _project_rows(architecture), ids, land_world, bounds, args.resources)
    emitted_hub_count = len(hubs) if CHUNK_M <= 65_535.0 else 0
    metadata_payload = json.loads(_metadata_payload(bounds, ids, page_stats, args.levels, emitted_hub_count, len(seas), len(lakes), hierarchy))
    metadata_payload["geography_class"] = "constrained_input" if constrained else "unconstrained_reference"
    if args.source_catalog:
        from prepare_world_geography import load_catalog
        catalog, _ = load_catalog(args.source_catalog)
        metadata_payload["source_provenance"] = catalog
    _write(out / "metadata.json", (json.dumps(metadata_payload, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    manifest.append("# static metadata is emitted above; all remaining rows are streamable page/vector families")
    (out / "manifest.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")

    report = {
        "format": "Thunder GIS compiler 2.0",
        "dual_border_constraints": bool(constrained),
        "historical_partition_coverage_validated": bool(constrained),
        "projection": args.projection,
        "bounds_wgs84": bounds.wgs84,
        "bounds_world_m": bounds.world,
        "horizontal_wrap": bounds.horizontal_wrap,
        "province_count": len(ids.province_id),
        "land_count": len(land),
        "sea_count": len(seas),
        "lake_count": len(lakes),
        "country_count": len(ids.countries),
        "market_count": len(ids.markets),
        "state_count": len(ids.states),
        **hierarchy,
        "clip_levels": args.levels,
        "map_pages": page_stats["pages"],
        "unrepresented_locations": page_stats["unrepresented_locations"],
        "pages_by_level": page_stats["level_pages"],
        "placement_candidates": page_stats["placement_candidates"],
        "settlement_anchors": emitted_hub_count,
        "adjacency_edges_undirected": adjacency_edges,
        "adjacency_edges_directed": adjacency_directed,
        "vector_families": vector_count,
        "terrain_height_range_m": [MIN_HEIGHT_M, MAX_HEIGHT_M],
        "pixel_resolution_m": DEFAULT_RESOLUTION_M,
        "source_resolution_m": args.resolution,
    }
    (out / "compile_report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
