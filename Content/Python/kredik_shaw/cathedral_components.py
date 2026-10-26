"""Editable SCS setup only. Never accesses or edits a Blueprint graph."""
import unreal, os, json
from kredik_shaw import cathedral_assets as A
BP_DIR=A.ROOT+'/Prefabs/Architecture/Bridges/'
F=unreal.SubobjectDataBlueprintFunctionLibrary
S=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
def objects(bp):
    out={}
    for h in S.k2_gather_subobject_data_for_blueprint(bp):
        d=F.get_data(h); o=F.get_object_for_blueprint(d,bp)
        if isinstance(o,unreal.SceneComponent): out[str(F.get_variable_name(d))]=(h,o)
    return out
def mesh(name): return unreal.load_asset(A.MODELS+'/SM_KS_Cathedral_'+name)
def add(bp,parent,name,cls):
    current=objects(bp)
    if name in current: return current[name]
    p=unreal.AddNewSubobjectParams(parent_handle=parent,new_class=cls,blueprint_context=bp)
    h,why=S.add_new_subobject(p)
    assert F.is_handle_valid(h),str(why)
    assert S.rename_subobject(h,unreal.Text(name)),name
    return h,F.get_object_for_blueprint(F.get_data(h),bp)
def stage(bp,parent,name,asset,rep='VIS',xyz=(0,0,0),ism=False):
    h,c=add(bp,parent,name,unreal.InstancedStaticMeshComponent if ism else unreal.StaticMeshComponent)
    c.modify(); c.set_static_mesh(mesh(asset))
    c.set_editor_property('relative_location',unreal.Vector(*xyz))
    c.set_editor_property('relative_rotation',unreal.Rotator())
    c.set_editor_property('relative_scale3d',unreal.Vector(1,1,1))
    c.set_editor_property('override_materials',[])
    c.set_editor_property('visible',False); c.set_editor_property('hidden_in_game',True)
    c.set_editor_property('can_ever_affect_navigation',False)
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property('component_tags',['KS_Owned','L1','KS_CathedralProfile:CathedralStone_v1','KS_Representation:'+rep,'KS_Pending3DConstruction','KS_StableID:'+bp.get_name()+'_'+name])
    return h,c
def setup(key):
    bp=unreal.load_asset(BP_DIR+'BP_KS_'+key); bp.modify()
    old=objects(bp)
    parts={'Deck':'Deck400','NorthRail':'ParapetNorth400','SouthRail':'ParapetSouth400','NorthSupport':'ArchSupport400','SouthSupport':'ArchSupport400'}
    replacements={}
    for part,asset in parts.items():
        for name,(h,c) in old.items():
            isvis=name in (part,'Repeat'+part+'VIS')
            isproxy=name in ('COL_'+part,'NAV_'+part,'Repeat'+part+'COL','Repeat'+part+'NAV')
            if not isvis and not isproxy: continue
            chosen=asset
            if isproxy and part in ('Deck','NorthRail','SouthRail'): chosen='COL_'+asset
            c.modify(); c.set_static_mesh(mesh(chosen))
            overrides=list(c.get_editor_property('override_materials'))
            if all(m is None or '/Generated/Materials/Kit/' in m.get_path_name() for m in overrides): c.set_editor_property('override_materials',[])
            tags=[str(t) for t in c.component_tags]
            if 'KS_CathedralProfile:CathedralStone_v1' not in tags: c.set_editor_property('component_tags',tags+['KS_CathedralProfile:CathedralStone_v1'])
            assert c.static_mesh==mesh(chosen)
            replacements[name]=chosen
    root_h,detail=add(bp,old['KitRepeatRoot'][0],'CathedralDetailRoot',unreal.SceneComponent)
    detail.set_editor_property('component_tags',['KS_Owned','KS_CathedralProfile:CathedralStone_v1'])
    new=[]
    for name,asset in [('CathedralRibs','RibVault400'),('CathedralNorthPinnacles','Pinnacle'),('CathedralSouthPinnacles','Pinnacle')]:
        stage(bp,root_h,name,asset,ism=True); new.append(name)
    for stem,asset,xyz,reps in [('StartDock','Dock400',(-400,0,0),('VIS','COL','NAV')),('EndDock','Dock400',(400,0,0),('VIS','COL','NAV')),('TowerEntryStart','TowerPortal',(0,0,0),('VIS','COL')),('TowerEntryEnd','TowerPortal',(400,0,0),('VIS','COL'))]:
        vis_h,c=stage(bp,root_h,stem+'VIS',asset,xyz=xyz); new.append(stem+'VIS')
        for rep in reps[1:]: stage(bp,vis_h,stem+rep,asset,rep); new.append(stem+rep)
    if key=='BridgeSpanPassing':
        for stem,asset in [('PassingDeck','PassingDeck400'),('PassingGuard','PassingGuard400')]:
            h,c=stage(bp,root_h,stem+'VIS',asset); new.append(stem+'VIS')
            for rep in ('COL','NAV'): stage(bp,h,stem+rep,asset,rep); new.append(stem+rep)
        h,c=stage(bp,root_h,'CathedralClosedSouthParapetsVIS','ParapetSouth400',ism=True); new.append('CathedralClosedSouthParapetsVIS')
        for rep in ('COL','NAV'): stage(bp,h,'CathedralClosedSouthParapets'+rep,'COL_ParapetSouth400',rep,ism=True); new.append('CathedralClosedSouthParapets'+rep)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert bp.get_editor_property('status')!=unreal.BlueprintStatus.BS_ERROR,'Compile error '+key
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp)
    now=objects(bp)
    if key=='BridgeSpanPassing':
        for n in ('SouthRail','COL_SouthRail','NAV_SouthRail'):
            c=now[n][1]
            assert not c.get_editor_property('visible') and c.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION and not c.get_editor_property('can_ever_affect_navigation')
    rows={}
    for name,(h,c) in now.items():
        if not isinstance(c,unreal.StaticMeshComponent): continue
        rows[name]={'object_path':c.get_path_name(),'mesh':c.static_mesh.get_path_name() if c.static_mesh else None,'relative_location':c.get_editor_property('relative_location').export_text(),'relative_rotation':c.get_editor_property('relative_rotation').export_text(),'relative_scale':c.get_editor_property('relative_scale3d').export_text(),'collision':str(c.get_collision_enabled()),'visible':c.get_editor_property('visible'),'nav':c.get_editor_property('can_ever_affect_navigation'),'tags':[str(t) for t in c.component_tags],'instances':c.get_instance_count() if isinstance(c,unreal.InstancedStaticMeshComponent) else None}
    result={'blueprint':bp.get_path_name(),'compile_status':str(bp.get_editor_property('status')),'original_mesh_assignments':replacements,'staged_components':new,'components':rows,'new_details_activation':'Disabled until endpoint construction is implemented; do not mistake staging for working graph controls.'}
    with open(os.path.join(A.REPORT,'step2_cathedral_'+key+'_components.json'),'w') as fp: json.dump(result,fp,indent=2)
    return {'blueprint':bp.get_path_name(),'original_components_preserved':len(old),'mesh_assignments':len(replacements),'staged_mesh_components':len(new),'compile_status':result['compile_status']}
