"""Separately editable component Blueprints. No graph authoring."""
import unreal,json,os
from . import kit2_defs as D
from . import kit2_build as B
S=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
L=unreal.SubobjectDataBlueprintFunctionLibrary
def handles(bp): return S.k2_gather_subobject_data_for_blueprint(bp)
def obj(h,bp): return L.get_object_for_blueprint(L.get_data(h),bp)
def populate(key):
    path=D.bp(key);bp=unreal.load_asset(path)
    assert bp and path.startswith(D.ROOT+'/')
    bp.modify();unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    hs=handles(bp)
    roots=[h for h in hs if isinstance(obj(h,bp),unreal.SceneComponent)]
    assert len(roots)==1, 'Expected empty owned BP with one root: '+key
    root=obj(roots[0],bp);root.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
    entries=[]
    for p in D.components(key):
        hs=handles(bp)
        rh=next(h for h in hs if isinstance(obj(h,bp),unreal.SceneComponent) and obj(h,bp).get_name().startswith('DefaultSceneRoot'))
        params=unreal.AddNewSubobjectParams()
        params.set_editor_property('parent_handle',rh)
        params.set_editor_property('new_class',unreal.StaticMeshComponent)
        params.set_editor_property('blueprint_context',bp)
        h,reason=S.add_new_subobject(params)
        assert L.is_handle_valid(h),str(reason)
        assert S.rename_subobject(h,unreal.Text(p['name']))
        c=obj(h,bp);assert isinstance(c,unreal.StaticMeshComponent)
        c.modify();mesh=unreal.load_asset(D.sm(p['mesh']));assert mesh
        c.set_static_mesh(mesh)
        c.set_editor_property('relative_location',unreal.Vector(*p['xyz']))
        pitch=0 if p['mesh']=='StairRail' else p['pitch']
        c.set_editor_property('relative_rotation',unreal.Rotator(yaw=p['yaw'],pitch=pitch,roll=p.get('roll',0)))
        c.set_editor_property('relative_scale3d',unreal.Vector(*p.get('scale',[1,1,1])))
        c.set_editor_property('visible',p.get('visible',True))
        c.set_editor_property('hidden_in_game',p.get('hidden_in_game',False))
        c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
        c.set_editor_property('component_tags',['KS_EditablePiece','L1','KS_StableID:'+key+'_'+p['name']])
        c.set_collision_profile_name('BlockAll' if p['collision'] else 'NoCollision')
        c.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS if p['collision'] else unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_ever_affect_navigation',p.get('navigation',p['collision']))
        assert c.static_mesh==mesh
        assert c.get_collision_enabled()==(unreal.CollisionEnabled.QUERY_AND_PHYSICS if p['collision'] else unreal.CollisionEnabled.NO_COLLISION)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        entries.append(dict(p,actual_pitch=pitch,mesh_path=mesh.get_path_name()))
    cdo=unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property('tags',['KS_Owned','L1','KS_Pinned','KS_Seed:'+str(D.SEED),'KS_ComponentOnly_NoConstructionControls'])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp)
    checked=[]
    for h in handles(bp):
        c=obj(h,bp)
        if isinstance(c,unreal.StaticMeshComponent):
            checked.append({'name':c.get_name(),'mesh':c.static_mesh.get_path_name(),'collision':str(c.get_collision_enabled()),'nav':c.get_editor_property('can_ever_affect_navigation')})
    file=os.path.join(B.REPORT,'step2_blueprint_manifest.json');data={}
    if os.path.exists(file):
        with open(file) as f:data=json.load(f)
    data[key]={'package':path,'component_count':len(checked),'components':checked,'source_parts':entries,'compiled_and_saved':True,'construction_controls':'BLOCKED: bp_agent unavailable; variables are metadata only, not wired'}
    B.dump('step2_blueprint_manifest.json',data)
    return {'key':key,'components':len(checked),'saved':True}
