import unreal, os, json, math, re, hashlib, zlib, collections
F=unreal.Paths.convert_relative_path_to_full(os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/UrithiruRepairLive'))
E=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
Q=unreal.GeometryScript_MeshQueries
L=unreal.GeometryScript_List

def vec(v): return [v.x,v.y,v.z]
def save(n,d):
    with open(os.path.join(F,n),'w') as f: json.dump(d,f,indent=2,allow_nan=False)
def load(n):
    with open(os.path.join(F,n)) as f: return json.load(f)
def unpack(n):
    with open(os.path.join(F,n),'rb') as f: return json.loads(zlib.decompress(f.read()))
def geometry(dm):
    return {'v':[vec(v) for v in L.convert_vector_list_to_array(Q.get_all_vertex_positions(dm,False)[1])], 't':[[t.x,t.y,t.z] for t in L.convert_triangle_list_to_array(Q.get_all_triangle_indices(dm,False)[1])]}
def digest(d): return hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest()
def val(x):
    if isinstance(x,unreal.Object): return x.get_path_name()
    if isinstance(x,unreal.Vector): return vec(x)
    if isinstance(x,unreal.Rotator): return [x.pitch,x.yaw,x.roll]
    if isinstance(x,(bool,int,float,str)) or x is None: return x
    return x.export_text() if hasattr(x,'export_text') else str(x)
def snapshot(a):
    r=a.get_editor_property('root_component'); c,e=a.get_actor_bounds(False,False)
    d={'path':a.get_path_name(),'label':a.get_actor_label(),'class':a.get_class().get_name(),'folder':str(a.get_folder_path()),'tags':[str(t) for t in a.get_editor_property('tags')],'parent':a.get_attach_parent_actor().get_path_name() if a.get_attach_parent_actor() else None,'world':a.get_actor_transform().export_text(),'loc':vec(a.get_actor_location()),'rot':val(a.get_actor_rotation()),'scale':vec(a.get_actor_scale3d()),'bounds':[vec(c),vec(e)],'root':{},'components':[]}
    if r:
        d['root']={k:val(r.get_editor_property(k)) for k in ['relative_location','relative_rotation','relative_scale3d','absolute_location','absolute_rotation','absolute_scale']}
    for c in a.get_components_by_class(unreal.PrimitiveComponent):
        row={'path':c.get_path_name(),'relative':c.get_relative_transform().export_text(),'collision':str(c.get_collision_enabled()),'profile':str(c.get_collision_profile_name()),'materials':[val(c.get_material(i)) for i in range(c.get_num_materials())]}
        if isinstance(c,unreal.StaticMeshComponent): row['mesh']=val(c.get_editor_property('static_mesh'))
        if isinstance(c,unreal.DynamicMeshComponent): row['mesh_digest']=digest(geometry(c.get_dynamic_mesh()))
        d['components'].append(row)
    if a.get_class().get_name()=='BP_UrithiruLift_C':
        d['properties']={k:val(a.get_editor_property(k)) for k in ['LowerLanding','UpperLanding','TravelSeconds','DwellSeconds','bStartAtUpper','PivotToTopOffset','LowerPivot','UpperPivot','bAtUpper','bInitialized','PlatformSizeCm','PlatformMaterial','bReplicates','bReplicateMovement']}
    return d

def shell_world(i):
    n='E%02dSkin_Wall_01'%i; g=unpack(n+'.json.zlib')
    a=next(a for a in E.get_all_level_actors() if a.get_actor_label()==n)
    t=a.get_actor_transform(); g['v']=[vec(t.transform_location(unreal.Vector(*p))) for p in g['v']]
    return g

def slice_segments(g,z):
    out=[]
    for ids in g['t']:
        p=[g['v'][j] for j in ids]
        if z<min(q[2] for q in p) or z>max(q[2] for q in p): continue
        pts=[]
        for a,b in zip(p,p[1:]+p[:1]):
            if (a[2]<=z<b[2]) or (b[2]<=z<a[2]):
                f=(z-a[2])/(b[2]-a[2]); pts.append([a[k]+(b[k]-a[k])*f for k in (0,1)])
        if len(pts)==2: out.append(pts)
    return out

def radial_hits(segs,angle):
    co,si=math.cos(math.radians(angle)),math.sin(math.radians(angle)); hits=[]
    for a,b in segs:
        dx,dy=b[0]-a[0],b[1]-a[1]; det=co*dy-si*dx
        if abs(det)<1e-8: continue
        r=(a[0]*dy-a[1]*dx)/det; t=(a[0]*si-a[1]*co)/det
        if r>0 and -1e-7<=t<=1+1e-7: hits.append(r)
    return sorted(set(round(r,5) for r in hits))

def checkpoint():
    assert not os.path.exists(os.path.join(F,'scene_before.json')),'Checkpoint already exists; do not overwrite'
    rows={a.get_path_name():snapshot(a) for a in E.get_all_level_actors()}
    save('scene_before.json',rows)
    targets=[p for p,r in rows.items() if re.match(r'^E\d\d_(?!Light)',r['label']) and r['class'] not in ['PointLight','SpotLight'] or (r['class']=='BP_UrithiruLift_C' and not r['label'].startswith('Basement'))]
    save('target_paths.json',targets)
    save('dirty_before.json',{'maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]})
    return {'scene_actors':len(rows),'targets':len(targets),'checkpoint':F}

def restore():
    before=load('scene_before.json'); targets=load('target_paths.json'); actors={a.get_path_name():a for a in E.get_all_level_actors()}
    with unreal.ScopedEditorTransaction('Restore Urithiru current-size repair checkpoint'):
        for path in sorted(targets,key=lambda p:0 if before[p]['class']=='BP_UrithiruTierAnchor_C' else 1):
            a=actors[path]; b=before[path]; r=a.get_editor_property('root_component'); a.modify(); r.modify()
            assert (a.get_attach_parent_actor().get_path_name() if a.get_attach_parent_actor() else None)==b['parent']
            for k in ['relative_scale3d','relative_rotation','relative_location']:
                x=unreal.Rotator(pitch=b['root'][k][0],yaw=b['root'][k][1],roll=b['root'][k][2]) if k=='relative_rotation' else unreal.Vector(*b['root'][k]); r.set_editor_property(k,x)
            a.set_editor_property('tags',b['tags'])
            for co in a.get_components_by_class(unreal.StaticMeshComponent):
                old=next(c for c in b['components'] if c['path']==co.get_path_name()); co.modify(); co.set_static_mesh(unreal.load_asset(old['mesh']))
            if 'properties' in b:
                for k in ['LowerPivot','UpperPivot']: a.set_editor_property(k,unreal.Vector(*b['properties'][k]))
    return {'restored':len(targets),'map_saved':False}
