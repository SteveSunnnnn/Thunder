"""Replace coarse administrative leaves with independently authored vector cells.

The supplied Wiki image is a visual reference only. Build inputs are this
project's JSON specifications, public BKG geometry and public Natural Earth.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

import geopandas as gpd
import pandas as pd
from shapely import make_valid, set_precision, union_all
from shapely.geometry import Polygon
from pyproj import Transformer

from prepare_world_geography import build_provinces, build_areas, build_architecture_regions, owner_for


def parts(geometry):
    if geometry.geom_type == 'Polygon':
        return [geometry]
    return [p for child in getattr(geometry, 'geoms', []) for p in parts(child)]


def partition(parent, districts, specifications, grid=0.01):
    # Keep the parent exterior exactly: adjacent original Locations share it.
    # Quantize district vertices once; overlay intersections stay on that edge.
    domain = make_valid(parent)
    # BKG publishes disconnected pieces (including lake islands) as separate
    # records with the same district name. Preserve every piece before clipping.
    by_name = {}
    for _, row in districts.iterrows():
        by_name.setdefault(str(row.shapeName).strip(), []).append(
            set_precision(set_precision(make_valid(row.geometry), grid), 0))
    by_name = {name: union_all(pieces)
               for name, pieces in by_name.items()}
    used = set()
    cells = []
    covered = domain.difference(domain)
    for spec in specifications:
        if used.intersection(spec['districts']):
            raise ValueError('district assigned to more than one Location')
        used.update(spec['districts'])
        missing = set(spec['districts']) - set(by_name)
        if missing:
            raise ValueError(f'missing public district geometry: {missing}')
        cell = union_all([by_name[name] for name in spec['districts']])
        cell = cell.intersection(domain).difference(covered)
        cell = union_all(parts(cell))
        if cell.is_empty or cell.area <= 0:
            raise ValueError(f'Location collapsed: {spec["key"]}')
        cells.append(cell)
        covered = union_all([covered, cell])
    # Source coast/generalisation differences belong to an adjacent city. Do
    # not drop them as tiny islands or turn terrestrial slivers into water.
    for gap in sorted(parts(domain.difference(covered)), key=lambda g: -g.area):
        scores = [gap.boundary.intersection(cell.boundary).length for cell in cells]
        if max(scores) > 0:
            owner = max(range(len(cells)), key=lambda i: (scores[i], -i))
        else:
            owner = min(range(len(cells)), key=lambda i: (gap.distance(cells[i]), i))
        cells[owner] = union_all([cells[owner], gap])
    combined = union_all(cells)
    if combined.symmetric_difference(domain).area > 1e-3:
        raise ValueError(f'refinement changed parent coverage: {combined.symmetric_difference(domain).area} m2')
    for i, a in enumerate(cells):
        if not a.is_valid:
            raise ValueError('invalid refined polygon')
        for b in cells[i+1:]:
            if a.intersection(b).area > 1e-3:
                raise ValueError('refined cells overlap')
    return cells


def ocean_coast_flags(cells, regional_ocean_boundary, tolerance_m=0.05):
    if regional_ocean_boundary.is_empty:
        return [False] * len(cells)
    shoreline_band = regional_ocean_boundary.buffer(tolerance_m)
    return [cell.boundary.intersection(shoreline_band).length > 1.0 for cell in cells]


def split_control_regions(domain, controls):
    """Independent territorial approximation from authored reference controls.

    These are geometric control points, never synthetic settlement anchors.
    Clip pairwise bisectors to the public district polygon and preserve its
    exterior. Every resulting cell has an explicit owner.
    """
    project = Transformer.from_crs(4326, 3857, always_xy=True).transform
    seeds = [project(c['lon'], c['lat']) for c in controls]
    if len(set(seeds)) != len(seeds) or any(not c.get('owner') for c in controls):
        raise ValueError('territorial controls require unique positions and explicit owners')
    xmin,ymin,xmax,ymax = domain.bounds
    cells = []
    for i,(sx,sy) in enumerate(seeds):
        ring = [(xmin-1,ymin-1),(xmax+1,ymin-1),(xmax+1,ymax+1),(xmin-1,ymax+1)]
        for j,(tx,ty) in enumerate(seeds):
            if i == j:
                continue
            nx,ny = tx-sx,ty-sy
            # Local coordinates avoid cancellation of squared world positions.
            limit = (nx*nx+ny*ny)/2
            output = []
            for a,b in zip(ring, ring[1:]+ring[:1]):
                da=(a[0]-sx)*nx+(a[1]-sy)*ny-limit
                db=(b[0]-sx)*nx+(b[1]-sy)*ny-limit
                if da <= 0:
                    output.append(a)
                if (da <= 0) != (db <= 0):
                    t=da/(da-db)
                    output.append((a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])))
            ring=output
            if not ring:
                break
        cell = union_all(parts(domain.intersection(Polygon(ring)))) if ring else Polygon()
        if cells:
            cell = cell.difference(union_all(cells))
        if cell.is_empty or cell.area <= 0:
            raise ValueError('territorial control collapsed: '+controls[i]['key'])
        cells.append(cell)
    for gap in parts(domain.difference(union_all(cells))):
        nearest = min(range(len(cells)), key=lambda i: gap.distance(cells[i]))
        cells[nearest] = union_all([cells[nearest],gap])
    if union_all(cells).symmetric_difference(domain).area > .001:
        raise ValueError('territorial controls changed district coverage')
    return cells


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('generated/world_map/source'))
    parser.add_argument('--out', type=Path, default=Path('generated/world_map/wiki_source'))
    parser.add_argument('--spec', type=Path, nargs='+', default=[Path('data/geography/wiki_southwest_germany.json')])
    parser.add_argument('--spec-directory', type=Path)
    parser.add_argument('--complete-germany', action='store_true')
    parser.add_argument('--scenario', type=Path, default=Path('data/geography/ownership_1836.json'))
    args = parser.parse_args()
    if args.spec_directory:
        args.spec = sorted(args.spec_directory.glob('*.json'))
        if not args.spec:
            raise ValueError('specification directory is empty')
    if args.source.resolve() == args.out.resolve():
        raise ValueError('refinement output must be separate from the source')
    specifications = [json.loads(path.read_text(encoding='utf-8')) for path in args.spec]
    if any(spec.get('schema_version') != 1 for spec in specifications):
        raise ValueError('unsupported refinement specification schema')
    parent_ids = [spec['replaces_source_id'] for spec in specifications]
    if len(set(parent_ids)) != len(parent_ids):
        raise ValueError('the same parent is refined twice')
    scenario = json.loads(args.scenario.read_text(encoding='utf-8'))
    if (scenario.get('schema_version') != 1 or scenario.get('scenario_year') != 1836
            or scenario.get('default_policy') != 'unassigned'):
        raise ValueError('expected an explicit, unassigned-by-default 1836 scenario')
    lock = json.loads(Path('data/geography/germany_detail.lock.json').read_text(encoding='utf-8'))
    entry = lock['sources'][0]
    detail = Path('data/geography') / entry['file']
    if hashlib.sha256(detail.read_bytes()).hexdigest() != entry['sha256']:
        raise ValueError('district source checksum mismatch')
    original = gpd.read_file(args.source / 'locations.gpkg')
    match = original[original.source_id.isin(parent_ids)]
    if len(match) != len(parent_ids):
        raise ValueError('expected exactly one Location for each coarse parent')
    districts = gpd.read_file(detail).to_crs(3857)
    ocean_boundary = union_all(gpd.read_file(args.source / 'seas.gpkg').to_crs(3857).geometry).boundary
    records = []
    overrides = {}
    region_reports = []
    for path, spec in zip(args.spec, specifications):
        parent = match[match.source_id == spec['replaces_source_id']].to_crs(3857).iloc[0]
        names = {name for item in spec['locations'] for name in item['districts']}
        selected = districts[districts.shapeName.str.strip().isin(names)]
        cells = partition(parent.geometry, selected, spec['locations'])
        expanded_items, expanded_cells = [], []
        for item, cell in zip(spec['locations'], cells):
            if item.get('territory_controls'):
                pieces = split_control_regions(cell, item['territory_controls'])
                expanded_items.extend(item['territory_controls'])
                expanded_cells.extend(pieces)
            else:
                expanded_items.append(item)
                expanded_cells.append(cell)
        cells = expanded_cells
        coast_flags = ocean_coast_flags(cells, ocean_boundary.intersection(parent.geometry.envelope.buffer(0.1)))
        child_keys = []
        for item, geometry, coastal in zip(expanded_items, cells, coast_flags):
            row = parent.to_dict()
            key = 'loc_deu_' + item['key']
            if key in overrides:
                raise ValueError('duplicate Location key across refinement regions: ' + key)
            state = 'state_deu_' + (item['owner'] or spec.get('pending_state', 'southwest_pending'))
            row.update(province_key=key, location_key=key, source_id=key,
                       state_key=state, trade_province_key=state,
                       name=item['name'], source_class='wiki_reference_independent_district_partition',
                       constraint_flags=18, coastal=coastal, representative_method='interior', geometry=geometry)
            row['country_tag'] = 'country.' + item['owner'] if item['owner'] else ''
            row['market_key'] = 'market.' + item['owner'] if item['owner'] else ''
            overrides[key] = row['country_tag']
            records.append(row)
            child_keys.append(key)
        region_reports.append({'parent_key':parent.province_key, 'source_id':parent.source_id,
            'region_name':parent['name'], 'new_locations':child_keys,
            'coastal_locations':[key for key,flag in zip(child_keys,coast_flags) if flag],
            'spec_file':path.name, 'spec_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'parent_coverage_difference_m2':union_all(cells).symmetric_difference(make_valid(parent.geometry)).area,
            'overlap_m2':sum(a.intersection(b).area for i,a in enumerate(cells) for b in cells[i+1:]),
            'accuracy':spec['accuracy']})
    refined = gpd.GeoDataFrame(records, geometry='geometry', crs=3857).to_crs(4326)
    # Projected control-line intersections can become self-touching at machine
    # precision when returned to longitude/latitude. Repair polygonal topology
    # before dissolving administrative parents; retain all area components.
    refined.geometry = refined.geometry.map(lambda geometry: union_all(parts(make_valid(geometry))))
    land = gpd.GeoDataFrame(pd.concat([original.drop(match.index), refined], ignore_index=True), geometry='geometry', crs=4326)
    for i, row in land.iterrows():
        if row.source_id in overrides:
            owner = overrides[row.source_id]
        else:
            owner = owner_for(scenario, str(row.territory_code), str(row.source_id))
        land.at[i, 'country_tag'] = owner
        land.at[i, 'market_key'] = 'market.' + owner.removeprefix('country.') if owner else ''
    land = land.sort_values('province_key').reset_index(drop=True)
    residual_links = []
    if args.complete_germany:
        # Natural Earth left nine numerical slivers totalling only a few m².
        # Attach each to its adjacent German cell, rather than retaining one
        # geographically scattered, ownerless pseudo-polity.
        metric_land = land.to_crs(3857)
        residuals = metric_land[metric_land.source_id == 'DEU.remainder']
        candidates = metric_land[(metric_land.territory_code == 'DEU') &
                                 (metric_land.source_id != 'DEU.remainder') &
                                 (metric_land.country_tag != '')]
        changed_indices = set()
        for _, residual in residuals.iterrows():
            for fragment in parts(residual.geometry):
                index = min(candidates.index, key=lambda i: fragment.distance(metric_land.at[i,'geometry']))
                metric_land.at[index,'geometry'] = union_all([metric_land.at[index,'geometry'],fragment])
                changed_indices.add(index)
                residual_links.append({'removed':residual.province_key,
                                       'target':metric_land.at[index,'province_key'], 'area_m2':fragment.area})
        # Preserve every unrelated original polygon byte-for-byte in its CRS.
        # A needless world-wide CRS round trip can damage narrow topology.
        changed = metric_land.loc[sorted(changed_indices)].to_crs(4326)
        for index, row in changed.iterrows():
            land.at[index,'geometry'] = union_all(parts(make_valid(row.geometry)))
        land = land.drop(residuals.index).sort_values('province_key').reset_index(drop=True)
        missing = land[(land.territory_code == 'DEU') & (land.country_tag == '')]
        if not missing.empty:
            raise ValueError('Germany must have complete ownership: '+', '.join(missing.source_id))
    args.out.mkdir(parents=True, exist_ok=True)
    for path in args.source.iterdir():
        if path.is_file() and path.suffix in {'.gpkg', '.tif'}:
            shutil.copy2(path, args.out / path.name)
    land.to_file(args.out / 'locations.gpkg', layer='locations', driver='GPKG', index=False)
    build_provinces(land).to_file(args.out / 'provinces.gpkg', layer='provinces', driver='GPKG', index=False)
    build_areas(land).to_file(args.out / 'areas.gpkg', layer='areas', driver='GPKG', index=False)
    build_architecture_regions(land).to_file(args.out / 'architecture_regions.gpkg', layer='architecture_regions', driver='GPKG', index=False)
    hubs = gpd.read_file(args.source / 'settlements.gpkg')
    for i, hub in hubs[hubs.province_key.isin(match.province_key)].iterrows():
        hits = refined[refined.geometry.covers(hub.geometry)]
        if hits.empty:
            raise ValueError('an existing settlement lost its Location during refinement')
        hits = hits.sort_values('province_key')
        hubs.at[i, 'province_key'] = hits.iloc[0].province_key
        hubs.at[i, 'state_key'] = hits.iloc[0].state_key
    hubs.to_file(args.out / 'settlements.gpkg', layer='settlements', driver='GPKG', index=False)
    (args.out / 'ownership_overrides.json').write_text(json.dumps({'schema_version':1, 'scenario_year':1836, 'owners':overrides}, indent=2)+'\n', encoding='utf-8')
    report = {'removed_locations': list(match.province_key), 'new_locations': list(refined.province_key),
              'reference_url':specifications[0]['reference_url'], 'reference_image':specifications[0]['reference_image'],
              'regions':region_reports,
              'land_count_before':len(original), 'land_count_after':len(land),
              'parent_coverage_difference_m2':sum(r['parent_coverage_difference_m2'] for r in region_reports),
              'overlap_m2':sum(r['overlap_m2'] for r in region_reports),
              'overlay_area_tolerance_m2':1e-3, 'precision_grid_m':0.01,
              'historical_border_accuracy':'Independent district reconstruction; see per-region accuracy notes.', 'source_provenance':lock,
              'complete_ownership_territories':['DEU'] if args.complete_germany else [],
              'residual_absorption':residual_links,
              'ownership_pending': [k for k,v in overrides.items() if not v]}
    (args.out / 'refinement_report.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
