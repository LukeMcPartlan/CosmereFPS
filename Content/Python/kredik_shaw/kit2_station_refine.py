"""Station overrides are saved, pinned, and do not change shared originals."""
import unreal
from . import kit2_arena as A
from . import kit2_build as B
from . import kit2_defs as D
def rail(label,loc,length=400,yaw=0):
    a=A.mesh('RailBay',label,loc,yaw=yaw,folder='Guards')
    a.modify();a.set_actor_scale3d(unreal.Vector(length/400,1,1));return a
def passing_bays():
    for tier,z in ((24,2400),(32,3200)):
        actor=next(a for a in A.E.get_all_level_actors() if a.get_actor_label()=='Bridge%d_Platform_02'%tier)
        c=next(c for c in actor.get_components_by_class(unreal.StaticMeshComponent) if c.get_name().startswith('NorthRail'))
        c.modify();c.set_visibility(False);c.set_hidden_in_game(True)
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_ever_affect_navigation',False)
        rail('Bridge%d_Wall_02'%tier,(0,200,z),yaw=90)
        rail('Bridge%d_Wall_03'%tier,(400,200,z),yaw=90)
    A.save()
    B.dump('step2_pinned_overrides.json',{'passing_bay':{'world':'LI_KS_BridgeStation','override':'Bridge24/32_Platform_02.NorthRail disabled/hidden; bay side and outer guard actors separate','bay_floor':{'x':[0,400],'y':[200,600]},'clear_bay_mouth_width':380,'core_bridge_clear_width':400,'note':'Bay mouth is 380 cm between 20 cm side guards; core bridge remains 400 cm clear'}})
def collar_guards():
    for z in (2400,3200):
        rail('Collar%d_Wall_09'%z,(1000,-510,z),200)
        rail('Collar%d_Wall_10'%z,(1000,810,z),200)
        rail('Collar%d_Wall_11'%z,(-610,-500,z),1300,90)
        rail('Collar%d_Wall_12'%z,(1210,-500,z),450,90)
        rail('Collar%d_Wall_13'%z,(1210,350,z),450,90)
        rail('Collar%d_Wall_14'%z,(-200,-90,z),1000)
        rail('Collar%d_Wall_15'%z,(-200,390,z),1000)
        rail('Collar%d_Wall_16'%z,(790,-100,z),500,90)
    for i in range(4):
        z=i*800
        rail('StairTurn%d_Wall_01'%i,(810,-100,z+400),450,90)
        rail('StairTurn%d_Wall_02'%i,(600,-110,z+400),200)
        rail('StairTurn%d_Wall_03'%i,(600,360,z+400),200)
        for j,h in enumerate((0,800)):
            rail('StairEnd%d_Wall_%02d'%(i,j*2+1),(-200,-110,z+h),200)
            rail('StairEnd%d_Wall_%02d'%(i,j*2+2),(-200,360,z+h),200)
    A.save()
