"""Build only new mesh assets; stage geometry in the scoped TestArena."""
import os,sys,json,unreal
from . import kit2_defs as D
BASE=os.path.join(unreal.Paths.project_saved_dir(),'Aura','LDA','District')
for p in (os.path.join(BASE,'_core'),):
    if p not in sys.path: sys.path.insert(0,p)
import district_core as K
FOLDER=os.path.join(BASE,'KS_Kit_Step2')
os.makedirs(FOLDER,exist_ok=True)
K.MAT_DIR=D.ROOT+'/Materials/Kit'
K._PARENT=K.MAT_DIR+'/M_KS_Kit'
K._MATS.clear()
REPORT=os.path.join(unreal.Paths.project_saved_dir(),'.Aura','reports','KredikShaw')
def fresh(path):
    reg=unreal.AssetRegistryHelpers.get_asset_registry()
    assert not unreal.EditorAssetLibrary.does_asset_exist(path), 'Asset collision: '+path
    assert not reg.get_assets_by_package_name(path), 'Registry collision: '+path

def world(): return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
def dump(name,data):
    os.makedirs(REPORT,exist_ok=True)
    with open(os.path.join(REPORT,name),'w') as f: json.dump(data,f,indent=2)
def build_one(key):
    assert world().get_path_name().split('.')[0]==D.TEST
    path=D.sm(key);fresh(path)
    ctx=K.Ctx('Blockout_KendrikShawKit','Kit')
    m=K.Mesh(ctx,(0,0,0))
    decorative=key.startswith('Spire') and key!='SpirePier'
    if key in D.BOXES:
        for center,size,mat in D.BOXES[key]:
            m.put('SourceModules','Prop',center,size,mat,pitch=33.6900675 if key=='StairRail' else 0,block=key)
        actor=m.bake('SourceModules','Prop',key,collide=not decorative)
    else:
        actor=m.bake('SourceModules','Prop',key,collide=False)
        comp=actor.dynamic_mesh_component;comp.modify()
        mesh=comp.get_dynamic_mesh();mesh.modify()
        rad,h,n,yaw=D.PROFILES[key]
        opts=unreal.GeometryScriptPrimitiveOptions()
        for z0,z1,r0,r1,turn in ((0,.25,rad,rad*.72,0),(.25,.6,rad*.72,rad*.37,yaw),(.6,1,rad*.37,0,2*yaw)):
            xf=unreal.Transform(location=unreal.Vector(0,0,z0*h),rotation=unreal.Rotator(yaw=turn))
            unreal.GeometryScript_Primitives.append_cone(mesh,opts,xf,r0,r1,(z1-z0)*h,n,1,True,unreal.GeometryScriptPrimitiveOriginMode.BASE)
        comp.set_material(0,K.mat('Wall'));comp.notify_mesh_modified()
    comp=actor.dynamic_mesh_component;mesh=comp.get_dynamic_mesh()
    box=unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(mesh)
    opts=unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.set_editor_property('enable_collision',not decorative)
    opts.set_editor_property('collision_mode',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    ret=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh,path,opts)
    sm=next(x for x in ret if isinstance(x,unreal.StaticMesh))
    sm.modify();sm.set_editor_property('has_navigation_data',not decorative)
    sm.set_editor_property('static_materials',[unreal.StaticMaterial(material_interface=x) for x in comp.get_materials()])
    body=sm.get_editor_property('body_setup')
    if body: body.set_editor_property('double_sided_geometry',True)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sm)
    unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
    index=(list(D.BOXES)+list(D.PROFILES)).index(key)
    origin=(index%10*2200-10000,12000+index//10*2200,0)
    a=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_object(sm,unreal.Vector(*origin))
    a.modify();a.set_actor_label('Kit_Prop_%02d'%(index+1));a.set_folder_path('Blockout_KendrikShawKit/ModuleMuseum')
    a.set_editor_property('tags',['KS_Owned','LDA_District','dst:KS_Kit_Step2','blk:'+key,'KS_KitMuseum','L1','KS_StableID:Mesh_'+key])
    a.static_mesh_component.modify()
    a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION if decorative else unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    actual=sm.get_bounding_box()
    entry={'package':path,'local_min':[actual.min.x,actual.min.y,actual.min.z],'local_max':[actual.max.x,actual.max.y,actual.max.z],'source_min':[box.min.x,box.min.y,box.min.z],'source_max':[box.max.x,box.max.y,box.max.z],'collision':not decorative,'nav_data':not decorative,'pivot':'local origin; decks top Z=0; bays start X=0; stair foot X=0','uv_source':'GeometryScript primitive UVs','source_block':key,'sample_actor':a.get_actor_label()}
    file=os.path.join(REPORT,'step2_mesh_manifest.json')
    data={}
    if os.path.exists(file):
        with open(file) as f: data=json.load(f)
    data[key]=entry;dump('step2_mesh_manifest.json',data)
def build(keys):
    return K.run_blocks(FOLDER,keys,build_one,key=D.SEED,seconds=20)
