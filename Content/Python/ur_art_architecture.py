import unreal, math, json, os
import ur_artkit as A
HEIGHTS=[0,6600,13400,20000,26600,33400,40000,46600,53400,60000]
RADII=[42000,37000,30000,28000,26000,24000,22000,20000,18000,16000]

def kit():
    g=A.Geo()
    g.bevel((0,0,0),(110,180,1000),18)
    for dy in [-55,55]:g.bevel((65,dy,0),(22,22,900),5)
    return g.asset('SM_UR_ArtFlutedRibV2')

def tier(i):
    A.world_check();zone='E%02d'%(i+1);lo=HEIGHTS[i];hi=HEIGHTS[i+1] if i<9 else 63300;r=RADII[i+1]-2800 if i<9 else 13200
    if any(a.get_actor_label()=='Art'+zone+'_Cornice_01' for a in A.EAS.get_all_level_actors()):return {'tier':zone,'existing':True}
    A.root('Urithiru');A.root(zone)
    stone=A.Geo();bronze=A.Geo()
    for z,rr in [(hi-175,r),(lo-90,RADII[i])]:
        stone.annulus([(rr-40,z-210),(rr+190,z-210),(rr+460,z-80),(rr+460,z-30),(rr+370,z+35),(rr-40,z+35)])
        bronze.annulus([(rr+366,z+5),(rr+467,z+5),(rr+467,z+25),(rr+366,z+25)])
    if i>0:
        stone.annulus([(r+200,lo+140),(r+470,lo+140),(r+470,lo+260),(r+400,lo+300),(r+200,lo+300)])
    stone.actor('Art'+zone+'_Cornice_01',zone,'stone');bronze.actor('Art'+zone+'_Trim_01',zone,'bronze')
    rib=kit();count=0
    for j in range(72):
        ang=j*5
        if i==0 and (ang<6 or ang>354):continue
        A.instance(rib,'Art'+zone+'_Rib_%02d'%(j+1),zone,'bronze',A.polar(ang,r+490,(lo+hi)/2),(0,ang,0),(1,1,(hi-lo-380)/1000));count+=1
    rail=A.Geo();carved=A.Geo();inlay=A.Geo();violet=A.Geo();rc=cc=0
    original=[a for a in A.EAS.get_all_level_actors() if str(a.get_folder_path()).startswith('UR/'+zone+'/') and 'UR_FinalArt' not in [str(t) for t in a.tags]]
    for a in original:
        if not isinstance(a,unreal.StaticMeshActor):continue
        c=a.static_mesh_component;sm=c.get_editor_property('static_mesh');name=sm.get_name() if sm else '';p=a.get_actor_location();s=a.get_actor_scale3d();rot=a.get_actor_rotation();folder=str(a.get_folder_path())
        if name=='SM_UR_Rail':
            g=A.Geo();g.bevel((0,0,51),(100,112,6),1.4)
            xf=a.get_actor_transform();rail.add([tuple(unreal.MathLibrary.transform_location(xf,unreal.Vector(*v)).to_tuple()) for v in g.v],g.t);rc+=1
        elif 'Pillar' in name:
            top=p.z+abs(s.z)*50
            carved.bevel((p.x,p.y,top-45),(abs(s.x)*110,abs(s.y)*110,90),14,rot.yaw);cc+=1
        elif '/Cover' in folder:
            b,e=a.get_actor_bounds(False)
            carved.bevel((b.x,b.y,b.z+e.z-12),(max(80,e.x*1.7),max(80,e.y*1.7),22),4,rot.yaw);cc+=1
        if '/Routes' in folder and ('Bridge' in name):
            length=abs(s.x)*100;target=violet if 'Violet' in (c.get_material(0).get_name() if c.get_material(0) else '') else inlay
            target.bevel((p.x,p.y,p.z+22),(max(40,length-80),14,2),.4,rot.yaw)
    if rail.v:rail.actor('Art'+zone+'_RailGrip_01',zone,'bronze')
    if carved.v:carved.actor('Art'+zone+'_CarvedCaps_01',zone,'stone')
    if inlay.v:inlay.actor('Art'+zone+'_RouteInlay_01',zone,'cyan')
    if violet.v:violet.actor('Art'+zone+'_RouteInlay_02',zone,'violet')
    return {'tier':zone,'reused_rib_instances':count,'rail_grips':rc,'carved_caps':cc}

def plaza():
    A.world_check();A.root('Entrance');A.root('FrontPlaza')
    stone=A.Geo();bronze=A.Geo();doors=A.Geo();pave=A.Geo();cyan=A.Geo()
    for y in [-2800,2800]:
        stone.bevel((34500,y,2000),(680,600,4000),80)
        stone.bevel((34500,y,4150),(900,850,260),45)
        bronze.bevel((34845,y,2060),(25,140,3600),12)
    for j in range(19):
        a=math.pi*j/18;y=2850*math.cos(a);z=4190+1250*math.sin(a)
        stone.bevel((34450,y,z),(900,610,350),45)
    for s in [-1,1]:
        doors.bevel((30512,s*1250,2100),(24,2400,4000),5)
        for y in [s*100,s*2400]:bronze.bevel((30538,y,2100),(18,45,3860),4)
        for z in [250,2100,3950]:bronze.bevel((30540,s*1250,z),(20,2250,35),5)
        stone.bevel((32500,s*3420,4300),(4800,420,300),48)
    stone.actor('ArtEntrance_DoorFrame_01','Entrance','stone');bronze.actor('ArtEntrance_Trim_01','Entrance','bronze');doors.actor('ArtEntrance_DoorRelief_01','Entrance','doors')
    positions=[(87500,-25000),(91500,-17500),(93500,-9000),(94000,0),(93500,9000),(91500,17500),(87500,25000),(73000,-30000),(73000,30000)]
    for i,(cx,cy) in enumerate(positions,1):
        g=A.Geo();b=A.Geo()
        for z in [600,3300]:
            g.annulus([(2450,z),(3050,z),(3100,z+70),(3100,z+200),(2450,z+200)],n=96,cx=cx,cy=cy)
            b.annulus([(2890,z+50),(3120,z+50),(3120,z+90),(2890,z+90)],n=96,cx=cx,cy=cy)
        for j in range(16):g.bevel(A.polar(j*22.5,2920,1960,cx,cy),(150,150,2050),22,j*22.5)
        g.actor('ArtFrontPlaza_Bastion_%02d'%i,'FrontPlaza','stone');b.actor('ArtFrontPlaza_BastionTrim_%02d'%i,'FrontPlaza','bronze')
    for y in [-27980,27980]:
        pave.bevel((65200,y,1570),(45800,430,110),24)
        bronze.bevel((65200,y,1640),(45800,100,28),7)
    for x in range(55000,89000,2500):
        pave.bevel((x,0,-76),(30,55600,4),.7)
    for y in range(-27500,28000,2500):pave.bevel((71700,y,-75),(34000,30,4),.7)
    for sign in [-1,1]:cyan.bevel((67500,sign*1050,-73),(39000,18,2),.4)
    pave.actor('ArtFrontPlaza_PavingJoint_01','FrontPlaza','interior');bronze.actor('ArtFrontPlaza_RailGrip_01','FrontPlaza','bronze');cyan.actor('ArtFrontPlaza_RouteInlay_01','FrontPlaza','cyan')
    return {'bastions':9,'modeled_entry':True,'plaza_paving':True}
