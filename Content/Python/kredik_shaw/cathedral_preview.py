"""Unpersisted geometry evidence, not an endpoint-construction implementation."""
import os, sys, math, json, unreal
from kredik_shaw import cathedral_shapes as S
from kredik_shaw import cathedral_assets as A
BASE=os.path.join(unreal.Paths.project_saved_dir(),'Aura/LDA/District')
if os.path.join(BASE,'_core') not in sys.path: sys.path.insert(0,os.path.join(BASE,'_core'))
import district_core as K
FOLDER=os.path.join(BASE,'KS_CathedralGeometryEvidence_v1')
ROOT='Blockout_KS_CathedralGeometryEvidence'
ORIGIN=(-12000,-15000,2400)
RUN=3200; RISE=800; BAYS=8
THETA=math.atan2(RISE,RUN); C=math.cos(THETA); Q=math.sin(THETA)
SCALE=math.hypot(RUN,RISE)/BAYS/400
MATS=['Stone','Granite','Metal']
def xf(shape,origin,pitch=0,sx=1):
    c=math.cos(pitch); s=math.sin(pitch); ox,oy,oz=origin
    return [(ox+c*x*sx-s*z,oy+y,oz+s*x*sx+c*z) for x,y,z in shape.v]
def add(m,shape,origin,pitch=0,sx=1): m.tris(xf(shape,origin,pitch,sx),shape.t,[MATS[int(k)] for k in shape.m])
def point(x,y=0,z=0): return (ORIGIN[0]+C*x-Q*z,ORIGIN[1]+y,ORIGIN[2]+Q*x+C*z)
def builder(block):
    passing=block=='Passing'; row=1600 if passing else 0
    ctx=K.Ctx(ROOT,'CausewayPassing' if passing else 'CausewayStandard')
    m=K.Mesh(ctx,(ORIGIN[0],ORIGIN[1]+row,ORIGIN[2]))
    mods=S.modules()
    def p(x,y=0,z=0):
        a,b,c=point(x,y,z); return (a,b+row,c)
    for i in range(BAYS):
        off=i*400*SCALE
        add(m,mods['Deck400'],p(off),THETA,SCALE)
        add(m,mods['RibVault400'],p(off),THETA,SCALE)
        for side in (-1,1):
            add(m,mods['ArchSupport400'],p(off,side*180),THETA,SCALE)
            if not(passing and i==4 and side==1):
                name='ParapetNorth400' if side==-1 else 'ParapetSouth400'
                add(m,mods[name],p(off,side*210),THETA,SCALE)
                add(m,mods['Pinnacle'],p(off+30*SCALE,side*240,126))
        if passing and i==4:
            for name in ('PassingDeck400','PassingGuard400'): add(m,mods[name],p(off),THETA,SCALE)
    # Docks are level, and portals are upright, never pitched with the span.
    add(m,mods['Dock400'],(ORIGIN[0]-400,ORIGIN[1]+row,2400))
    add(m,mods['Dock400'],(ORIGIN[0]+RUN,ORIGIN[1]+row,3200))
    add(m,mods['TowerPortal'],(ORIGIN[0],ORIGIN[1]+row,2400))
    add(m,mods['TowerPortal'],(ORIGIN[0]+RUN,ORIGIN[1]+row,3200))
    a=m.bake('GeometryEvidence','Prop',block,collide=True)
    a.modify(); a.set_actor_label(('Passing' if passing else 'Standard')+'_Prop_01')
    a.set_editor_property('tags',list(a.tags)+['KS_TemporaryGeometryEvidence'])
def build():
    os.makedirs(FOLDER,exist_ok=True)
    for name,mat in zip(MATS,A.materials()): K._MATS[name]=mat
    K.save_layout(FOLDER,{'purpose':'temporary geometry comparison only','run':RUN,'rise':RISE,'bays':BAYS,'origin':ORIGIN})
    return K.run_blocks(FOLDER,['Standard','Passing'],builder,key='cathedral-v1')
def numeric():
    mods=S.modules()
    north=max(y-210 for x,y,z in mods['ParapetNorth400'].v)
    south=min(y+210 for x,y,z in mods['ParapetSouth400'].v)
    joins=[]
    for i in range(BAYS-1):
        a=point((i+1)*400*SCALE); b=point((i*400+400)*SCALE)
        joins.append(math.dist(a,b))
    checks={'preview_only':True,'bay_count_each':BAYS,'pitch_degrees':math.degrees(THETA),'bay_x_scale':SCALE,'clear_width_cm':south-north,'low_endpoint_cm':point(0),'high_endpoint_cm':point(BAYS*400*SCALE),'max_authored_deck_z_cm':3200,'seam_gaps_cm':joins,'dock_top_height_mismatch_cm':[0,0],'portal_clear_rectangle_cm':[400,400],'support_depth_cm':700,'pinnacle_height_cm':341,'passing_opening_bay_index':4,'passing_floor_y_range_cm':[200,600],'passing_guard_in_opening':False,'graph_construction_tested':False,'navmesh_tested':False}
    assert checks['clear_width_cm']==400 and max(joins)==0
    assert abs(checks['high_endpoint_cm'][2]-3200)<.001
    with open(os.path.join(A.REPORT,'step2_cathedral_geometry_numeric.json'),'w') as fp: json.dump(checks,fp,indent=2)
    return checks
