import unreal
import ur_e10_stage2_state as S
def variant(row,progress):
    key=S.digest([row['source'],row['v']]);path=S.ASSET_DIR+'/SM_E10_'+key[:20]
    if key in progress['assets']:
        registered=progress['assets'][key]
        if unreal.EditorAssetLibrary.does_asset_exist(registered):return unreal.load_asset(registered)
    assert unreal.EditorAssetLibrary.does_asset_exist(row['source']),row['source']
    assert not unreal.EditorAssetLibrary.does_asset_exist(path),'Do not overwrite any existing asset'
    source=unreal.load_asset(row['source']);sm=unreal.EditorAssetLibrary.duplicate_asset(row['source'],path);assert sm
    sm.modify();unreal.EditorAssetLibrary.set_metadata_tag(sm,'E10Stage2LiveKey',key)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    dm=unreal.DynamicMesh();dm.modify()
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(source,dm,unreal.GeometryScriptCopyMeshFromAssetOptions(),unreal.GeometryScriptMeshReadLOD())
    old=S.geo(dm);assert len(old['v'])==len(row['v'])
    unreal.GeometryScript_MeshEdits.set_all_mesh_vertex_positions(dm,unreal.GeometryScript_List.convert_array_to_vector_list([unreal.Vector(*v) for v in row['v']]))
    opts=unreal.GeometryScriptSplitNormalsOptions();opts.set_editor_property('opening_angle_deg',50)
    unreal.GeometryScript_Normals.compute_split_normals(dm,opts,unreal.GeometryScriptCalculateNormalsOptions())
    body=sm.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    options=unreal.GeometryScriptCopyMeshToAssetOptions();options.set_editor_property('enable_recompute_tangents',True)
    sm.modify();result=unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm,sm,options,unreal.GeometryScriptMeshWriteLOD())
    assert 'FAILURE' not in str(result).upper(),str(result)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    actual=S.meshgeo(sm);assert actual['t']==old['t'],'Topology changed'
    assert max(abs(x-y) for a,b in zip(actual['v'],row['v']) for x,y in zip(a,b))<.003
    assert S.materials(sm)==S.materials(source)
    progress['assets'][key]=sm.get_path_name();S.save('progress.json',progress)
    return sm
def assets():
    rows=S.load('proposal.json');progress=S.load('progress.json')
    with unreal.ScopedEditorTransaction('Create E10 Stage 2 owned ramp variant only'):
        for row in rows.values():
            if row['v'] is not None:variant(row,progress)
    return {'owned_assets':list(progress['assets'].values()),'map_save_issued':False}
def apply():
    B=S.load('before.json');rows=S.load('proposal.json');aa=S.actors();progress=S.load('progress.json');changed=S.load('changed.json')
    assert set(aa)==set(B)
    for p,b in B.items():assert S.close(S.xf(aa[p].get_actor_transform()),b['world']),('Scene changed since checkpoint',b['label'])
    edits=[]
    with unreal.ScopedEditorTransaction('Stage 2 shorten E10 only and refit upper circulation; preserve E09'):
        for p in changed:
            r=rows[p];a=aa[p];assert r['label'].startswith('E10')
            S.set_pose(a,r['pose'])
            if r['v'] is not None:
                key=S.digest([r['source'],r['v']]);dest=progress['assets'][key]
                assert unreal.EditorAssetLibrary.does_asset_exist(dest),dest
                c=S.mesh_role(a);c.modify();c.set_static_mesh(unreal.load_asset(dest))
            edits.append(r['label'])
        for p,b in B.items():
            a=aa[p];now=S.snap(a);expected=rows[p]['pose'] if p in rows else b['world']
            assert S.close(now['world'],expected),('Unexpected pose',b['label'])
            assert now['parent']==b['parent'] and now['folder']==b['folder'] and now['tags']==b['tags']
            assert [c['materials'] for c in now['components']]==[c['materials'] for c in b['components']]
            if p not in changed:
                assert S.close(now['root'],b['root']),('Protected local',b['label'])
                assert len(now['components'])==len(b['components'])
                for c,d in zip(now['components'],b['components']):
                    assert S.close(c['world'],d['world']) and S.close(c['rel'],d['rel']),('Protected component',b['label'])
                    assert c.get('mesh')==d.get('mesh') and c.get('geometry')==d.get('geometry')
        for p,h in S.load('lift_proof.json').items():assert S.liftproof(aa[p])==h,'Lift authored settings changed'
    report={'changed_actors':len(edits),'labels':edits,'assets':list(progress['assets'].values()),'actors_added_deleted':0,'map_save_issued':False,'dirty':S.dirty()}
    S.save('applied.json',report);return {k:v for k,v in report.items() if k!='labels'}
