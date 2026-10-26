import unreal, math, collections
import ur_e10_stage2_state as S
import ur_e10_stage2_plan as P
import ur_e09_stage1_verify as V
hit=V.hit
def preserve():
    B=S.load('before.json');aa=S.actors();rows=S.load('proposal.json');changed=set(S.load('changed.json'));errors=[];posechanges=0;meshchanges=0
    if set(aa)!=set(B):errors.append('Actor set changed')
    for p,b in B.items():
        now=S.snap(aa[p]);expected=rows[p]['pose'] if p in rows else b['world']
        if not S.close(now['world'],expected):errors.append([b['label'],'world'])
        if not S.close(now['world'],b['world']):posechanges+=1
        if now['parent']!=b['parent'] or now['folder']!=b['folder'] or now['tags']!=b['tags']:errors.append([b['label'],'organization'])
        if len(now['components'])!=len(b['components']):errors.append([b['label'],'component_count']);continue
        if p not in changed:
            if not S.close(now['root'],b['root']):errors.append([b['label'],'protected_root'])
            if max(abs(x-y) for a,c in zip(now['bounds'],b['bounds']) for x,y in zip(a,c))>.003:errors.append([b['label'],'fresh_protected_bounds'])
        for x,y in zip(b['components'],now['components']):
            for k in ['materials','profile','collision','geometry']:
                if x.get(k)!=y.get(k):errors.append([b['label'],k])
            if x.get('mesh')!=y.get('mesh'):
                meshchanges+=1
                if p not in changed or rows[p]['v'] is None:errors.append([b['label'],'unexpected_mesh'])
            if p not in changed and (not S.close(x['world'],y['world']) or not S.close(x['rel'],y['rel'])):errors.append([b['label'],'protected_component_pose'])
    asseterrors=[];fp=S.load('asset_fingerprints.json')
    for p,b in fp.items():
        sm=unreal.load_asset(p)
        if not sm or S.digest(S.meshgeo(sm))!=b['geometry'] or S.materials(sm)!=b['materials']:asseterrors.append(p)
    ls=S.landscape();fr=S.frozen();by={a.get_actor_label():a for a in aa.values()};shells={n:S.bounds(by[n+'Skin_Wall_01']) for n in ['E01','E02','E08','E09','E10']};base=S.load('baseline_bounds.json')
    dims={'E10_height_10000cm':abs(shells['E10']['size'][2]-10000)<.003,'E10_width_15000cm':abs(shells['E10']['size'][0]-15000)<.003,'E10_bottom_fixed':abs(shells['E10']['min'][2]-base['E10']['min'][2])<.003,'E09_exact_fresh_bounds':max(abs(x-y) for k in ['min','max'] for x,y in zip(shells['E09'][k],base['E09'][k]))<.003,'E09_E10_contiguous':abs(shells['E09']['max'][2]-shells['E10']['min'][2])<.003,'E08_E09_contiguous':abs(shells['E08']['max'][2]-shells['E09']['min'][2])<.003,'total_180000cm':abs(shells['E10']['max'][2]-(-80)-180000)<.003}
    dims['shell_XY_unchanged']=all(abs(x-y)<.003 for k in ['min','max'] for x,y in zip(shells['E10'][k][:2],base['E10'][k][:2]))
    lifts=S.load('lift_proof.json');liftok=all(S.liftproof(aa[p])==h for p,h in lifts.items())
    data={'actors':len(aa),'changed_actors':len(changed),'changed_actor_poses':posechanges,'changed_mesh_references':meshchanges,'protected_actors':len(B)-len(changed),'errors':errors,'original_asset_errors':asseterrors,'original_static_assets_checked':len(fp),'Landscape_unchanged':ls==S.load('landscape.json'),'Landscape_sources':len(ls['sources']),'frozen_unchanged':fr==S.load('frozen.json'),'frozen_assets':len(fr),'dimensions':dims,'shell_bounds_cm':shells,'anchors_cm':{n:S.vec(by[n+'_Anchor_01'].get_actor_location()) for n in ['E09','E10']},'lift_authored_settings_unchanged':liftok,'lifts_checked':len(lifts),'dirty_query':S.dirty(),'map_save_issued':False}
    S.save('preservation.json',data);return data
def circulation():
    aa=S.actors();actors=list(aa.values());B=S.load('before.json');G={};checks=collections.defaultdict(list);endpoints={}
    ignore=[a for a in actors if isinstance(a,(unreal.LandscapeProxy,unreal.PlayerStart,unreal.TargetPoint)) or a.get_class().get_name()=='BP_UrithiruLift_C']
    upper=[]
    for a in actors:
        n=a.get_actor_label()
        if not n.startswith('E10_Ramp_'):continue
        c=S.mesh_role(a);sm=c.get_editor_property('static_mesh');key=sm.get_path_name()
        if key not in G:G[key]=S.meshgeo(sm)
        g=G[key];t=c.get_world_transform();angles=sorted(set(round(math.atan2(v[1],v[0]),7) for v in g['v']));groups=[]
        for angle in angles:
            group=[q for q in g['v'] if abs(math.atan2(q[1],q[0])-angle)<1e-6];rad=[math.hypot(q[0],q[1]) for q in group]
            groups.append((angle,min(rad),max(rad),max(q[2] for q in group)))
        ts=P.triangles([S.vec(t.transform_location(unreal.Vector(*v))) for v in g['v']],g['t'])
        for f in [.1,.5,.9]:
            ang=angles[0]+(angles[-1]-angles[0])*f
            for rf in [.15,.5,.85]:
                rad=groups[0][1]+(groups[0][2]-groups[0][1])*rf;xy=t.transform_location(unreal.Vector(rad*math.cos(ang),rad*math.sin(ang),0));hh=P.hits(xy.x,xy.y,ts);assert hh,n;z=max(hh)
                h=hit([xy.x,xy.y,z+35],[xy.x,xy.y,z-35],ignore)
                checks['ramp_collision_support'].append({'actor':n,'ok':bool(h and abs(h['p'][2]-z)<2),'hit':h})
                if rf==.5 and f==.5:
                    h=hit([xy.x,xy.y,z+120],[xy.x,xy.y,z+120.1],ignore,True)
                    checks['ramp_capsule_headroom'].append({'actor':n,'ok':h is None,'hit':h})
        end=[]
        for ang,r0,r1,top in [groups[0],groups[-1]]:
            rr=(r0+r1)/2;end.append(S.vec(t.transform_location(unreal.Vector(rr*math.cos(ang),rr*math.sin(ang),top))))
        endpoints[n]=end
        checks['ramp_width_1200cm'].append({'actor':n,'ok':all(abs((v[2]-v[1])*S.xf(t)['s'][0]-1200)<.1 for v in groups)})
        b=B[a.get_path_name()];source=next(x['mesh'] for x in b['components'] if 'mesh' in x);old=S.meshgeo(unreal.load_asset(source))
        oldtops={};newtops={}
        for v in old['v']:k=(round(v[0],4),round(v[1],4));oldtops[k]=max(oldtops.get(k,-1e30),v[2])
        for v in g['v']:k=(round(v[0],4),round(v[1],4));newtops[k]=max(newtops.get(k,-1e30),v[2])
        ok=old['t']==g['t'] and len(old['v'])==len(g['v'])
        for v,w in zip(old['v'],g['v']):
            k=(round(v[0],4),round(v[1],4));ok=ok and max(abs(v[j]-w[j]) for j in [0,1])<.003 and abs((oldtops[k]-v[2])-(newtops[k]-w[2]))<.003
        checks['ramp_topology_XY_thickness_preserved'].append({'actor':n,'ok':ok})
    for side in [0,36]:
        for j in range(1,36):
            p=endpoints['E10_Ramp_%02d'%(side+j)][1];q=endpoints['E10_Ramp_%02d'%(side+j+1)][0]
            checks['ramp_seams'].append({'joint':side+j,'ok':abs(p[2]-q[2])<5 and math.hypot(p[0]-q[0],p[1]-q[1])<25,'dz':p[2]-q[2],'xy_gap':math.hypot(p[0]-q[0],p[1]-q[1])})
    for side in [0,180]:
        for r in [2700,3000,3300,3600,3800,4100,4400,4700,5000,5300,5500]:
            x=r*math.cos(math.radians(side));y=r*math.sin(math.radians(side));h=hit([x,y,179630],[x,y,179450],ignore)
            checks['gallery_docking'].append({'side':side,'radius':r,'ok':bool(h and abs(h['p'][2]-179600)<5),'hit':h})
    S.save('circulation_detail.json',dict(checks))
    result={k:{'pass':sum(x['ok'] for x in v),'total':len(v),'failures':[x for x in v if not x['ok']][:8]} for k,v in checks.items()}
    S.save('circulation_summary.json',result);return result
