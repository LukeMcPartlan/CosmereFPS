import unreal, math, re
import ur_upper_height_state as S
import ur_upper_height_apply as U
import ur_current_repair_inspect as I

def close_pose(a,p):
    return max(abs(x-y) for x,y in zip(I.vec(a.get_actor_location()),p['loc']))<.002 and max(abs(x-y) for x,y in zip(I.vec(a.get_actor_scale3d()),p['scale']))<1e-6 and max(abs((x-y+180)%360-180) for x,y in zip(I.val(a.get_actor_rotation()),p['rot']))<.0001

def restore(paths=None):
    B=S.load('before.json');aa=S.actors();paths=paths or S.load('scope.json')
    with unreal.ScopedEditorTransaction('Restore current upper-layer height checkpoint'):
        for p in sorted(paths,key=lambda p:0 if B[p]['class']=='BP_UrithiruTierAnchor_C' else 1):
            a=aa[p];b=B[p];r=a.get_editor_property('root_component');a.modify();r.modify()
            assert (a.get_attach_parent_actor().get_path_name() if a.get_attach_parent_actor() else None)==b['parent']
            for k in ['relative_scale3d','relative_rotation','relative_location']:
                v=b['root'][k];r.set_editor_property(k,unreal.Rotator(pitch=v[0],yaw=v[1],roll=v[2]) if k=='relative_rotation' else unreal.Vector(*v))
            a.set_editor_property('tags',b['tags'])
            for c in a.get_components_by_class(unreal.PrimitiveComponent):
                old=next(x for x in b['components'] if x['path']==c.get_path_name())
                if c!=r and c.get_relative_transform().export_text()!=old['relative']:
                    def nums(field):return [float(x) for x in re.search(field+r'=\(([^)]+)\)',old['relative']).group(1).replace('X=','').replace('Y=','').replace('Z=','').replace('W=','').split(',')]
                    xyz=nums('Translation');sc=nums('Scale3D');q=nums('Rotation');c.modify()
                    c.set_editor_property('relative_scale3d',unreal.Vector(*sc));c.set_editor_property('relative_rotation',unreal.Quat(*q).rotator());c.set_editor_property('relative_location',unreal.Vector(*xyz))
                if isinstance(c,unreal.StaticMeshComponent):
                    sm=unreal.load_asset(old['mesh']) if old['mesh'] else None
                    if c.get_editor_property('static_mesh')!=sm:c.modify();c.set_static_mesh(sm)
                assert str(c.get_collision_enabled())==old['collision'] and str(c.get_collision_profile_name())==old['profile'],(b['label'],'collision')
            if 'properties' in b:assert I.snapshot(a)['properties']==b['properties'],(b['label'],'Lift properties were never authorized to change')
        for p in paths:
            assert close_pose(aa[p],B[p]),B[p]['label']
            now=I.snapshot(aa[p]);assert [c.get('mesh') for c in now['components']]==[c.get('mesh') for c in B[p]['components']],B[p]['label']
    S.save('restored.json',{'restored':len(paths),'map_saved':False})
    return {'restored':len(paths),'map_saved':False}

def install():
    U.close_pose=close_pose;S.restore=restore

def recover_failed_batch():
    install();result=restore();p=S.load('progress.json');p['done']=[];p['assets']={};p['fixes']=3;S.save('progress.json',p)
    return dict(result,fixes_used=p['fixes'])

def run(seconds=18):
    install();return U.run(seconds)
