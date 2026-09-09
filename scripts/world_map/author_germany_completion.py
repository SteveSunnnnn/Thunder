"""Write independently authored V3-reference territory specifications.

Public districts supply geometry. Named geographic controls approximate the
mixed historical districts; they are not game province geometry or town assets.
"""
import json
from pathlib import Path
import geopandas as gpd
from prepare_world_geography import safe_key

ROOT = Path('data/geography')
OUT = ROOT / 'germany_complete'


def control(key, owner, lon, lat, name=None):
    return {'key':key, 'name':name or key.replace('_',' ').title(),
            'owner':owner, 'lon':lon, 'lat':lat}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    edits = {
        'main_tauber':[control('tauberbischofsheim','bad',9.663,49.625),control('mergentheim','wur',9.773,49.493)],
        'schwarzwald_baar':[control('villingen','bad',8.461,48.061),control('schwenningen','wur',8.535,48.060)],
        'bodensee':[control('ueberlingen','bad',9.19,47.75),control('friedrichshafen','wur',9.49,47.66)],
        'sigmaringen':[control('sigmaringen','hoh',9.22,48.088),control('saulgau','wur',9.50,48.017),control('pfullendorf','bad',9.251,47.923)],
        'balingen':[control('balingen','wur',8.85,48.275),control('hechingen','hoh',8.965,48.352)],
        'pforzheim':[control('pforzheim','bad',8.704,48.894),control('muehlacker','wur',8.853,48.945)],
        'goslar':[control('goslar','han',10.425,51.905),control('harzburg','bra',10.556,51.879),control('seesen','bra',10.182,51.893)],
        'salzgitter':[control('lebenstedt','bra',10.335,52.16),control('ringelheim','han',10.315,52.04)],
        'holzminden':[control('holzminden','bra',9.454,51.827),control('bodenwerder','han',9.516,51.975)],
        'hameln_pyrmont':[control('hameln','han',9.357,52.104),control('pyrmont','wld',9.255,51.985)],
        'schaumburg':[control('bueckeburg','scm',9.08,52.30),control('rinteln','hek',9.084,52.186)],
        'hochtaunus':[control('usingen','nas',8.535,50.335),control('homburg','hes',8.611,50.227)],
        'lahn_dill':[control('wetzlar','pru',8.504,50.555),control('dillenburg','nas',8.287,50.742)],
        'marburg_biedenkopf':[control('marburg','hek',8.772,50.808),control('biedenkopf','hes',8.525,50.913)],
        'waldeck_frankenberg':[control('korbach','wld',8.875,51.273),control('frankenberg','hek',8.850,51.061)],
        'birkenfeld':[control('birkenfeld','old',7.169,49.649),control('idar','pru',7.306,49.704),control('baumholder','pru',7.334,49.614)]
    }
    for filename in ['wiki_southwest_germany.json','wiki_lower_saxony.json','wiki_hesse.json','wiki_rhineland_palatinate.json']:
        spec=json.loads((ROOT/filename).read_text(encoding='utf-8'))
        for item in spec['locations']:
            if item['key'] in edits:
                item['territory_controls']=edits[item['key']]
            elif item['key']=='main_taunus':
                item['owner']='nas'
            if not item['owner'] and not item.get('territory_controls'):
                raise ValueError('unresolved Germany Location: '+item['key'])
        spec['accuracy']='Complete owner assignment with independently authored control-based approximations inside mixed districts. Exact historical border surveying remains a separate accuracy task.'
        (OUT/filename).write_text(json.dumps(spec,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

    # District membership is read only from the locked public geometry.
    districts=gpd.read_file(ROOT/'raw/germany_districts.geojson')
    original=gpd.read_file('generated/world_map/source/locations.gpkg')

    def region(source_id, filename, owners, groups=(), controls=None):
        parent=original[original.source_id==source_id].iloc[0]
        names=sorted(set(districts[districts.geometry.representative_point().within(parent.geometry)].shapeName))
        items=[]; used=set()
        for key,name,owner,members in groups:
            if set(members)-set(names):
                raise ValueError('group contains a district outside its parent')
            used.update(members)
            items.append({'key':key,'name':name,'owner':owner,'districts':members})
        for name in names:
            if name in used:
                continue
            owner=owners[name] if isinstance(owners,dict) else owners
            key=safe_key(name.split(',')[0],source_id).replace('.','_')
            items.append({'key':key,'name':name.split(',')[0], 'owner':owner,'districts':[name]})
        for item in items:
            if controls and item['key'] in controls:
                item['territory_controls']=controls[item['key']]
        if controls and set(controls)-{item['key'] for item in items}:
            raise ValueError('unmatched territorial controls: '+str(set(controls)-{item['key'] for item in items}))
        spec={'schema_version':1,'reference_url':'https://vic3.paradoxwikis.com/Victoria_3_Wiki',
              'reference_image':'user-supplied Countries.png and read-only V3 1836 state-owner review',
              'replaces_source_id':source_id,
              'accuracy':'Independent district and geographic-control reconstruction of V3 opening polities; complete ownership, approximate fine boundaries.',
              'locations':items}
        (OUT/filename).write_text(json.dumps(spec,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

    region('DEU-1579','wiki_schleswig_holstein.json',{
        'Dithmarschen':'hol','Herzogtum Lauenburg':'hol','Lübeck, Kreisfreie Stadt':'lub',
        'Nordfriesland':'sch','Ostholstein':'hol','Pinneberg':'hol',
        'Rendsburg-Eckernförde':'hol','Steinburg':'hol','Stormarn':'hol'},
        [('flensburg','Flensburg','sch',['Flensburg, Kreisfreie Stadt','Schleswig-Flensburg']),
         ('kiel','Kiel','hol',['Kiel, Kreisfreie Stadt','Plön']),
         ('segeberg','Segeberg','hol',['Neumünster, Kreisfreie Stadt','Segeberg'])],
        {'rendsburg_eckernforde':[control('eckernfoerde','sch',9.84,54.47),control('rendsburg','hol',9.66,54.30)]})
    region('DEU-3488','wiki_mecklenburg.json',{'Nordwestmecklenburg':'mec','Mecklenburgische Seenplatte':'mst',
        'Vorpommern-Greifswald':'pru','Vorpommern-Rügen':'pru'},
        [('rostock','Rostock','mec',['Rostock, Kreisfreie Stadt','Landkreis Rostock']),
         ('schwerin','Schwerin','mec',['Schwerin, Kreisfreie Stadt','Ludwigslust-Parchim'])],
        {'nordwestmecklenburg':[control('wismar','mec',11.465,53.89),control('schoenberg','mst',10.935,53.85)],
         'mecklenburgische_seenplatte':[control('waren','mec',12.687,53.522),control('neustrelitz','mst',13.064,53.362),control('neubrandenburg','mst',13.262,53.557)]})
    region('DEU-1600','wiki_anhalt.json','pru',
        [('magdeburg','Magdeburg','pru',['Magdeburg, Kreisfreie Stadt','Börde']),
         ('halle','Halle','pru',['Halle (Saale), Kreisfreie Stadt','Saalekreis']),
         ('dessau_bitterfeld','Dessau-Bitterfeld','anh',['Dessau-Roßlau, Kreisfreie Stadt','Anhalt-Bitterfeld'])],
        {'dessau_bitterfeld':[control('dessau','anh',12.245,51.831),control('bitterfeld','pru',12.325,51.626)],
         'salzlandkreis':[control('bernburg','anh',11.735,51.799),control('schoenebeck','pru',11.741,52.02)],
         'harz':[control('blankenburg','bra',10.962,51.791),control('halberstadt','pru',11.055,51.895),control('quedlinburg','pru',11.152,51.785)]})
    region('DEU-1577','wiki_thuringia.json',{
        'Altenburger Land':'mei','Eichsfeld':'pru','Erfurt, Kreisfreie Stadt':'pru','Gotha':'cob',
        'Ilm-Kreis':'scw','Kyffhäuserkreis':'pru','Nordhausen':'pru','Saale-Orla-Kreis':'mei',
        'Saalfeld-Rudolstadt':'scw','Schmalkalden-Meiningen':'mei','Sonneberg':'mei',
        'Sömmerda':'pru','Unstrut-Hainich-Kreis':'pru'},
        [('eisenach','Eisenach','wei',['Eisenach, Kreisfreie Stadt','Wartburgkreis']),
         ('gera','Gera','mei',['Gera, Kreisfreie Stadt','Greiz']),
         ('weimar','Weimar','wei',['Weimar, Kreisfreie Stadt','Weimarer Land']),
         ('jena','Jena','wei',['Jena, Kreisfreie Stadt','Saale-Holzland-Kreis']),
         ('suhl_hildburghausen','Suhl-Hildburghausen','mei',['Suhl, Kreisfreie Stadt','Hildburghausen'])],
        {'ilm_kreis':[control('arnstadt','scw',10.949,50.834),control('ilmenau','wei',10.915,50.686)],
         'saalfeld_rudolstadt':[control('rudolstadt','scw',11.329,50.721),control('saalfeld','mei',11.359,50.649)],
         'schmalkalden_meiningen':[control('schmalkalden','hek',10.45,50.72),control('meiningen','mei',10.41,50.57)],
         'suhl_hildburghausen':[control('suhl','pru',10.72,50.56),control('hildburghausen','mei',10.728,50.426)]})
    # The Lippe exception is a polity, not a Prussian administrative district.
    parent=original[original.source_id=='DEU-1572'].iloc[0]
    names=set(districts[districts.geometry.representative_point().within(parent.geometry)].shapeName)
    owners={name:('lip' if name=='Lippe' else 'pru') for name in names}
    region('DEU-1572','wiki_westphalia.json',owners,
        [('duisburg','Duisburg','pru',['Duisburg, Kreisfreie Stadt','Oberhausen, Kreisfreie Stadt','Mülheim an der Ruhr, Kreisfreie Stadt']),
         ('essen','Essen','pru',['Essen, Kreisfreie Stadt','Gelsenkirchen, Kreisfreie Stadt','Bottrop, Kreisfreie Stadt']),
         ('bochum','Bochum','pru',['Bochum, Kreisfreie Stadt','Herne, Kreisfreie Stadt']),
         ('dortmund','Dortmund','pru',['Dortmund, Kreisfreie Stadt','Unna']),
         ('wuppertal','Wuppertal','pru',['Wuppertal, Kreisfreie Stadt','Remscheid, Kreisfreie Stadt','Solingen, Kreisfreie Stadt']),
         ('koeln','Köln','pru',['Köln, Kreisfreie Stadt','Leverkusen, Kreisfreie Stadt','Rhein-Erft-Kreis']),
         ('bonn','Bonn','pru',['Bonn, Kreisfreie Stadt','Rhein-Sieg-Kreis'])])
    saxon_parent=original[original.source_id=='DEU-1601'].iloc[0]
    saxon_names=set(districts[districts.geometry.representative_point().within(saxon_parent.geometry)].shapeName)
    region('DEU-1601','wiki_saxony.json',{n:('pru' if n=='Nordsachsen' else 'sax') for n in saxon_names},
        [('leipzig','Leipzig','sax',['Leipzig','Leipzig, Kreisfreie Stadt'])],
        {'gorlitz':[control('goerlitz','pru',14.987,51.155),control('zittau','sax',14.806,50.899)]})
    bavarian_parent=original[original.source_id=='DEU-1591'].iloc[0]
    bavarian_names=set(districts[districts.geometry.representative_point().within(bavarian_parent.geometry)].shapeName)
    stems={}
    for name in sorted(bavarian_names):
        stems.setdefault(name.split(',')[0],[]).append(name)
    groups=[(safe_key(stem,'bav').replace('.','_'),stem,'cob' if stem=='Coburg' else 'bav',members)
            for stem,members in stems.items() if len(members)>1]
    region('DEU-1591','wiki_bavaria.json',{n:('cob' if n.startswith('Coburg,') else 'bav') for n in bavarian_names},groups)
    # Ensure every requested control actually matched a generated key.
    for path in OUT.glob('*.json'):
        data=json.loads(path.read_text(encoding='utf-8'))
        for item in data['locations']:
            if not item.get('owner') and not item.get('territory_controls'):
                raise ValueError('incomplete territorial assignment in '+str(path))
    print('Authored',len(list(OUT.glob('*.json'))),'complete German region specifications')


if __name__=='__main__':
    main()
