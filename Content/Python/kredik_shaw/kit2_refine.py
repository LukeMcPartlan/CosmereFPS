"""Explicit refinements of only this run's owned prototype assets."""
import unreal
from . import kit2_defs as D
from . import kit2_bp as P
from . import kit2_build as B
def refine():
    for key in ('SpireCollar32','SpireStandard60'):
        bp=unreal.load_asset(D.bp(key));assert bp
        bp.modify();seen=set()
        for h in P.handles(bp):
            c=P.obj(h,bp)
            if not isinstance(c,unreal.StaticMeshComponent) or c.get_name() in seen: continue
            seen.add(c.get_name());name=c.get_name();loc=c.get_editor_property('relative_location')
            if name.startswith('North') or name.startswith('South'):
                c.set_editor_property('relative_location',unreal.Vector(-200,loc.y,loc.z))
            if name.startswith('DecorativeCap') and key=='SpireStandard60':
                c.set_editor_property('relative_location',unreal.Vector(300,150,4000))
                c.set_editor_property('relative_scale3d',unreal.Vector(1,1,5/7))
        if key=='SpireStandard60':
            rh=next(h for h in P.handles(bp) if isinstance(P.obj(h,bp),unreal.SceneComponent) and P.obj(h,bp).get_name().startswith('DefaultSceneRoot'))
            params=unreal.AddNewSubobjectParams()
            params.set_editor_property('parent_handle',rh)
            params.set_editor_property('new_class',unreal.StaticMeshComponent)
            params.set_editor_property('blueprint_context',bp)
            h,reason=P.S.add_new_subobject(params);assert P.L.is_handle_valid(h),str(reason)
            assert P.S.rename_subobject(h,unreal.Text('DecorativeNeck'))
            c=P.obj(h,bp);c.modify();c.set_static_mesh(unreal.load_asset(D.sm('SpirePier')))
            c.set_editor_property('relative_location',unreal.Vector(300,150,3200))
            c.set_editor_property('relative_scale3d',unreal.Vector(2,2,.25))
            c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
            c.set_editor_property('component_tags',['L1','KS_EditablePiece','KS_Decorative_NoRoute'])
            c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            c.set_editor_property('can_ever_affect_navigation',False)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp)
    B.dump('step2_source_refinements.json',{'collar':{'north_south_origin_x':-200,'actual_outer_x':[-600,1200],'inner_x':[-200,800],'clear_band_cm':400},'standard_cap':{'base_z':4000,'local_height':2800,'component_scale_z':5/7,'target_tip':6000,'reason':'800 cm upper collar headroom; cap and independent neck noncolliding/non-navigating'},'source_of_truth':'Blueprint component transforms and actual bounds supersede initial kit2_defs component positions; bp_agent should reconcile source definitions when wiring controls'})
    return 'Owned collar corners, cap clearance and independent neck refined'
