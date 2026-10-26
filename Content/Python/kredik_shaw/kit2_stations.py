"""Test-only source Worlds, nested Level Instances and placement."""
import unreal
from . import kit2_defs as D
from . import kit2_arena as A
from . import kit2_build as B
SCENES=D.ROOT+'/Prefabs/TestAssemblies/'
PATHS={k:SCENES+'LI_KS_'+k for k in ('Window24','Window32','WindowStack','BridgeStation','StandardSpire60','CentralSpire150')}
def make_window(tier):
    path=PATHS['Window'+str(tier)];A.new(path);z=tier*100
    A.bp('WindowEntrance','Window%d_Window_01'%tier,(-200,0,z))
    if tier==24:
        A.bp('Balcony24','Window24_Platform_01',(-200,230,z))
        A.bp('Balcony24','Window24_Platform_02',(200,-230,z),yaw=180)
    else:
        A.mesh('BridgeDock','Window32_Platform_01',(0,30,z),yaw=90)
        A.mesh('BridgeDock','Window32_Platform_02',(0,-30,z),yaw=270)
    A.save();return path
def make_stack():
    A.new(PATHS['WindowStack'])
    A.instance(PATHS['Window24'],'WindowStack_Window_01')
    A.instance(PATHS['Window32'],'WindowStack_Window_02')
    A.save();return PATHS['WindowStack']
def make_bridge():
    A.new(PATHS['BridgeStation'])
    for tier,z in ((24,2400),(32,3200)):
        zone='Bridge'+str(tier)
        A.bp('Landing32',zone+'_Platform_01',(-400,0,z))
        A.bp('BridgeSpan',zone+'_Platform_02',(0,0,z))
        A.bp('BridgeSpan',zone+'_Platform_03',(400,0,z))
        A.bp('Landing32',zone+'_Platform_04',(800,0,z))
        # Extra side bay is a separate replaceable floor, never part of a merged bridge mesh.
        A.mesh('FloorBay',zone+'_Cover_01',(0,400,z))
        A.mesh('RailBay',zone+'_Wall_01',(0,610,z))
    A.save();return PATHS['BridgeStation']
def make_spire(central=False):
    key='CentralSpire150' if central else 'StandardSpire60'
    A.new(PATHS[key])
    A.bp('CentralSpire150' if central else 'SpireStandard60',key+'_Prop_01',(0,0,0))
    if not central:
        for i in range(4): A.bp('StairRise800','StandardSpire60_Stairs_%02d'%(i+1),(0,0,i*800))
        # Separate +24/+32 collar perimeter rail segments, with an east dock gap.
        for z in (2400,3200):
            for j,x in enumerate((-600,-200,200,600)):
                A.mesh('RailBay','Collar%d_Wall_%02d'%(z,j+1),(x,-510,z))
                A.mesh('RailBay','Collar%d_Wall_%02d'%(z,j+5),(x,810,z))
    A.save();return PATHS[key]
def assemble_test():
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    assert A.L.load_level(D.TEST)
    A.instance(PATHS['WindowStack'],'WindowStations_Window_01',(-3000,0,0))
    A.instance(PATHS['BridgeStation'],'BridgeStations_Platform_01',(-1000,2000,0))
    A.instance(PATHS['StandardSpire60'],'StandardSpire_Prop_01',(3000,0,0))
    A.instance(PATHS['CentralSpire150'],'CentralSpire_Prop_01',(9000,0,0))
    A.mesh('TestPad','TestArena_Floor_01',(0,0,0))
    A.mesh('TestPad','TestArena_Floor_02',(9000,0,0))
    sun=A.E.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,6000),unreal.Rotator(pitch=-45,yaw=-35))
    A.tag(sun,'TestArena_Prop_01','Lighting')
    sky=A.E.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,5000))
    A.tag(sky,'TestArena_Prop_02','Lighting')
    A.save()
    B.dump('step2_level_instance_manifest.json',{'test_world':D.TEST,'source_worlds':PATHS,'nested_chain':[D.TEST,PATHS['WindowStack'],PATHS['Window24'],D.bp('WindowEntrance')],'not_packed':True,'no_palace_or_graybox_build':True})
    return {'test':D.TEST,'source_worlds':PATHS}
