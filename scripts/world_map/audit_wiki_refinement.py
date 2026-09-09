"""Audit rebuilt CSR against independent source polygons and draw a GIS review."""
import argparse
import json
import struct
from pathlib import Path

import geopandas as gpd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
from shapely import union_all

from compile_scenario import province_keys
from thunder_gis_compile import _shared_border_length


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('generated/world_map/wiki_source'))
    parser.add_argument('--chunks', type=Path, default=Path('generated/world_map/wiki_chunks'))
    parser.add_argument('--out', type=Path, default=Path('generated/world_map/wiki_review'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    refinement = json.loads((args.source / 'refinement_report.json').read_text(encoding='utf-8'))
    before = gpd.read_file('generated/world_map/source/locations.gpkg').to_crs(3857)
    after = gpd.read_file(args.source / 'locations.gpkg').to_crs(3857)
    parents = before[before.province_key.isin(refinement['removed_locations'])]
    children = after[after.province_key.isin(refinement['new_locations'])].copy()
    children['country_tag'] = children.country_tag.fillna('')
    replacement = {r['parent_key']: r['new_locations'] for r in refinement.get('regions', [])}
    if not replacement and len(parents) == 1:
        replacement[parents.iloc[0].province_key] = list(children.province_key)
    for entry in refinement.get('residual_absorption', []):
        targets = replacement.setdefault(entry['removed'], [])
        if entry['target'] not in targets:
            targets.append(entry['target'])
    residual_keys = {entry['removed'] for entry in refinement.get('residual_absorption', [])}
    residual_geometry = union_all(before[before.province_key.isin(residual_keys)].geometry)
    written_coverage_error = 0.0
    for _, parent in parents.iterrows():
        group = children[children.province_key.isin(replacement[parent.province_key])]
        error = union_all(group.geometry).symmetric_difference(parent.geometry).area
        combined = union_all(group.geometry)
        # Inverse-projected diagonal edge chords can differ by centimetres.
        # Keep the raw area discrepancy in the report; enforce a 25 cm spatial
        # tolerance, far below the public source's positional resolution.
        outside_tolerance = (combined.difference(parent.geometry.buffer(.25)).difference(residual_geometry.buffer(.25)).area +
                             parent.geometry.difference(combined.buffer(.25)).area)
        if outside_tolerance > 1e-3:
            raise ValueError(f'written geometry changed parent coverage beyond 25 cm: {parent.province_key}: {outside_tolerance}')
        written_coverage_error += error
    complete_territories = refinement.get('complete_ownership_territories', [])
    ownership_audit = {}
    for territory in complete_territories:
        group = after[after.territory_code == territory]
        unowned = int((group.country_tag.fillna('') == '').sum())
        if group.empty or unowned:
            raise ValueError('required complete territory has unowned land: '+territory)
        ownership_audit[territory] = {'land_locations':len(group), 'unowned_locations':unowned}
    keys = province_keys((args.chunks / 'definitions/provinces.bin').read_bytes())
    ids = {key: i for i, key in enumerate(keys)}
    def leaf_flags(filename):
        payload = (args.chunks / 'definitions' / filename).read_bytes()
        _, records = struct.unpack_from('<II', payload)
        cursor = 8
        result = {}
        for _ in range(records):
            size, = struct.unpack_from('<H', payload, cursor)
            cursor += 2
            key = payload[cursor:cursor+size].decode('utf-8')
            cursor += size
            result[key] = payload[cursor+33]
            cursor += 36
        if cursor != len(payload):
            raise ValueError('invalid leaf definition stride')
        return result
    province_flags, location_coastal = leaf_flags('provinces.bin'), leaf_flags('locations.bin')
    for _, row in children.iterrows():
        if ((province_flags[row.province_key] & 1) != int(bool(row.coastal))
                or location_coastal[row.province_key] != int(bool(row.coastal))):
            raise ValueError('coastal flag changed between source and runtime: ' + row.province_key)
    offset_data = (args.chunks / 'definitions/adjacency_offsets.bin').read_bytes()
    count, = struct.unpack_from('<I', offset_data)
    offsets = struct.unpack_from(f'<{count}I', offset_data, 4)
    neighbor_data = (args.chunks / 'definitions/adjacency_neighbors.bin').read_bytes()

    def connected(a, b):
        source, target = ids[a], ids[b]
        return any(struct.unpack_from('<I', neighbor_data, 4 + index * 8)[0] == target
                   for index in range(offsets[source], offsets[source + 1]))

    internal = []
    for i, a in children.iterrows():
        for j, b in children.iterrows():
            if j <= i or _shared_border_length(a.geometry, b.geometry) <= 1:
                continue
            if not connected(a.province_key, b.province_key) or not connected(b.province_key, a.province_key):
                raise ValueError(f'missing internal CSR border: {a.province_key}, {b.province_key}')
            internal.append([a.province_key, b.province_key])
    external = []
    for _, parent in parents.iterrows():
        for index in before.sindex.query(parent.geometry):
            old_neighbor = before.iloc[index]
            if old_neighbor.source_id == parent.source_id:
                continue
            if _shared_border_length(parent.geometry, old_neighbor.geometry) <= 1:
                continue
            targets = replacement.get(old_neighbor.province_key, [old_neighbor.province_key])
            matches = [[a,b] for a in replacement[parent.province_key] for b in targets
                       if connected(a,b) and connected(b,a)]
            if not matches:
                raise ValueError(f'parent lost external neighbor: {old_neighbor.province_key}')
            external.append({'parent':parent.province_key,'neighbor':old_neighbor.province_key,'links':matches})
    report = {'new_location_count':len(children), 'internal_borders_checked':len(internal),
              'written_geometry_coverage_error_m2':written_coverage_error,
              'geometry_boundary_tolerance_m':.25,
              'complete_ownership':ownership_audit,
              'coastal_flags_verified':len(children),
              'preserved_external_neighbors':len(external), 'external_connections':external,
              'all_checked_csr_links_symmetric':True,
              'parent_regions':list(parents['name']),
              'scope':'authored German refinement regions; this does not certify global historical accuracy'}
    (args.out / 'topology_audit.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')

    # Public vector geometry only; no pixels from the reference image are used.
    scenario = json.loads(Path('data/geography/ownership_1836.json').read_text(encoding='utf-8'))
    colors = {'country.'+key: '#'+''.join(f'{c:02x}' for c in value.get('color',[180,180,180]))
              for key,value in scenario['countries'].items()}
    colors[''] = '#e5e5df'
    fig, axes = plt.subplots(1,2,figsize=(14,11),facecolor='#f7f5ef')
    bounds = parents.total_bounds
    margin = 25000
    context = before.cx[bounds[0]-margin:bounds[2]+margin,bounds[1]-margin:bounds[3]+margin]
    for ax in axes:
        ax.set_facecolor('#f7f5ef')
        context.plot(ax=ax,facecolor='#eeece5',edgecolor='#c8c5ba',linewidth=0.45)
        ax.set_xlim(bounds[0]-margin,bounds[2]+margin)
        ax.set_ylim(bounds[1]-margin,bounds[3]+margin)
        ax.set_axis_off()
    parents.plot(ax=axes[0],facecolor='#d9d5c9',edgecolor='#55564e',linewidth=1.2)
    axes[0].set_title(f'Before: {len(parents)} coarse Locations',fontsize=15,loc='left')
    for owner, group in children.groupby('country_tag',dropna=False):
        group.plot(ax=axes[1],facecolor=colors.get(owner,'#e5e5df'),edgecolor='#484a43',
                   linewidth=0.65,hatch='///' if not owner else None)
    for _, row in children.iterrows():
        point = row.geometry.representative_point()
        label = row['name']
        if label in {'Karlsruhe','Freiburg','Stuttgart','Oldenburg','Hannover','Braunschweig','Frankfurt','Kassel','Darmstadt','Wiesbaden','Trier','Mainz','Kaiserslautern'}:
            offset = {'Wiesbaden':(-28,18),'Frankfurt':(28,10),
                      'Mainz':(-20,-7),'Darmstadt':(25,-7)}.get(label,(0,0))
            axes[1].annotate(label,(point.x,point.y),fontsize=7,ha='center',va='center',
                             xytext=offset,textcoords='offset points',
                             arrowprops={'arrowstyle':'-','color':'#5d6057','lw':.6} if offset != (0,0) else None,
                             bbox={'facecolor':'#ffffffbb','edgecolor':'none','pad':1})
    axes[1].set_title(f'After: {len(children)} authored Locations',fontsize=15,loc='left')
    fig.suptitle('German regions | geometry and ownership review',fontsize=20,x=.05,ha='left')
    owners = sorted(set(children.country_tag))
    fig.legend(handles=[Patch(facecolor=colors[owner],label=scenario['countries'].get(owner.removeprefix('country.'),{}).get('name','Pending'),edgecolor='#555')
                        for owner in owners],
               loc='lower center',ncol=5,frameon=False,bbox_to_anchor=(.5,.05),fontsize=9)
    fig.text(.05,.025,'Public BKG / geoBoundaries district geometry. Historical boundaries remain approximate.\n'
             'Geometry source: © GeoBasis-DE / BKG 2021, dl-de/by-2-0; Natural Earth public domain.',fontsize=9,color='#55584f')
    fig.subplots_adjust(left=.03,right=.98,bottom=.16,top=.9,wspace=.08)
    fig.savefig(args.out / 'location_comparison.png',dpi=160)
    plt.close(fig)
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
