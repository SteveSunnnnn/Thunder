"""Author playable city hinterlands before area-balanced consolidation.

Small historical border deviations are deliberate and recorded. German land
coverage and named independent city cores are preserved.
"""
import argparse
import json
import shutil
from pathlib import Path
import geopandas as gpd
import pandas as pd
from shapely import make_valid, union_all
from shapely.geometry import box
from refine_wiki_geography import parts, ocean_coast_flags
from prepare_world_geography import build_provinces, build_areas, build_architecture_regions


def polygonal(geometry):
    return union_all(parts(make_valid(geometry)))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    land=gpd.read_file(args.source/'locations.gpkg')
    german=land[land.territory_code=='DEU'].copy().to_crs(3035)
    german.geometry=german.geometry.map(polygonal)
    german=german.set_index('province_key',drop=False)
    original_mask=union_all(german.geometry)
    transfers=[]
    merges=[
        ('loc_deu_biedenkopf','loc_deu_marburg','Marburg'),
        ('loc_deu_pinneberg','location.deu.1578','Hamburg'),
        ('loc_deu_rinteln','loc_deu_bueckeburg','Schaumburg'),
        ('loc_deu_schmalkalden','loc_deu_gotha','Gotha'),
        ('loc_deu_suhl','loc_deu_meiningen','Meiningen'),
        ('loc_deu_wetzlar','loc_deu_giessen','Gießen-Wetzlar'),
        ('loc_deu_birkenfeld','loc_deu_idar','Birkenfeld')]
    for source,target,name in merges:
        a,b=german.loc[source],german.loc[target]
        if a.geometry.distance(b.geometry)>1.0:
            raise ValueError('city hinterland merge is not adjacent: '+source+' / '+target)
        transfers.append({'kind':'small_fragment_merge','source':source,'target':target,
                          'previous_owner':a.country_tag,'owner':b.country_tag,
                          'area_km2':a.geometry.area/1e6,'name':name})
        german.at[target,'geometry']=polygonal(union_all([a.geometry,b.geometry]))
        german.at[target,'name']=name
        german=german.drop(source)

    # A recognisable city remains a city Location; its near hinterland brings
    # it to a useful map scale instead of deleting the city or leaving a speck.
    cities=['loc_deu_frankfurt','location.deu.1575','loc_deu_lubeck','loc_deu_bueckeburg']
    city_target=720e6
    for key in cities:
        core=german.at[key,'geometry']
        if core.area>=city_target:
            continue
        low,high=0.0,30000.0
        protected=union_all([german.at[other,'geometry'] for other in cities if other!=key])
        available=original_mask.difference(protected)
        for _ in range(28):
            radius=(low+high)/2
            grown=core.buffer(radius,quad_segs=12).intersection(available)
            if grown.area<city_target: low=radius
            else: high=radius
        grown=polygonal(core.buffer(high,quad_segs=12).intersection(available))
        additions=grown.difference(core)
        for other in list(german.index):
            if other==key or not german.at[other,'geometry'].intersects(additions):
                continue
            old=german.at[other,'geometry']
            amount=old.intersection(additions).area
            if amount<=0: continue
            remainder=polygonal(old.difference(grown))
            transfers.append({'kind':'city_hinterland','source':other,'target':key,
                              'previous_owner':german.at[other,'country_tag'],
                              'owner':german.at[key,'country_tag'],'area_km2':amount/1e6})
            if remainder.is_empty: german=german.drop(other)
            else: german.at[other,'geometry']=remainder
        german.at[key,'geometry']=grown
    coverage=union_all(german.geometry)
    if coverage.symmetric_difference(original_mask).area>1.0:
        raise ValueError('city authoring changed the overall land mask')
    sea=gpd.read_file(args.source/'seas.gpkg').to_crs(4326)
    sea.geometry=sea.geometry.intersection(box(5,46,16,56))
    coast=union_all(sea.to_crs(3035).geometry).boundary
    german['coastal']=ocean_coast_flags(list(german.geometry),coast,.5)
    german['representative_method']='interior'
    german['source_class']='playable_city_and_traditional_region'
    german=german.to_crs(4326)
    german.geometry=german.geometry.map(polygonal)
    output=gpd.GeoDataFrame(pd.concat([land[land.territory_code!='DEU'],german],ignore_index=True),geometry='geometry',crs=4326)
    output=output.sort_values('province_key').reset_index(drop=True)
    hubs=gpd.read_file(args.source/'settlements.gpkg')
    old_keys=set(land[land.territory_code=='DEU'].province_key)
    for i,hub in hubs[hubs.province_key.isin(old_keys)].iterrows():
        hits=german[german.geometry.covers(hub.geometry)]
        if hits.empty:
            raise ValueError('a real settlement lost its city Location')
        row=hits.sort_index().iloc[0]
        hubs.at[i,'province_key']=row.province_key
        hubs.at[i,'state_key']=row.state_key
    args.out.mkdir(parents=True,exist_ok=True)
    for path in args.source.iterdir():
        if path.is_file() and path.suffix in {'.gpkg','.tif'}: shutil.copy2(path,args.out/path.name)
    output.to_file(args.out/'locations.gpkg',layer='locations',driver='GPKG',index=False)
    build_provinces(output).to_file(args.out/'provinces.gpkg',layer='provinces',driver='GPKG',index=False)
    build_areas(output).to_file(args.out/'areas.gpkg',layer='areas',driver='GPKG',index=False)
    build_architecture_regions(output).to_file(args.out/'architecture_regions.gpkg',layer='architecture_regions',driver='GPKG',index=False)
    hubs.to_file(args.out/'settlements.gpkg',layer='settlements',driver='GPKG',index=False)
    report={'policy':'city and traditional region names take precedence over tiny historical boundary fragments',
            'city_hinterland_target_km2':720,'transfers':transfers,
            'total_transferred_area_km2':sum(t['area_km2'] for t in transfers),
            'land_mask_difference_m2':coverage.symmetric_difference(original_mask).area}
    provenance=json.loads((args.source/'refinement_report.json').read_text(encoding='utf-8'))
    provenance.update(playable_city_adjustments=report,complete_ownership_territories=['DEU'])
    (args.out/'refinement_report.json').write_text(json.dumps(provenance,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    (args.out/'city_adjustments.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='transfers'},indent=2))


if __name__=='__main__': main()
