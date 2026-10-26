import unreal, os, json, hashlib, collections
E = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
REPORT = os.path.join(unreal.Paths.project_saved_dir(), '.Aura', 'reports', 'UrithiruArt')
PROPS = ['LowerLanding','UpperLanding','TravelSeconds','DwellSeconds','PivotToTopOffset','PlatformSizeCm','PlatformMaterial','bStartAtUpper','bReplicates','bReplicateMovement','NetUpdateFrequency']
def xyz(v): return [v.x,v.y,v.z]
def path(v): return v.get_path_name() if v else None
def dirty():
    return {'maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()], 'content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
def note(name, value):
    os.makedirs(REPORT,exist_ok=True)
    with open(os.path.join(REPORT,name+'.json'),'w') as f: json.dump(value,f,indent=2,sort_keys=True)
def snapshot(name):
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert w.get_path_name().split('.')[0]=='/Game/Maps/Lvl_Urithiru'
    actors={};raw={};counts=collections.Counter();blocks=collections.Counter();lifts=[]
    for a in E.get_all_level_actors():
        cls=a.get_class().get_name();counts[cls]+=1;tags=[str(t) for t in a.tags]
        for t in tags:
            if t.startswith('blk:'):blocks[t]+=1
        xf=a.get_actor_transform();rot=a.get_actor_rotation()
        row={'label':a.get_actor_label(),'class':cls,'location':xyz(a.get_actor_location()),'rotation':[rot.pitch,rot.yaw,rot.roll],'scale':xyz(a.get_actor_scale3d()),'tags':sorted(tags),'components':{}}
        legacy=[a.get_path_name(),a.get_actor_label(),cls,str(xf),tags]
        for c in a.get_components_by_class(unreal.MeshComponent):
            materials=[path(c.get_material(i)) for i in range(c.get_num_materials())]
            state={'location':xyz(c.get_editor_property('relative_location')),'rotation':str(c.get_editor_property('relative_rotation')),'scale':xyz(c.get_editor_property('relative_scale3d')),'collision':str(c.get_collision_enabled()),'profile':str(c.get_collision_profile_name()),'materials':materials}
            old=[c.get_path_name(),str(c.get_editor_property('relative_location')),str(c.get_editor_property('relative_rotation')),str(c.get_editor_property('relative_scale3d')),state['collision'],state['profile'],materials]
            if isinstance(c,unreal.StaticMeshComponent): state['mesh']=path(c.static_mesh);old.append(state['mesh'])
            if isinstance(c,unreal.DynamicMeshComponent):
                m=c.get_dynamic_mesh();pos=unreal.GeometryScript_MeshQueries.get_all_vertex_positions(m,False)[1]
                vs=unreal.GeometryScript_List.convert_vector_list_to_array(pos);arr=[xyz(v) for v in vs]
                state.update(triangles=m.get_triangle_count(),vertices=unreal.GeometryScript_MeshQueries.get_vertex_count(m),vertex_hash=hashlib.sha256(str(arr).encode()).hexdigest())
                old.extend([state['triangles'],state['vertices'],state['vertex_hash']])
            row['components'][c.get_path_name()]=state;legacy.append(old)
        if cls=='BP_UrithiruLift_C':
            vals={k:str(a.get_editor_property(k)) for k in PROPS};row['properties']=vals;legacy.append(vals);lifts.append({'label':a.get_actor_label(),'properties':vals})
        actors[a.get_path_name()]=row;raw[a.get_path_name()]=legacy
    gm=w.get_world_settings().get_editor_property('default_game_mode')
    rows=sorted(raw.values(),key=lambda x:x[0])
    legacy={'sha256':hashlib.sha256(json.dumps(rows,sort_keys=True).encode()).hexdigest(),'actor_count':len(rows),'classes':dict(counts),'blocks':dict(blocks),'lifts':lifts,'world_settings':str(gm)}
    out={'actors':actors,'raw_rows':raw,'legacy_comparison':legacy,'game_mode':path(gm),'dirty':dirty()}
    note(name,out);return out

def diff(a,b,prefix=''):
    out=[]
    if isinstance(a,dict) and isinstance(b,dict):
        for k in sorted(set(a)|set(b)):
            p=prefix+'/'+str(k)
            if k not in a:out.append({'field':p,'before':'<absent>','after':b[k]})
            elif k not in b:out.append({'field':p,'before':a[k],'after':'<absent>'})
            else:out.extend(diff(a[k],b[k],p))
    elif a!=b:out.append({'field':prefix,'before':a,'after':b})
    return out

def test():
    before=snapshot('revision_test_before');again=snapshot('revision_test_readonly_repeat')
    temp=None;measures={}
    def measure(a):
        m=a.dynamic_mesh_component.get_dynamic_mesh();b=unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(m);v,valid=unreal.GeometryScript_MeshQueries.get_vertex_position(m,0)
        return {'dimensions':[b.max.x-b.min.x,b.max.y-b.min.y,b.max.z-b.min.z],'vertex0':xyz(v),'valid':valid,'transform':str(a.get_actor_transform())}
    try:
        temp=E.spawn_actor_from_class(unreal.DynamicMeshActor,unreal.Vector(800000,800000,-800000));temp.modify();temp.set_actor_label('GeometryTest_Prop_01');temp.set_editor_property('tags',['UR_OwnedReversibleGeometryTest'])
        c=temp.dynamic_mesh_component;c.modify();m=c.get_dynamic_mesh();m.modify()
        unreal.GeometryScript_Primitives.append_box(m,unreal.GeometryScriptPrimitiveOptions(),unreal.Transform(),200,300,400,0,0,0,unreal.GeometryScriptPrimitiveOriginMode.CENTER)
        c.notify_mesh_modified();measures['before']=measure(temp);m.modify()
        unreal.GeometryScript_MeshTransforms.transform_mesh(m,unreal.Transform(scale=unreal.Vector(1.75,1,.625)),True);c.notify_mesh_modified();measures['after']=measure(temp);note('revision_test_measurements',measures)
    finally:
        if temp is not None:temp.modify();assert E.destroy_actor(temp)
    after=snapshot('revision_test_after')
    stable=diff(before['actors'],after['actors'],'actors')+diff(before['game_mode'],after['game_mode'],'game_mode')
    raw=diff(before['raw_rows'],after['raw_rows'],'raw_rows')
    legacy=diff(before['legacy_comparison'],after['legacy_comparison'],'legacy_comparison')
    repeat=diff(before['actors'],again['actors'],'actors')
    out={'canonical_diffs':stable,'raw_row_diffs':raw,'legacy_diffs':legacy,'readonly_repeat_diffs':repeat,'measurements':measures,'before_dirty':before['dirty'],'after_dirty':after['dirty'],'actor_count':len(after['actors']),'counts':after['legacy_comparison']['classes'],'blocks':len(after['legacy_comparison']['blocks'])}
    note('revision_test_exact_diffs',out)
    assert not stable,'Exact actor fields changed; see revision_test_exact_diffs.json'
    assert measures['before']['dimensions']==[200,300,400] and measures['after']['dimensions']==[350,300,250]
    assert measures['before']['vertex0']!=measures['after']['vertex0'] and measures['before']['transform']==measures['after']['transform']
    note('revision_trustworthy_baseline',after)
    return {k:v for k,v in out.items() if k not in ['counts']}
