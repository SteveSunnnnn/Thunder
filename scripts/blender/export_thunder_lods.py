# Run inside Blender: blender asset.blend --background --python export_thunder_lods.py -- <output_dir> <asset_name>
import bpy, os, sys, json
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
if len(args)<2: raise SystemExit('usage: <output_dir> <asset_name>')
out_dir,name=args[0],args[1];os.makedirs(out_dir,exist_ok=True)
ratios=[1.0,0.5,0.2,0.08];manifest=[]
for lod,ratio in enumerate(ratios):
    duplicates=[]
    for obj in [o for o in bpy.context.scene.objects if o.type=='MESH']:
        dup=obj.copy();dup.data=obj.data.copy();bpy.context.collection.objects.link(dup);duplicates.append(dup)
        if ratio<1.0:
            mod=dup.modifiers.new('Thunder_LOD','DECIMATE');mod.ratio=ratio
            bpy.context.view_layer.objects.active=dup;dup.select_set(True);bpy.ops.object.modifier_apply(modifier=mod.name);dup.select_set(False)
    for o in bpy.context.selected_objects:o.select_set(False)
    for o in duplicates:o.select_set(True)
    path=os.path.join(out_dir,f'{name}_lod{lod}.glb')
    bpy.ops.export_scene.gltf(filepath=path,export_format='GLB',use_selection=True,export_apply=True)
    tris=sum(len(o.data.loop_triangles) for o in duplicates)
    manifest.append({'lod':lod,'ratio':ratio,'file':os.path.basename(path),'objects':len(duplicates),'triangles':tris})
    for o in duplicates:bpy.data.objects.remove(o,do_unlink=True)
with open(os.path.join(out_dir,f'{name}.thunderlod.json'),'w',encoding='utf-8') as f:json.dump(manifest,f,indent=2)
