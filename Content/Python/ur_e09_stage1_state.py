import unreal, os, json, zlib, hashlib, math, collections
E=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
F=os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/Urithiru_E09_Stage1_Live_20261014')
ASSET_DIR='/Game/Maps/Urithiru/Repairs/E09Stage1_Live_20261014'
def vec(v):return [v.x,v.y,v.z]
def val(v):
    if isinstance(v,unreal.Object):return v.get_path_name()
    if isinstance(v,unreal.Vector):return vec(v)
    if isinstance(v,(str,int,float,bool)) or v is None:return v
    return v.export_text() if hasattr(v,'export_text') else str(v)
def digest(v):return hashlib.sha256(json.dumps(v,sort_keys=True).encode()).hexdigest()
def save(n,v):
    os.makedirs(F,exist_ok=True)
    with open(os.path.join(F,n),'w') as f:json.dump(v,f,indent=1,allow_nan=False)
def load(n):
    with open(os.path.join(F,n)) as f:return json.load(f)
def packed(n,v):
    with open(os.path.join(F,n),'wb') as f:f.write(zlib.compress(json.dumps(v).encode(),6))
def unpack(n):
    with open(os.path.join(F,n),'rb') as f:return json.loads(zlib.decompress(f.read()))
def actors():return {a.get_path_name():a for a in E.get_all_level_actors()}
def xf(t):
    q=t.rotation
    return {'p':vec(t.translation),'s':vec(t.scale3d),'q':[q.x,q.y,q.z,q.w]}
def transform(t):return unreal.Transform(location=unreal.Vector(*t['p']),rotation=unreal.Quat(*t['q']).rotator(),scale=unreal.Vector(*t['s']))
def close(a,b,pt=.003,st=1e-6,qt=1e-7):
    if a is None or b is None:return a is None and b is None
    return max(abs(x-y) for x,y in zip(a['p'],b['p']))<=pt and max(abs(x-y) for x,y in zip(a['s'],b['s']))<=st and min(max(abs(x-y) for x,y in zip(a['q'],b['q'])),max(abs(x+y) for x,y in zip(a['q'],b['q'])))<=qt
def geo(dm):
    Q=unreal.GeometryScript_MeshQueries;L=unreal.GeometryScript_List
    return {'v':[vec(v) for v in L.convert_vector_list_to_array(Q.get_all_vertex_positions(dm,False)[1])],'t':[[v.x,v.y,v.z] for v in L.convert_triangle_list_to_array(Q.get_all_triangle_indices(dm,False)[1])]}
def meshgeo(sm):
    dm=unreal.DynamicMesh()
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm,dm,unreal.GeometryScriptCopyMeshFromAssetOptions(),unreal.GeometryScriptMeshReadLOD())
    return geo(dm)
def materials(sm):return [val(s.material_interface) for s in sm.get_editor_property('static_materials')]
def snap(a):
    r=a.get_editor_property('root_component');par=a.get_attach_parent_actor();c,e=a.get_actor_bounds(False,False)
    row={'label':a.get_actor_label(),'class':a.get_class().get_name(),'folder':str(a.get_folder_path()),'tags':[str(t) for t in a.get_editor_property('tags')],'parent':val(par),'world':xf(a.get_actor_transform()),'root':xf(r.get_relative_transform()) if r else None,'bounds':[vec(c),vec(e)],'components':[]}
    for c in a.get_components_by_class(unreal.PrimitiveComponent):
        d={'role':[c.get_name(),c.get_class().get_name()],'rel':xf(c.get_relative_transform()),'world':xf(c.get_world_transform()),'materials':[val(c.get_material(i)) for i in range(c.get_num_materials())],'collision':str(c.get_collision_enabled()),'profile':str(c.get_collision_profile_name())}
        if isinstance(c,unreal.StaticMeshComponent):d['mesh']=val(c.get_editor_property('static_mesh'))
        if isinstance(c,unreal.DynamicMeshComponent):d['geometry']=digest(geo(c.get_dynamic_mesh()))
        row['components'].append(d)
    return row
def liftproof(a):
    keys=['LowerLanding','UpperLanding','TravelSeconds','DwellSeconds','bStartAtUpper','PivotToTopOffset','PlatformSizeCm','PlatformMaterial','bReplicates','bReplicateMovement']
    return digest({k:val(a.get_editor_property(k)) for k in keys})
def landscape():
    rows=[];tex={}
    for t in unreal.ObjectIterator(unreal.Texture2D):
        if ':PersistentLevel.Landscape' in t.get_path_name():tex[t.get_path_name()]=t.blueprint_get_texture_source_id_string()
    for a in E.get_all_level_actors():
        if not isinstance(a,unreal.LandscapeProxy):continue
        d={'actor':snap(a),'settings':{},'components':[]}
        for k in ['landscape_material','landscape_hole_material','static_lighting_lod','lod_distribution_setting','lod0_distribution_setting','component_size_quads','subsection_size_quads','num_subsections']:
            try:d['settings'][k]=val(a.get_editor_property(k))
            except Exception:pass
        for c in a.get_components_by_class(unreal.LandscapeComponent):
            d['components'].append({'name':c.get_name(),'rel':xf(c.get_relative_transform())})
        rows.append(d)
    return {'actors':rows,'sources':tex}
def filehash(p):
    with open(p,'rb') as f:return hashlib.sha256(f.read()).hexdigest()
def frozen():
    import ur_current_repair_inspect as I
    out=[]
    for old in I.load('frozen_live_before.json'):
        p=old['path'];obj=unreal.load_asset(p);assert obj,p
        disk=os.path.join(unreal.Paths.project_content_dir(),p.split('.')[0][6:]+'.uasset')
        d={'path':p,'disk':filehash(disk) if os.path.isfile(disk) else None}
        if isinstance(obj,unreal.StaticMesh):d.update(geometry=digest(meshgeo(obj)),materials=materials(obj))
        if isinstance(obj,unreal.MaterialInstanceConstant):d['params']={k:val(obj.get_editor_property(k)) for k in ['parent','scalar_parameter_values','vector_parameter_values','texture_parameter_values']}
        out.append(d)
    return out
def checkpoint():
    assert not os.path.exists(os.path.join(F,'before.json')),'Fresh checkpoint already exists; never overwrite'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()=='/Game/Maps/Lvl_Urithiru.Lvl_Urithiru'
    aa=actors();B={p:snap(a) for p,a in aa.items()};save('before.json',B)
    scope=[p for p,b in B.items() if b['label'].startswith(('E09','E10','Pair09_10_','UR_Lift_09_10_'))]
    save('scope.json',scope);G={};fps={}
    for a in aa.values():
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            sm=c.get_editor_property('static_mesh')
            if not sm or sm.get_path_name() in G:continue
            p=sm.get_path_name();G[p]=meshgeo(sm);fps[p]={'geometry':digest(G[p]),'materials':materials(sm)}
    packed('static_geometry.zlib',G);save('asset_fingerprints.json',fps)
    save('lift_proof.json',{p:liftproof(a) for p,a in aa.items() if a.get_class().get_name()=='BP_UrithiruLift_C'})
    save('landscape.json',landscape());save('frozen.json',frozen())
    save('dirty_before.json',{'maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]})
    old=os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/UrithiruUpperHeight_20261014/progress.json')
    stale={}
    if os.path.isfile(old):
        with open(old) as f:reg=json.load(f).get('assets',{})
        stale={p:unreal.EditorAssetLibrary.does_asset_exist(p) for p in reg.values()}
    save('old_variant_existence.json',stale);save('progress.json',{'done':[],'assets':{},'fixes':0})
    return {'actors':len(B),'scope':len(scope),'static_assets':len(G),'landscape_sources':len(load('landscape.json')['sources']),'frozen':len(load('frozen.json')),'old_variants':len(stale),'old_missing':sum(not x for x in stale.values()),'checkpoint':F}
def restore():
    B=load('before.json');aa=actors();changed=load('changed.json')
    with unreal.ScopedEditorTransaction('Restore E09 stage 1 authored changes only'):
        for p in sorted(changed,key=lambda p:0 if B[p]['class']=='BP_UrithiruTierAnchor_C' else 1):
            a=aa[p];b=B[p];r=a.get_editor_property('root_component');a.modify();r.modify()
            r.set_relative_transform(transform(b['root']),False,False)
            if [str(t) for t in a.get_editor_property('tags')]!=b['tags']:a.set_editor_property('tags',b['tags'])
            cs=list(a.get_components_by_class(unreal.StaticMeshComponent));old=[c for c in b['components'] if 'mesh' in c]
            if cs:
                assert len(cs)==len(old)==1,'Ambiguous authored mesh role; stop recovery'
                sm=unreal.load_asset(old[0]['mesh']);assert sm
                cs[0].modify();cs[0].set_static_mesh(sm)
            assert close(xf(a.get_actor_transform()),b['world']),b['label']
    save('recovered.json',{'actors':len(changed),'map_saved':False});return len(changed)
