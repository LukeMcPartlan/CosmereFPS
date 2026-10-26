import unreal, os, math, time
import ur_upper_height_state as S
import ur_current_repair_inspect as I
import ur_current_repair_apply as A
import ur_current_repair_validate as V
def variant(row,progress):
    verts=row['vertices'];key=I.digest([row['source_mesh'],[[round(x,5) for x in q] for q in verts]])
    if key in progress['assets']:return unreal.load_asset(progress['assets'][key])
    path=S.ASSET_DIR+'/SM_Upper_'+key[:20];source=unreal.load_asset(row['source_mesh'])
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        sm=unreal.load_asset(path);assert unreal.EditorAssetLibrary.get_metadata_tag(sm,'UR_UpperHeight_Key')==key,'Unowned asset collision'
    else:
        sm=unreal.EditorAssetLibrary.duplicate_asset(row['source_mesh'],path);assert sm
        sm.modify();unreal.EditorAssetLibrary.set_metadata_tag(sm,'UR_UpperHeight_Key',key)
        assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    dm=unreal.DynamicMesh();dm.modify()
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(source,dm,unreal.GeometryScriptCopyMeshFromAssetOptions(),unreal.GeometryScriptMeshReadLOD())
    old=I.geometry(dm);assert len(verts)==len(old['v'])
    unreal.GeometryScript_MeshEdits.set_all_mesh_vertex_positions(dm,I.L.convert_array_to_vector_list([unreal.Vector(*q) for q in verts]))
    opts=unreal.GeometryScriptSplitNormalsOptions();opts.set_editor_property('opening_angle_deg',50)
    unreal.GeometryScript_Normals.compute_split_normals(dm,opts,unreal.GeometryScriptCalculateNormalsOptions())
    sm.modify();body=sm.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    opts=unreal.GeometryScriptCopyMeshToAssetOptions();opts.set_editor_property('enable_recompute_tangents',True)
    result=unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm,sm,opts,unreal.GeometryScriptMeshWriteLOD())
    assert 'FAILURE' not in str(result).upper(),str(result)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    actual=S.meshgeo(sm);assert len(actual['t'])==len(old['t']) and len(actual['v'])==len(verts)
    assert max(abs(a-b) for q,r in zip(actual['v'],verts) for a,b in zip(q,r))<.003,'Variant vertex readback'
    assert S.materials(sm)==S.materials(source),'Variant material mismatch'
    progress['assets'][key]=sm.get_path_name();S.save('progress.json',progress)
    return sm
def close_pose(a,p):
    return max(abs(x-y) for x,y in zip(I.vec(a.get_actor_location()),p['loc']))<1e-7 and max(abs(x-y) for x,y in zip(I.vec(a.get_actor_scale3d()),p['scale']))<1e-8 and max(abs(x-y) for x,y in zip(I.val(a.get_actor_rotation()),p['rot']))<1e-7
def rail_seat(a,p,aa):
    ignore=[x for x in aa.values() if isinstance(x,(unreal.LandscapeProxy,unreal.PlayerStart)) or x.get_class().get_name()=='BP_UrithiruLift_C' or '_Rail_' in x.get_actor_label()]
    x,y,z=p['loc'];h=V.hit([x,y,z+200],[x,y,z-600],ignore)
    assert h and h['actor'] and any(s in h['actor'] for s in ['_Ramp_','_Floor_','_Bridge_']),(a.get_actor_label(),'rail support',h)
    p['loc'][2]=h['point'][2]+p['scale'][2]*50/math.cos(math.radians(p['rot'][0]))
    return p
def run(seconds=23):
    rows=S.load('proposal.json');B=S.load('before.json');progress=S.load('progress.json');assert progress['fixes']<5,'Five-fix safety budget exhausted'
    aa=S.actors();done=set(progress['done']);start=time.time();changed=[]
    def rank(r):
        if r['kind']=='shell_transform':return 0
        if B[r['path']]['class']=='BP_UrithiruTierAnchor_C':return 1
        if r['kind']=='ramp_surface_keep_thickness':return 3
        if r['kind']=='rail_reslope_keep_section':return 4
        if r['kind']=='lift_placement':return 5
        return 2
    todo=sorted([r for p,r in rows.items() if p not in done],key=lambda r:(rank(r),r['label']))
    touched=[]
    try:
        with unreal.ScopedEditorTransaction('Shorten E09 E10 and refit upper interiors only'):
            for row in todo:
                if touched and time.time()-start>seconds:break
                p=row['path'];a=aa[p];b=B[p];pose={k:v[:] for k,v in row['pose'].items()}
                if row['kind']=='rail_reslope_keep_section':pose=rail_seat(a,pose,aa)
                if not close_pose(a,pose):A.pose(a,pose);changed.append(p)
                if row['vertices'] is not None:
                    sm=variant(row,progress);co=a.get_component_by_class(unreal.StaticMeshComponent);co.modify();co.set_static_mesh(sm)
                    assert co.get_editor_property('static_mesh')==sm
                    if p not in changed:changed.append(p)
                tags=b['tags'][:]
                if any(s.startswith('deck_top_z:') for s in tags):
                    tags=[s for s in tags if not s.startswith('deck_top_z:')]+['deck_top_z:'+str(pose['loc'][2])]
                    if tags!=[str(t) for t in a.get_editor_property('tags')]:a.set_editor_property('tags',tags)
                actual=I.snapshot(a)
                assert actual['parent']==b['parent'] and actual['folder']==b['folder']
                assert [c['materials'] for c in actual['components']]==[c['materials'] for c in b['components']],row['label']
                assert close_pose(a,pose),row['label']
                if 'properties' in b:assert actual['properties']==b['properties'],row['label']
                done.add(p);touched.append(p)
            for p,b in B.items():
                if p in rows:continue
                assert aa[p].get_actor_transform().export_text()==b['world'],('Protected world transform',b['label'])
        progress['done']=sorted(done);S.save('progress.json',progress)
        S.save('last_batch.json',{'touched':touched,'changed':changed})
        return {'success':True,'processed':len(touched),'remaining':len(rows)-len(done),'owned_assets':len(progress['assets']),'seconds':round(time.time()-start,1),'map_saved':False}
    except Exception as e:
        result=S.restore();progress['done']=[];progress['fixes']+=1;S.save('progress.json',progress)
        S.save('error.json',{'error':str(e),'restore':result,'fixes_used':progress['fixes']})
        return {'success':False,'error':str(e),'restored':result,'fixes_used':progress['fixes']}
