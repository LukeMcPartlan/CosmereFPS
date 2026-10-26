import unreal, math, time, copy
import ur_e09_stage1_state as S
def variant(row,progress):
    key=S.digest([row['source'],row['v']]);path=S.ASSET_DIR+'/SM_E09_'+key[:20]
    if key in progress['assets'] and unreal.EditorAssetLibrary.does_asset_exist(progress['assets'][key]):return unreal.load_asset(progress['assets'][key])
    assert unreal.EditorAssetLibrary.does_asset_exist(row['source']),row['source']
    assert not unreal.EditorAssetLibrary.does_asset_exist(path),'Owned destination exists but is unregistered: do not overwrite'
    source=unreal.load_asset(row['source']);sm=unreal.EditorAssetLibrary.duplicate_asset(row['source'],path);assert sm
    sm.modify();unreal.EditorAssetLibrary.set_metadata_tag(sm,'E09Stage1LiveKey',key)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    dm=unreal.DynamicMesh();dm.modify()
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(source,dm,unreal.GeometryScriptCopyMeshFromAssetOptions(),unreal.GeometryScriptMeshReadLOD())
    old=S.geo(dm);assert len(old['v'])==len(row['v'])
    unreal.GeometryScript_MeshEdits.set_all_mesh_vertex_positions(dm,unreal.GeometryScript_List.convert_array_to_vector_list([unreal.Vector(*v) for v in row['v']]))
    norm=unreal.GeometryScriptSplitNormalsOptions();norm.set_editor_property('opening_angle_deg',50)
    unreal.GeometryScript_Normals.compute_split_normals(dm,norm,unreal.GeometryScriptCalculateNormalsOptions())
    body=sm.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    opts=unreal.GeometryScriptCopyMeshToAssetOptions();opts.set_editor_property('enable_recompute_tangents',True)
    sm.modify();result=unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm,sm,opts,unreal.GeometryScriptMeshWriteLOD())
    assert 'FAILURE' not in str(result).upper(),str(result)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    actual=S.meshgeo(sm)
    assert actual['t']==old['t'],'Topology changed'
    assert len(actual['v'])==len(row['v']) and max(abs(x-y) for a,b in zip(actual['v'],row['v']) for x,y in zip(a,b))<.003
    assert S.materials(sm)==S.materials(source),'Material slots changed'
    progress['assets'][key]=sm.get_path_name();S.save('progress.json',progress)
    return sm
def assets(seconds=18):
    rows=S.load('proposal.json');p=S.load('progress.json');start=time.time();built=0
    with unreal.ScopedEditorTransaction('Create E09 stage 1 owned ramp variants'):
        for r in rows.values():
            if r['v'] is None:continue
            k=S.digest([r['source'],r['v']])
            if k in p['assets'] and unreal.EditorAssetLibrary.does_asset_exist(p['assets'][k]):continue
            variant(r,p);built+=1
            if time.time()-start>seconds:break
    need=set(S.digest([r['source'],r['v']]) for r in rows.values() if r['v'] is not None)
    return {'built':built,'total':len(p['assets']),'remaining':len(need-set(p['assets'])),'map_saved':False}
def set_pose(a,p):
    r=a.get_editor_property('root_component');par=a.get_attach_parent_actor();t=S.transform(p)
    if par:
        pt=par.get_actor_transform();local=pt.inverse_transform_location(unreal.Vector(*p['p']))
        ps=S.xf(pt)['s'];scale=unreal.Vector(*[p['s'][j]/ps[j] for j in range(3)])
        pq=pt.rotation;assert abs(pq.x)+abs(pq.y)+abs(pq.z)<1e-7,'Unexpected rotated attachment'
    else:local=unreal.Vector(*p['p']);scale=unreal.Vector(*p['s'])
    a.modify();r.modify()
    if not S.close(S.xf(a.get_actor_transform()),p):
        r.set_editor_property('relative_scale3d',scale)
        r.set_editor_property('relative_rotation',t.rotation.rotator())
        r.set_editor_property('relative_location',local)
    assert S.close(S.xf(a.get_actor_transform()),p),a.get_actor_label()
def apply():
    rows=S.load('proposal.json');B=S.load('before.json');aa=S.actors();progress=S.load('progress.json')
    assert all(S.close(S.xf(aa[p].get_actor_transform()),b['world']) for p,b in B.items()),'User scene changed after checkpoint; stop'
    for r in rows.values():
        if r['v'] is not None:
            k=S.digest([r['source'],r['v']]);assert k in progress['assets'] and unreal.EditorAssetLibrary.does_asset_exist(progress['assets'][k])
    edited=[]
    with unreal.ScopedEditorTransaction('Stage 1 shorten E09 and refit its live circulation; translate E10 intact'):
        for kind in ['E10_rigid_root','E09_shell_fixed_bottom','E09_panel_reslope_keep_live_thickness','E09_rail_fit_live_support','affected_lift_placement_only']:
            for path,r in rows.items():
                if r['kind']!=kind:continue
                a=aa[path];set_pose(a,r['pose']);edited.append(path)
                if r['v'] is not None:
                    c=a.get_component_by_class(unreal.StaticMeshComponent);c.modify();c.set_static_mesh(unreal.load_asset(progress['assets'][S.digest([r['source'],r['v']])]))
        for path,r in rows.items():
            a=aa[path];tags=[str(t) for t in a.get_editor_property('tags')]
            if r['kind']=='E10_inherited_translation' and any(t.startswith('deck_top_z:') for t in tags):
                a.set_editor_property('tags',[('deck_top_z:'+str(float(t.split(':',1)[1])-10000)) if t.startswith('deck_top_z:') else t for t in tags])
            assert S.close(S.xf(a.get_actor_transform()),r['pose']),r['label']
            now=S.snap(a);old=B[path]
            assert now['parent']==old['parent'] and now['folder']==old['folder']
            assert [c['materials'] for c in now['components']]==[c['materials'] for c in old['components']],r['label']
        for path,b in B.items():
            if path in rows:continue
            assert S.close(S.xf(aa[path].get_actor_transform()),b['world']),('Protected transform',b['label'])
        for path,h in S.load('lift_proof.json').items():assert S.liftproof(aa[path])==h,('Lift settings changed',B[path]['label'])
    S.save('applied.json',{'directly_edited':edited,'changed_world':S.load('changed.json'),'owned_assets':progress['assets'],'map_saved':False})
    return {'directly_edited':len(edited),'changed_world':len(S.load('changed.json')),'owned_assets':len(progress['assets']),'E10_shortened':False,'map_saved':False}
