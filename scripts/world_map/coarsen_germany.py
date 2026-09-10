"""Merge same-polity neighbours to the requested city/hinterland scale.

The stored country masks are preserved. Area decisions use a European equal
area CRS, not the inflated areas of the display Mercator projection.
"""
import argparse
import json
import math
import shutil
from pathlib import Path
import geopandas as gpd
import pandas as pd
from shapely import make_valid, union_all
from refine_wiki_geography import parts
from thunder_gis_compile import _shared_border_length
from prepare_world_geography import build_provinces, build_areas, build_architecture_regions, safe_key

MIN_AREA = math.pi * 15**2
MAX_AREA = math.pi * 35**2
TARGET_AREA = 2200.0
SMALL_POLITY_AREA = MAX_AREA


def balanced_groups(frame, target_area=TARGET_AREA, maximum_area=MAX_AREA,
                    minimum_area=MIN_AREA, small_polity_area=SMALL_POLITY_AREA):
    """Return member-index groups, never crossing country or modern territory."""
    result=[]
    for (territory,owner), country in frame.groupby(['territory_code','country_tag'],sort=True):
        total=country.geometry.area.sum()/1e6
        if owner != 'country.sch' and total <= small_polity_area:
            result.append((set(country.index),'whole_small_polity'))
            continue
        groups={i:{'members':{i},'area':row.geometry.area/1e6,'bounds':row.geometry.bounds,'neighbours':set()}
                for i,row in country.iterrows()}
        order=list(country.index)
        index=country.sindex
        for i,row in country.iterrows():
            for position in index.query(row.geometry.envelope.buffer(.5)):
                j=order[int(position)]
                if j<=i:
                    continue
                if _shared_border_length(row.geometry,country.at[j,'geometry'],.5)>1:
                    groups[i]['neighbours'].add(j)
                    groups[j]['neighbours'].add(i)
        treaty_two = owner == 'country.sch'
        while True:
            best=None
            for i,a in groups.items():
                for j in a['neighbours']:
                    if j<=i or j not in groups:
                        continue
                    b=groups[j]; area=a['area']+b['area']
                    bounds=(min(a['bounds'][0],b['bounds'][0]),min(a['bounds'][1],b['bounds'][1]),
                            max(a['bounds'][2],b['bounds'][2]),max(a['bounds'][3],b['bounds'][3]))
                    if treaty_two:
                        if len(groups)<=2:
                            continue
                        target=total/2
                    else:
                        if area>maximum_area or min(a['area'],b['area'])>=target_area*.8:
                            continue
                        if math.hypot(bounds[2]-bounds[0],bounds[3]-bounds[1])>130000:
                            continue
                        target=target_area
                    box_area=(bounds[2]-bounds[0])*(bounds[3]-bounds[1])/1e6
                    score=(0 if min(a['area'],b['area'])<minimum_area else 1,
                           abs(area-target)+max(0,box_area-area)*.15,i,j)
                    if best is None or score<best[0]:
                        best=(score,i,j,bounds,area)
            if best is None:
                break
            _,i,j,bounds,area=best
            a,b=groups[i],groups.pop(j)
            a['members'] |= b['members'];a['area']=area;a['bounds']=bounds
            a['neighbours']=(a['neighbours']|b['neighbours'])-{i,j}
            for neighbour in a['neighbours']:
                groups[neighbour]['neighbours'].discard(j)
                groups[neighbour]['neighbours'].add(i)
        result.extend((g['members'],'treaty_region' if treaty_two else 'city_hinterland') for g in groups.values())
    return result


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--baseline-source',type=Path)
    args=parser.parse_args()
    if args.source.resolve()==args.out.resolve():
        raise ValueError('write consolidation to a separate source directory')
    land=gpd.read_file(args.source/'locations.gpkg')
    land['country_tag']=land.country_tag.fillna('')
    land['market_key']=land.market_key.fillna('')
    selected=land[land.territory_code=='DEU'].copy()
    if selected.empty or (selected.country_tag=='').any():
        raise ValueError('all German input Locations must already have owners')
    metric=selected.to_crs(3035)
    metric.geometry=metric.geometry.map(lambda geometry:union_all(parts(make_valid(geometry))))
    groups=balanced_groups(metric)
    hubs=gpd.read_file(args.source/'settlements.gpkg')
    hub_groups={key:group for key,group in hubs.groupby('province_key')}
    mapping={};records=[];audit=[];used=set()
    capitals={'anh':'Anhalt','bra':'Braunschweig','cob':'Coburg-Gotha','hoh':'Hohenzollern',
              'lip':'Detmold','lub':'Lübeck','mst':'Neustrelitz','nas':'Wiesbaden',
              'old':'Oldenburg','scm':'Schaumburg','scw':'Schwarzburg','wei':'Weimar','wld':'Waldeck'}
    traditional_names={'Altmarkkreis Salzwedel':'Altmark','Unstrut-Hainich-Kreis':'Unstrut-Hainich',
                       'Hochsauerlandkreis':'Hochsauerland','Märkischer Kreis':'Märkisches Sauerland',
                       'Vogtlandkreis':'Vogtland'}
    for members,mode in sorted(groups,key=lambda value:min(value[0])):
        member_rows=selected.loc[sorted(members)]
        owner=member_rows.iloc[0].country_tag
        tag=owner.removeprefix('country.')
        anchor_rows=[hub_groups[key] for key in member_rows.province_key if key in hub_groups]
        anchors=pd.concat(anchor_rows,ignore_index=True) if anchor_rows else None
        largest=metric.loc[sorted(members)].geometry.area.idxmax()
        row=selected.loc[largest].to_dict()
        name=str(anchors.sort_values('importance',ascending=False).iloc[0]['name']) if anchors is not None else str(row['name'])
        if mode=='whole_small_polity':
            name=capitals.get(tag,name)
        name=traditional_names.get(name,name)
        key='loc_deu_'+tag+'_'+safe_key(name.replace('ß','ss'),'region').replace('.','_')
        base=key;suffix=2
        while key in used:
            key=base+'_'+str(suffix);suffix+=1
        used.add(key)
        geometry=union_all(parts(make_valid(union_all(member_rows.geometry))))
        state='state_deu_'+tag
        row.update(province_key=key,location_key=key,source_id=key,name=name,geometry=geometry,
                   state_key=state,trade_province_key=state,coastal=bool(member_rows.coastal.any()),
                   representative_method='interior',source_class='same_polity_city_hinterland_consolidation')
        if anchors is not None:
            anchor=anchors.sort_values('importance',ascending=False).iloc[0].geometry
            world_anchor=gpd.GeoSeries([anchor],crs=hubs.crs).to_crs(3857).iloc[0]
            row['settlement_x_m']=float(world_anchor.x)
            row['settlement_y_m']=float(world_anchor.y)
        records.append(row)
        for previous in member_rows.province_key:
            mapping[previous]=key
        area=float(metric.loc[sorted(members)].geometry.area.sum()/1e6)
        within=bool(MIN_AREA<=area<=MAX_AREA)
        exception='' if within else ('whole_polity_integrity' if mode=='whole_small_polity' else
                                     'treaty_region_integrity' if mode=='treaty_region' else
                                     'same_owner_adjacency_or_area_limit')
        audit.append({'location':key,'name':name,'owner':owner,'members':list(member_rows.province_key),
                      'area_km2':area,'equivalent_radius_km':math.sqrt(area/math.pi),'mode':mode,
                      'within_area_target':within,'area_exception':exception})
    merged=gpd.GeoDataFrame(records,geometry='geometry',crs=4326)
    country_checks={}
    for owner,before in selected.groupby('country_tag'):
        old=union_all(before.geometry);new=union_all(merged[merged.country_tag==owner].geometry)
        error=old.symmetric_difference(new).area
        if error>1e-10:
            raise ValueError('consolidation changed a country mask: '+owner)
        country_checks[owner]=error
    output=gpd.GeoDataFrame(pd.concat([land.drop(selected.index),merged],ignore_index=True),geometry='geometry',crs=4326)
    output=output.sort_values('province_key').reset_index(drop=True)
    for i,hub in hubs.iterrows():
        if hub.province_key in mapping:
            hubs.at[i,'province_key']=mapping[hub.province_key]
            hubs.at[i,'state_key']=merged[merged.province_key==mapping[hub.province_key]].iloc[0].state_key
    args.out.mkdir(parents=True,exist_ok=True)
    for path in args.source.iterdir():
        if path.is_file() and path.suffix in {'.gpkg','.tif'}:
            shutil.copy2(path,args.out/path.name)
    output.to_file(args.out/'locations.gpkg',layer='locations',driver='GPKG',index=False)
    build_provinces(output).to_file(args.out/'provinces.gpkg',layer='provinces',driver='GPKG',index=False)
    build_areas(output).to_file(args.out/'areas.gpkg',layer='areas',driver='GPKG',index=False)
    build_architecture_regions(output).to_file(args.out/'architecture_regions.gpkg',layer='architecture_regions',driver='GPKG',index=False)
    hubs.to_file(args.out/'settlements.gpkg',layer='settlements',driver='GPKG',index=False)
    owners=dict(zip(merged.source_id,merged.country_tag))
    (args.out/'ownership_overrides.json').write_text(json.dumps({'schema_version':1,'scenario_year':1836,'owners':owners},indent=2)+'\n',encoding='utf-8')
    report={'germany_locations_before':len(selected),'germany_locations_after':len(merged),
            'minimum_area_km2':MIN_AREA,'target_area_km2':TARGET_AREA,'maximum_area_km2':MAX_AREA,
            'area_crs':'EPSG:3035 (equal area)','small_polity_maximum_km2':SMALL_POLITY_AREA,
            'whole_polities':sum(a['mode']=='whole_small_polity' for a in audit),
            'within_area_target':sum(a['within_area_target'] for a in audit),
            'country_mask_errors_degrees2':country_checks,'locations':audit,'member_mapping':mapping,
            'germany_unowned_locations':int((merged.country_tag=='').sum())}
    if args.baseline_source:
        baseline=gpd.read_file(args.baseline_source/'locations.gpkg')
        report['previous_preview_locations']=int((baseline.territory_code=='DEU').sum())
    (args.out/'consolidation_report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    provenance=json.loads((args.source/'refinement_report.json').read_text(encoding='utf-8'))
    provenance={'seed_refinement':provenance,
                'reference_url':provenance['reference_url'],
                'source_provenance':provenance['source_provenance'],
                'new_locations':list(merged.province_key),
                'removed_locations':list(selected.province_key),
                'complete_ownership_territories':['DEU'],'consolidation':report,
                'historical_border_accuracy':'Playable city-region approximation; recorded small border changes support city hinterlands. Consolidation preserves the adjusted masks.'}
    (args.out/'refinement_report.json').write_text(json.dumps(provenance,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps({key:value for key,value in report.items() if key not in {'locations','member_mapping','country_mask_errors_degrees2'}},indent=2))


if __name__=='__main__':
    main()
