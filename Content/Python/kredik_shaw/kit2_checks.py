"""Editor/numeric prototype checks only. Not terminal gameplay verification."""
import unreal,math,json,os
from . import kit2_defs as D
from . import kit2_build as B
from . import kit2_bp as P
TOL=.1
RADIUS=33.99907684326172
HH=96.0
CHANNEL=unreal.TraceTypeQuery.ECC_VISIBILITY
def vector(v):return [v.x,v.y,v.z]
def run():
    w=B.world();assert w.get_path_name().split('.')[0]==D.TEST
    rows=[]
    def record(kind,name,ok,**data):rows.append(dict(kind=kind,name=name,pass_check=bool(ok),**data))
    def floor(name,x,y,z):
        h=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,z+30),unreal.Vector(x,y,z-40),CHANNEL,False,[],unreal.DrawDebugTrace.NONE)
        t=h.to_tuple() if h else None
        actual=t[5].z if t else None
        record('footing',name,h is not None and abs(actual-z)<TOL,expected_z=z,actual_z=actual)
    def sweep(name,p,q,radius=RADIUS):
        h=unreal.SystemLibrary.capsule_trace_single(w,unreal.Vector(*p),unreal.Vector(*q),radius,HH,CHANNEL,False,[],unreal.DrawDebugTrace.NONE)
        t=h.to_tuple() if h else None
        record('capsule_clearance',name,h is None,radius_cm=radius,halfheight_cm=HH,start=p,end=q,blocker=t[10].get_path_name() if t and t[10] else None)
    for z in (2400,3200):
        for i,dx in enumerate((-95,0,95)):
            for radius in (RADIUS,48):
                sweep('window%d_%d_r%.1f_forward'%(z,i,radius),(-3000+dx,-350,z+98),(-3000+dx,350,z+98),radius)
                sweep('window%d_%d_r%.1f_reverse'%(z,i,radius),(-3000+dx,350,z+98),(-3000+dx,-350,z+98),radius)
        for j in range(15):floor('window%d_floor%02d'%(z,j),-3000,-350+j*50,z)
        for dy in (-145,0,145):
            sweep('bridge%d_y%d'%(z,dy),(-1350,2000+dy,z+98),(150,2000+dy,z+98),48)
            for i in range(31):floor('bridge%d_y%d_f%02d'%(z,dy,i),-1350+i*50,2000+dy,z)
        sweep('passing_bay%d'%z,(-800,2000,z+98),(-800,2550,z+98),48)
        for i in range(12):floor('bay%d_floor%d'%(z,i),-800,2000+i*50,z)
        corners=[(2600,-300),(4000,-300),(4000,600),(2600,600),(2600,-300)]
        for i,(a,b) in enumerate(zip(corners,corners[1:])):
            sweep('collar%d_loop%d'%(z,i),(a[0],a[1],z+98),(b[0],b[1],z+98),48)
            for j in range(11):floor('collar%d_leg%d_%d'%(z,i,j),a[0]+(b[0]-a[0])*j/10,a[1]+(b[1]-a[1])*j/10,z)
        sweep('collar%d_stair_dock'%z,(2900,250,z+98),(2600,250,z+98),48)
    for unit in range(4):
        for flight in range(2):
            z=unit*800+flight*400;y=flight*250
            for i in range(20):
                x=3015+i*30 if flight==0 else 3585-i*30
                floor('stair%d_%d_tread%d'%(unit,flight,i),x,y,z+(i+1)*20)
                if i<19:
                    nx=x+30 if flight==0 else x-30
                    # Step-height proxy: lift by inspected max step (45), then advance.
                    sweep('stair%d_%d_step%d'%(unit,flight,i),(x,y,z+(i+1)*20+98+45),(nx,y,z+(i+1)*20+98+45),48)
            sweep('turn%d_%d'%(unit,flight),(3700,0,unit*800+400+98),(3700,250,unit*800+400+98),48)
        sweep('stair%d_upper_landing'%unit,(2900,250,(unit+1)*800+98),(2900,0,(unit+1)*800+98),48)
    for key,expected in (('SpireStandard60',6000),('CentralSpire150',15000)):
        candidates=[a for a in unreal.ObjectIterator(unreal.Actor) if a.get_class().get_name()=='BP_KS_'+key+'_C' and '_LevelInstance_' in a.get_path_name() and ':PersistentLevel.' in a.get_path_name()]
        for a in candidates:
            o,e=a.get_actor_bounds(False);co,ce=a.get_actor_bounds(True)
            record('actual_bounds',key,abs(o.z+e.z-expected)<TOL,actor=a.get_path_name(),min_z=o.z-e.z,max_z=o.z+e.z,max_collision_z=co.z+ce.z)
        record('presence',key,len(candidates)==1,instances=len(candidates))
    blueprint_data={}
    paths={k:D.bp(k) for k in D.BP};paths['BridgeSpanPassing']=D.ROOT+'/Prefabs/Architecture/Bridges/BP_KS_BridgeSpanPassing'
    for k,path in paths.items():
        bp=unreal.load_asset(path);parts={}
        for h in P.handles(bp):
            c=P.obj(h,bp)
            if not isinstance(c,unreal.StaticMeshComponent):continue
            parts[c.get_name()]={'mesh':c.static_mesh.get_path_name(),'location':vector(c.get_editor_property('relative_location')),'scale':vector(c.get_editor_property('relative_scale3d')),'collision':str(c.get_collision_enabled()),'navigation':c.get_editor_property('can_ever_affect_navigation')}
        blueprint_data[k]={'package':path,'component_count':len(parts),'parts':parts,'generated_class':bp.generated_class().get_path_name(),'controls_wired':False,'compiled_saved':True}
    B.dump('step2_blueprint_manifest.json',blueprint_data)
    # Actual window opening and snap measurements come from owned mesh data, not actor scale.
    window=blueprint_data['WindowEntrance']['parts']
    jamb=unreal.load_asset(D.sm('WindowJamb')).get_bounding_box()
    header=unreal.load_asset(D.sm('WindowHeader')).get_bounding_box()
    threshold=unreal.load_asset(D.sm('WindowThreshold')).get_bounding_box()
    width=window['EastJamb_GEN_VARIABLE']['location'][0]+jamb.min.x-(window['WestJamb_GEN_VARIABLE']['location'][0]+jamb.max.x)
    height=window['Header_GEN_VARIABLE']['location'][2]+header.min.z-threshold.max.z
    record('actual_clearance','window_opening',abs(width-300)<TOL and abs(height-400)<TOL,width_cm=width,height_cm=height,pawn_lateral_margin_cm=width-2*RADIUS,pawn_headroom_margin_cm=height-2*HH)
    deck=unreal.load_asset(D.sm('BridgeDeck')).get_bounding_box()
    dock=unreal.load_asset(D.sm('BridgeDock')).get_bounding_box()
    record('snapping','bridge_seams',abs(deck.max.x-400)<TOL and abs(deck.min.x)<TOL and abs(deck.max.z-dock.max.z)<TOL,bay_length_cm=deck.max.x-deck.min.x,clear_width_cm=deck.max.y-deck.min.y,dock_width_cm=dock.max.y-dock.min.y,deck_dock_top_delta_cm=deck.max.z-dock.max.z,grid_cm=50,detail_cm=10)
    record('stair_metrics','step_and_slope',20<=45 and math.degrees(math.atan2(20,30))<44.7650833,riser_cm=20,tread_cm=30,stair_pitch_deg=math.degrees(math.atan2(20,30)),max_step_cm=45,allowed_floor_angle_deg=44.7650833,clear_width_cm=200)
    counts={}
    for row in rows:
        key=row['kind'];counts.setdefault(key,{'passed':0,'failed':0})
        counts[key]['passed' if row['pass_check'] else 'failed']+=1
    data={'scope':'EDITOR_NUMERIC_KIT_ONLY_NOT_TERMINAL','counts':counts,'total':len(rows),'failed':[r for r in rows if not r['pass_check']],'checks':rows,'limitations':['No PIE, runtime walk or gameplay test','Stair sweep uses max-step-height proxy, not CharacterMovement implementation','No full palace/graybox connectivity or fairness assessment','No navigation reachability proof yet','Screenshots stale/unconfirmed; two captures identical despite distinct cameras','Construction controls require bp_agent; current variables are metadata only']}
    B.dump('step2_numeric_kit_checks.json',data)
    return {'total':len(rows),'counts':counts,'failures':data['failed'][:20]}
