import unreal, math, collections
import ur_e10_stage2_state as S
import ur_e10_stage2_plan as P
import ur_e09_stage1_state as U
import ur_e09_stage1_verify as V
hit=V.hit
def run():
    aa=S.actors();actors=list(aa.values());by={a.get_actor_label():a for a in actors};B=S.load('before.json');R=S.load('proposal.json');checks=collections.defaultdict(list);cache={};supports={9:[],10:[]}
    ignore=[a for a in actors if isinstance(a,(unreal.TargetPoint,unreal.PlayerStart,unreal.LandscapeProxy)) or a.get_class().get_name()=='BP_UrithiruLift_C']
    rails=[a for a in actors if '_Rail_' in a.get_actor_label()];no_rails=ignore+rails
    for a in actors:
        n=a.get_actor_label()
        if not n.startswith(('E09_','E10_')) or not isinstance(a,unreal.StaticMeshActor):continue
        if not any(k in n for k in ['_Floor_','_Bridge_','_Ramp_']):continue
        c=S.mesh_role(a);sm=c.get_editor_property('static_mesh');key=sm.get_path_name()
        if key not in cache:cache[key]=S.meshgeo(sm)
        g=cache[key];t=c.get_world_transform();w=[S.vec(t.transform_location(unreal.Vector(*v))) for v in g['v']];tier=int(n[1:3]);ts=P.triangles(w,g['t']);supports[tier].extend(ts)
        if tier==10 and a.get_path_name() in R and R[a.get_path_name()]['kind']=='gallery_rigid_dimensions':
            z=max(v[2] for v in w);top=[v for v in w if abs(v[2]-z)<.01];pt=[sum(v[j] for v in top)/len(top) for j in range(3)]
            h=hit([pt[0],pt[1],z+10],[pt[0],pt[1],z-20],ignore);block=hit([pt[0],pt[1],z+120],[pt[0],pt[1],z+120.1],ignore,True)
            checks['upper_deck_support'].append({'actor':n,'ok':bool(h and abs(h['p'][2]-179600)<2),'hit':h})
            checks['upper_deck_capsule_headroom'].append({'actor':n,'ok':block is None,'block':block})
        if tier==9 and '_Ramp_' in n:
            tops=[v for v in w if abs(v[2]-min(q[2] for q in w))>1];pt=[sum(v[j] for v in w)/len(w) for j in range(3)];z=max(P.hits(pt[0],pt[1],ts))
            h=hit([pt[0],pt[1],z+35],[pt[0],pt[1],z-35],ignore);block=hit([pt[0],pt[1],z+120],[pt[0],pt[1],z+120.1],ignore,True)
            checks['E09_fresh_ramp_support'].append({'actor':n,'ok':bool(h and abs(h['p'][2]-z)<2),'hit':h})
            checks['E09_fresh_capsule_headroom'].append({'actor':n,'ok':block is None,'block':block})
    for a in rails:
        n=a.get_actor_label()
        if not n.startswith(('E09_','E10_')):continue
        tier=int(n[1:3]);t=a.get_actor_transform();b=B[a.get_path_name()];s=S.xf(t)['s'];old=b['world']['s'];newcat=str(a.get_folder_path()).split('/')[-1]
        checks['E%02d_rail_sections_preserved'%tier].append({'actor':n,'ok':abs(s[1]-old[1])<1e-6 and abs(s[2]-old[2])<1e-6,'section_cm':[100*s[1],100*s[2]]})
        for f in [-45,0,45]:
            q=S.vec(t.transform_location(unreal.Vector(f,0,-50)));z=P.closest(q[0],q[1],q[2],supports[tier]);h=hit([q[0],q[1],q[2]+15],[q[0],q[1],q[2]-15],no_rails)
            gap=None if z is None else q[2]-z
            checks['E%02d_rail_seating'%tier].append({'actor':n,'ok':bool(gap is not None and abs(gap)<5 and h and abs(h['p'][2]-q[2])<5),'gap':gap,'hit':h})
    # Replay coordinates as NEW traces, not old proof. Lower roof and stops are protected fixed interfaces.
    previous=U.load('final_interface_detail.json');static_ignore=ignore+[a for a in actors if isinstance(a,unreal.StaticMeshActor)]
    for d in previous['roof_bearing_annulus']:
        tier=d['tier'];x=d['radius']*math.cos(math.radians(d['angle']));y=d['radius']*math.sin(math.radians(d['angle']));z=160000 if tier==9 else 170000
        h=hit([x,y,z-79],[x,y,z-300],static_ignore)
        checks['fresh_roof_bearing_annulus'].append({'tier':tier,'angle':d['angle'],'ok':bool(h and abs(h['p'][2]-(z-80))<.1),'hit':h})
    for tier,z in [(9,160000),(10,170000)]:
        for side,sgn in [('N',-1),('S',1)]:
            for x in [200,450,700]:
                block=hit([x,sgn*1800,z+104],[x,sgn*2400,z+104],ignore,True);h=hit([x,sgn*2100,z+30],[x,sgn*2100,z-100],ignore)
                checks['fresh_lift_outer_boarding'].append({'tier':tier,'side':side,'x':x,'ok':block is None and bool(h and abs(h['p'][2]-z)<2),'block':block,'floor':h})
    for a in actors:
        n=a.get_actor_label()
        if n.startswith(('E09_WalkLanding_','E10_WalkLanding_')):
            q=S.vec(a.get_actor_location());z=160000 if n.startswith('E09') else 170000;h=hit([q[0],q[1],z+30],[q[0],q[1],z-100],ignore)
            checks['fresh_walking_thresholds'].append({'actor':n,'ok':bool(h and abs(h['p'][2]-z)<=45),'hit':h})
        if a.get_class().get_name()=='BP_UrithiruLift_C':
            bb=S.bounds(a);size=bb['size'];ps=S.vec(a.get_editor_property('PlatformSizeCm'));off=a.get_editor_property('PivotToTopOffset')
            checks['all_20_lift_platforms'].append({'actor':n,'ok':max(abs(x-y) for x,y in zip(size,[800,1560,80]))<.01 and ps==[800,1560,80] and abs(off-40)<1e-6,'size_cm':size})
    for tier,z,radius in [(9,160000,15000),(10,170000,7350)]:
        for sign in [-1,1]:
            block=hit([sign*(radius-400),0,z+104],[sign*(radius+400),0,z+104],ignore,True)
            checks['fresh_shell_door_capsules'].append({'tier':tier,'side':sign,'ok':block is None,'block':block})
    for path,r in R.items():
        if r['kind']=='fixed':continue
        a=aa[path];b=S.bounds(a)
        checks['E10_upper_envelope'].append({'actor':r['label'],'ok':b['max'][2]<=179920.003,'max_z':b['max'][2]})
    S.save('interface_detail.json',dict(checks));summary={k:{'pass':sum(v['ok'] for v in vv),'total':len(vv),'failures':[v for v in vv if not v['ok']][:8]} for k,vv in checks.items()}
    S.save('interface_summary.json',summary);return summary
