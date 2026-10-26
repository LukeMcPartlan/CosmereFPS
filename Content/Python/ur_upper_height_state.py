import unreal, os, json, zlib, hashlib, math
import ur_current_repair_inspect as I
import ur_current_repair_apply as A
F=unreal.Paths.convert_relative_path_to_full(os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/UrithiruUpperHeight_20261014'))
ASSET_DIR='/Game/Maps/Urithiru/Repairs/UpperHeight_20261014'
def save(n,d):
    os.makedirs(F,exist_ok=True)
    with open(os.path.join(F,n),'w') as f: json.dump(d,f,indent=2,allow_nan=False)
def load(n):
    with open(os.path.join(F,n)) as f:return json.load(f)
def packed(n,d):
    with open(os.path.join(F,n),'wb') as f:f.write(zlib.compress(json.dumps(d).encode(),6))
def unpack(n):
    with open(os.path.join(F,n),'rb') as f:return json.loads(zlib.decompress(f.read()))
def actors():return {a.get_path_name():a for a in I.E.get_all_level_actors()}
def meshgeo(sm):
    dm=unreal.DynamicMesh()
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm,dm,unreal.GeometryScriptCopyMeshFromAssetOptions(),unreal.GeometryScriptMeshReadLOD())
    return I.geometry(dm)
def materials(sm):return [I.val(s.material_interface) for s in sm.get_editor_property('static_materials')]
def landscape():
    tex={};props=[]
    for a in I.E.get_all_level_actors():
        if not isinstance(a,unreal.LandscapeProxy):continue
        p={'actor':I.snapshot(a),'settings':{}}
        for k in ['landscape_material','landscape_hole_material','static_lighting_lod','lod_distribution_setting','lod0_distribution_setting','component_size_quads','subsection_size_quads','num_subsections']:
            try:p['settings'][k]=I.val(a.get_editor_property(k))
            except Exception:pass
        cs=a.get_components_by_class(unreal.LandscapeComponent);p['components']=[]
        for c in cs:
            row={'path':c.get_path_name(),'relative':c.get_relative_transform().export_text()}
            for k in ['heightmap_texture','weightmap_textures','weightmap_layer_allocations']:
                v=c.get_editor_property(k);row[k]=[I.val(x) for x in v] if k!='heightmap_texture' else I.val(v)
                ts=[v] if k=='heightmap_texture' else list(v) if k=='weightmap_textures' else []
                for t in ts:
                    if t:tex[t.get_path_name()]=t.blueprint_get_texture_source_id_string()
            p['components'].append(row)
        props.append(p)
    return {'landscapes':props,'texture_sources':tex}
def filehash(p):
    with open(p,'rb') as f:return hashlib.sha256(f.read()).hexdigest()
def frozen():
    out=[]
    for b in I.load('frozen_live_before.json'):
        obj=unreal.load_asset(b['path']);r={'path':b['path'],'class':obj.get_class().get_name()}
        package=b['path'].split('.')[0];disk=os.path.join(unreal.Paths.project_content_dir(),package[6:]+'.uasset')
        r['disk']=filehash(disk) if os.path.isfile(disk) else None
        if isinstance(obj,unreal.StaticMesh):r.update(geometry=I.digest(meshgeo(obj)),materials=materials(obj))
        if isinstance(obj,unreal.MaterialInstanceConstant):r['params']={k:I.val(obj.get_editor_property(k)) for k in ['parent','scalar_parameter_values','vector_parameter_values','texture_parameter_values']}
        out.append(r)
    return out
def checkpoint():
    assert not os.path.exists(os.path.join(F,'before.json')),'Never overwrite upper-height checkpoint'
    aa=actors();world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()=='/Game/Maps/Lvl_Urithiru.Lvl_Urithiru'
    rows={p:I.snapshot(a) for p,a in aa.items()};save('before.json',rows)
    scope=[p for p,b in rows.items() if b['label'] in ['E09Skin_Wall_01','E10Skin_Wall_01'] or (b['label'].startswith(('E09_','E10_')) and b['class'] not in ['PointLight','SpotLight','RectLight']) or b['label'].startswith('Pair09_10_')]
    save('scope.json',scope);geos={};fps={}
    for a in aa.values():
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            sm=c.get_editor_property('static_mesh')
            if not sm or sm.get_path_name() in geos:continue
            key=sm.get_path_name();g=meshgeo(sm);geos[key]=g;fps[key]={'geometry':I.digest(g),'materials':materials(sm)}
    packed('static_geometry.zlib',geos);save('asset_fingerprints.json',fps)
    save('landscape.json',landscape());save('frozen.json',frozen())
    save('dirty_before.json',{'maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]})
    save('progress.json',{'done':[],'assets':{},'fixes':0})
    return {'actors':len(rows),'scope':len(scope),'static_assets':len(geos),'checkpoint':F}
def restore(paths=None):
    b=load('before.json');aa=actors();paths=paths or load('scope.json')
    with unreal.ScopedEditorTransaction('Restore upper height checkpoint only'):
        for p in sorted(paths,key=lambda p:0 if b[p]['class']=='BP_UrithiruTierAnchor_C' else 1):
            a=aa[p];r=a.get_editor_property('root_component');a.modify();r.modify()
            assert (a.get_attach_parent_actor().get_path_name() if a.get_attach_parent_actor() else None)==b[p]['parent']
            for k in ['relative_scale3d','relative_rotation','relative_location']:
                v=b[p]['root'][k];r.set_editor_property(k,unreal.Rotator(pitch=v[0],yaw=v[1],roll=v[2]) if k=='relative_rotation' else unreal.Vector(*v))
            a.set_editor_property('tags',b[p]['tags'])
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                old=next(x for x in b[p]['components'] if x['path']==c.get_path_name());c.modify();c.set_static_mesh(unreal.load_asset(old['mesh']))
            if 'properties' in b[p]:
                for k in ['LowerPivot','UpperPivot']:a.set_editor_property(k,unreal.Vector(*b[p]['properties'][k]))
        for p in paths:
            now=I.snapshot(aa[p])
            assert now['root']==b[p]['root'],b[p]['label']
    save('restored.json',{'paths':paths,'map_saved':False});return {'restored':len(paths),'map_saved':False}
