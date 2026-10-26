"""Scoped kit stations, not palace or graybox construction."""
import unreal
from . import kit2_defs as D
from . import kit2_build as B
E=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
L=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
def owned_world():
    assert B.world().get_path_name().split('.')[0].startswith(D.ROOT+'/')
def tag(a,label,folder='Stations',ident=None):
    a.modify();a.set_actor_label(label);a.set_folder_path('Blockout_KendrikShawKit/'+folder)
    a.set_editor_property('tags',['KS_Owned','L1','KS_Pinned','KS_StableID:'+(ident or label),'KS_Seed:'+str(D.SEED)])
    return a
def mesh(key,label,loc,yaw=0,folder='Stations'):
    owned_world();asset=unreal.load_asset(D.sm(key));assert asset
    a=E.spawn_actor_from_object(asset,unreal.Vector(*loc),unreal.Rotator(yaw=yaw))
    return tag(a,label,folder)
def bp(key,label,loc,yaw=0,folder='Stations'):
    owned_world();cl=unreal.EditorAssetLibrary.load_blueprint_class(D.bp(key));assert cl
    a=E.spawn_actor_from_class(cl,unreal.Vector(*loc),unreal.Rotator(yaw=yaw))
    return tag(a,label,folder)
def save():
    owned_world();assert L.save_current_level()
def new(path):
    assert path.startswith(D.ROOT+'/');B.fresh(path)
    assert not [p for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if not p.get_path_name().startswith(D.ROOT+'/')]
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Save scoped current map first'
    assert L.new_level(path)
def instance(path,label,loc=(0,0,0),folder='LevelInstances'):
    owned_world();asset=unreal.load_asset(path);assert asset
    a=E.spawn_actor_from_class(unreal.LevelInstance,unreal.Vector(*loc))
    tag(a,label,folder);a.modify();assert a.set_world_asset(asset)
    assert a.get_world_asset().get_path_name().split('.')[0]==path
    a.load_level_instance()
    return a
