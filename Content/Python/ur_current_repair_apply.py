import unreal, os, json, math, time
import ur_current_repair_inspect as I
import ur_current_repair_mapping as M
ASSET_DIR='/Game/Maps/Urithiru/Repairs/CurrentSize_20261014'
def pose(a,p):
    a.modify();r=a.get_editor_property('root_component');r.modify();parent=a.get_attach_parent_actor()
    if parent:
        t=parent.get_actor_transform();loc=t.inverse_transform_location(unreal.Vector(*p['loc']));ps=parent.get_actor_scale3d();pr=parent.get_actor_rotation()
        assert max(abs(pr.pitch),abs(pr.yaw),abs(pr.roll))<1e-6,'Nontrivial parent rotation needs explicit compensation'
        scale=unreal.Vector(p['scale'][0]/ps.x,p['scale'][1]/ps.y,p['scale'][2]/ps.z)
    else:loc=unreal.Vector(*p['loc']);scale=unreal.Vector(*p['scale'])
    r.set_editor_property('relative_scale3d',scale);r.set_editor_property('relative_rotation',unreal.Rotator(pitch=p['rot'][0],yaw=p['rot'][1],roll=p['rot'][2]));r.set_editor_property('relative_location',loc)
    assert max(abs(x-y) for x,y in zip(I.vec(a.get_actor_location()),p['loc']))<.002,(a.get_actor_label(),'location')
    assert max(abs(x-y) for x,y in zip(I.vec(a.get_actor_scale3d()),p['scale']))<1e-6,(a.get_actor_label(),'scale')
def cave_cut_needed(row):
    if row['tier']!=1 or '_Floor_' not in row['label'] or not M.B[row['path']]['folder'].endswith('/Floors'):return False
    t=M.xf(row['pose']);v=[I.vec(t.transform_location(unreal.Vector(*q))) for q in row['vertices'] or M.G[row['source_mesh']]['v']]
    return min(q[0] for q in v)<53300 and max(q[0] for q in v)>51700 and min(q[1] for q in v)<23600 and max(q[1] for q in v)>20500

def edit_cave(dm,row):
    t=M.xf(row['pose']);unreal.GeometryScript_MeshTransforms.transform_mesh(dm,t,True)
    cut=unreal.DynamicMesh();cut.modify();opts=unreal.GeometryScriptPrimitiveOptions()
    unreal.GeometryScript_Primitives.append_box(cut,opts,unreal.Transform(location=unreal.Vector(52500,22050,0)),1600,3100,1000,0,0,0,unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    unreal.GeometryScript_MeshBooleans.apply_mesh_boolean(dm,unreal.Transform(),cut,unreal.Transform(),unreal.GeometryScriptBooleanOperation.SUBTRACT,unreal.GeometryScriptMeshBooleanOptions())
    b=M.B[row['path']]
    if abs((b['rot'][1]%360)-15)<.1 and 'Interior_' in row['source_mesh']:
        # Ramp threshold: the protected cavern ramp tops at -80, while the repaired main floor tops at zero.
        length=math.hypot(500,80);roll=math.degrees(math.atan2(80,500))
        unreal.GeometryScript_Primitives.append_box(dm,opts,unreal.Transform(location=unreal.Vector(52500,20750,-80),rotation=unreal.Rotator(roll=roll)),1380,length+12,80,0,0,0,unreal.GeometryScriptPrimitiveOriginMode.CENTER)
        for center,size in [((51665,22050,110),(70,3100,220)),((53335,22050,110),(70,3100,220)),((52500,23635,110),(1740,70,220))]:
            unreal.GeometryScript_Primitives.append_box(dm,opts,unreal.Transform(location=unreal.Vector(*center)),*size,0,0,0,unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    unreal.GeometryScript_MeshTransforms.transform_mesh(dm,t.inverse(),True)

def variant(row,registry):
    cave=cave_cut_needed(row);verts=row['vertices']
    if verts is None and not cave:return unreal.load_asset(row['source_mesh']),False
    key=I.digest([row['source_mesh'],[[round(x,4) for x in q] for q in verts] if verts else None,row['label'] if cave else None])
    if key in registry:return unreal.load_asset(registry[key]),False
    source=unreal.load_asset(row['source_mesh']);path=ASSET_DIR+'/SM_Fit_'+key[:20]
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        sm=unreal.load_asset(path);assert unreal.EditorAssetLibrary.get_metadata_tag(sm,'UR_Repair_Key')==key,'Unowned asset collision'
    else:
        sm=unreal.EditorAssetLibrary.duplicate_asset(row['source_mesh'],path);assert sm,'Mesh duplication failed'
        sm.modify();unreal.EditorAssetLibrary.set_metadata_tag(sm,'UR_Repair_Key',key)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False),'Initial owned asset save failed'
    dm=unreal.DynamicMesh();dm.modify();unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(source,dm,unreal.GeometryScriptCopyMeshFromAssetOptions(),unreal.GeometryScriptMeshReadLOD())
    if verts is not None:unreal.GeometryScript_MeshEdits.set_all_mesh_vertex_positions(dm,I.L.convert_array_to_vector_list([unreal.Vector(*q) for q in verts]))
    if cave:edit_cave(dm,row)
    n=unreal.GeometryScriptSplitNormalsOptions();n.set_editor_property('opening_angle_deg',50)
    unreal.GeometryScript_Normals.compute_split_normals(dm,n,unreal.GeometryScriptCalculateNormalsOptions())
    sm.modify();body=sm.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    opts=unreal.GeometryScriptCopyMeshToAssetOptions();opts.set_editor_property('enable_recompute_tangents',True)
    result=unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm,sm,opts,unreal.GeometryScriptMeshWriteLOD())
    assert 'FAILURE' not in str(result).upper(),str(result)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm,False),'Final owned asset save failed'
    registry[key]=sm.get_path_name();I.save('new_asset_registry.json',registry)
    return sm,True

def run(tiers,seconds=24):
    rows=I.load('proposals.json');actors={a.get_path_name():a for a in I.E.get_all_level_actors()};registry=I.load('new_asset_registry.json') if os.path.exists(os.path.join(I.F,'new_asset_registry.json')) else {};done=I.load('repair_progress.json') if os.path.exists(os.path.join(I.F,'repair_progress.json')) else []
    selected=sorted([r for r in rows.values() if r['tier'] in tiers and r['path'] not in done],key=lambda r:(r['tier'],0 if M.B[r['path']]['class']=='BP_UrithiruTierAnchor_C' else 1,r['label']))
    start=time.time();count=0;created=0;changed_assets=0
    with unreal.ScopedEditorTransaction('Fit Urithiru interiors and balcony assemblies to approved shells'):
        for row in selected:
            if count and time.time()-start>seconds:break
            a=actors[row['path']];pose(a,row['pose'])
            if row['source_mesh']:
                sm,new=variant(row,registry);created+=int(new);co=a.get_component_by_class(unreal.StaticMeshComponent)
                if co.get_editor_property('static_mesh')!=sm:co.modify();co.set_static_mesh(sm);changed_assets+=1
                assert co.get_editor_property('static_mesh')==sm,row['label']
            if M.B[row['path']]['class']=='TargetPoint' and 'deck_top_z:' in ' '.join(M.B[row['path']]['tags']):
                a.set_editor_property('tags',[s for s in M.B[row['path']]['tags'] if not s.startswith('deck_top_z:')]+['deck_top_z:'+str(row['pose']['loc'][2])])
            done.append(row['path']);count+=1
    I.save('repair_progress.json',done);I.save('new_asset_registry.json',registry)
    return {'processed':count,'done':len(done),'remaining_selected':len(selected)-count,'created_assets':created,'mesh_references_changed':changed_assets,'seconds':round(time.time()-start,2),'map_saved':False}

def lifts():
    A={a.get_path_name():a for a in I.E.get_all_level_actors()};results=[]
    with unreal.ScopedEditorTransaction('Align live Urithiru lift placements with repaired floors'):
        for path,b in M.B.items():
            if b['class']!='BP_UrithiruLift_C' or b['label'].startswith('Basement'):continue
            a=A[path];props=b['properties'];land=A[props['UpperLanding'] if props['bStartAtUpper'] else props['LowerLanding']];loc=I.vec(land.get_actor_location());loc[2]-=props['PivotToTopOffset'];pose(a,dict(loc=loc,rot=b['rot'],scale=b['scale']))
            for k in ['LowerLanding','UpperLanding','TravelSeconds','DwellSeconds','PlatformSizeCm','PlatformMaterial','bReplicates','bReplicateMovement','PivotToTopOffset']:assert I.val(a.get_editor_property(k))==props[k],(b['label'],k)
            c,e=a.get_actor_bounds(False,False);assert abs(c.z+e.z-land.get_actor_location().z)<.01,b['label'];results.append(b['label'])
    I.save('lifts_repaired.json',results);return {'placements_aligned':len(results),'logic_changes':0,'basement_lifts_changed':0}
