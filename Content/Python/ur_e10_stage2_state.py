import unreal, os, json, zlib, collections
import ur_e09_stage1_state as U
F=os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/Urithiru_E10_Stage2_Live_20261014')
ASSET_DIR='/Game/Maps/Urithiru/Repairs/E10Stage2_Live_20261014'
vec=U.vec
val=U.val
digest=U.digest
xf=U.xf
transform=U.transform
close=U.close
geo=U.geo
meshgeo=U.meshgeo
materials=U.materials
snap=U.snap
actors=U.actors
landscape=U.landscape
frozen=U.frozen
liftproof=U.liftproof
def save(n,v):
    os.makedirs(F,exist_ok=True)
    with open(os.path.join(F,n),'w') as f:json.dump(v,f,indent=1,allow_nan=False)
def load(n):
    with open(os.path.join(F,n)) as f:return json.load(f)
def packed(n,v):
    with open(os.path.join(F,n),'wb') as f:f.write(zlib.compress(json.dumps(v).encode(),6))
def unpack(n):
    with open(os.path.join(F,n),'rb') as f:return json.loads(zlib.decompress(f.read()))
def bounds(a):
    c,e=a.get_actor_bounds(False,False)
    return {'min':[c.x-e.x,c.y-e.y,c.z-e.z],'max':[c.x+e.x,c.y+e.y,c.z+e.z],'size':[2*e.x,2*e.y,2*e.z]}
def dirty():return {'maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
def checkpoint():
    assert not os.path.exists(os.path.join(F,'before.json')),'Never overwrite a Stage 2 checkpoint'
    aa=actors();assert len(aa)==5384
    by={a.get_actor_label():a for a in aa.values()}
    assert abs(bounds(by['E09Skin_Wall_01'])['size'][2]-10000)<.003
    assert abs(bounds(by['E10Skin_Wall_01'])['size'][2]-20000)<.003
    B={p:snap(a) for p,a in aa.items()};save('before.json',B)
    scope=[p for p,b in B.items() if b['label'].startswith('E10')]
    save('scope.json',scope);G={};fp={}
    for a in aa.values():
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            sm=c.get_editor_property('static_mesh')
            if not sm or sm.get_path_name() in G:continue
            p=sm.get_path_name();G[p]=meshgeo(sm);fp[p]={'geometry':digest(G[p]),'materials':materials(sm)}
    packed('static_geometry.zlib',G);save('asset_fingerprints.json',fp)
    save('landscape.json',landscape());save('frozen.json',frozen())
    save('lift_proof.json',{p:liftproof(a) for p,a in aa.items() if a.get_class().get_name()=='BP_UrithiruLift_C'})
    save('dirty_before.json',dirty());save('progress.json',{'assets':{}})
    d=by['E10Skin_Wall_01'].get_component_by_class(unreal.DynamicMeshComponent)
    packed('shell_geometry.zlib',geo(d.get_dynamic_mesh()))
    save('baseline_bounds.json',{n:bounds(by[n+'Skin_Wall_01']) for n in ['E01','E02','E08','E09','E10']})
    return {'actors':len(B),'scope':len(scope),'static_assets':len(G),'landscape_sources':len(load('landscape.json')['sources']),'frozen_assets':len(load('frozen.json')),'checkpoint':F,'dirty':dirty()}
def set_pose(a,p):
    if close(xf(a.get_actor_transform()),p):return False
    r=a.get_editor_property('root_component');parent=r.get_attach_parent();t=transform(p)
    if parent:
        pt=parent.get_world_transform();pq=pt.rotation
        assert abs(pq.x)+abs(pq.y)+abs(pq.z)<1e-7,'Nontrivial attachment rotation: stop'
        loc=pt.inverse_transform_location(unreal.Vector(*p['p']));ps=xf(pt)['s'];sc=unreal.Vector(*[p['s'][j]/ps[j] for j in range(3)])
    else:loc=unreal.Vector(*p['p']);sc=unreal.Vector(*p['s'])
    a.modify();r.modify()
    r.set_editor_property('relative_scale3d',sc)
    r.set_editor_property('relative_rotation',t.rotation.rotator())
    r.set_editor_property('relative_location',loc)
    assert close(xf(a.get_actor_transform()),p),a.get_actor_label()
    return True
def mesh_role(a):
    cs=list(a.get_components_by_class(unreal.StaticMeshComponent))
    assert len(cs)==1,(a.get_actor_label(),'ambiguous authored mesh role')
    return cs[0]
def restore():
    B=load('before.json');aa=actors();paths=load('changed.json');done=[]
    with unreal.ScopedEditorTransaction('Restore only E10 Stage 2 scoped changes'):
        for p in paths:
            b=B[p];a=aa[p];assert b['label'].startswith('E10')
            assert snap(a)['parent']==b['parent'],'Attachment changed externally; stop'
            set_pose(a,b['world'])
            old=[c for c in b['components'] if 'mesh' in c]
            if old:
                c=mesh_role(a);sm=unreal.load_asset(old[0]['mesh']);assert sm
                if c.get_editor_property('static_mesh')!=sm:c.modify();c.set_static_mesh(sm)
            if [str(t) for t in a.get_editor_property('tags')]!=b['tags']:a.set_editor_property('tags',b['tags'])
            assert close(xf(a.get_actor_transform()),b['world'])
            done.append(b['label'])
    save('recovered.json',{'actors':done,'map_save_issued':False});return len(done)
