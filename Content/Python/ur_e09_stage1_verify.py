import unreal, math, collections, json
import ur_e09_stage1_state as S
import ur_e09_stage1_plan as P
W=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
def hit(p,q,ignore,capsule=False):
    if capsule:h=unreal.SystemLibrary.capsule_trace_single(W,unreal.Vector(*p),unreal.Vector(*q),42,96,unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE)
    else:h=unreal.SystemLibrary.line_trace_single(W,unreal.Vector(*p),unreal.Vector(*q),unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE)
    if h is None:return None
    t=h.to_tuple();return {'p':S.vec(t[5]),'n':S.vec(t[7]),'actor':t[9].get_actor_label() if t[9] else None}
def bounds(a):
    c,e=a.get_actor_bounds(False,False);return {'min':[c.x-e.x,c.y-e.y,c.z-e.z],'max':[c.x+e.x,c.y+e.y,c.z+e.z],'size':[2*e.x,2*e.y,2*e.z]}
def preserve():
    B=S.load('before.json');aa=S.actors();rows=S.load('proposal.json');problems=[];changed=S.load('changed.json')
    assert set(aa)==set(B),'Actor added or removed'
    for p,b in B.items():
        a=aa[p];n=S.snap(a);expected=rows[p]['pose'] if p in rows else b['world']
        if not S.close(n['world'],expected):problems.append([b['label'],'world'])
        if n['parent']!=b['parent'] or n['folder']!=b['folder']:problems.append([b['label'],'attachment/folder'])
        old=b['components'];new=n['components']
        if len(old)!=len(new):problems.append([b['label'],'component_count']);continue
        for x,y in zip(old,new):
            if x['materials']!=y['materials'] or x['collision']!=y['collision'] or x['profile']!=y['profile']:problems.append([b['label'],'materials/collision'])
            if x.get('geometry')!=y.get('geometry'):problems.append([b['label'],'dynamic topology/vertices'])
            if p not in changed and x.get('mesh')!=y.get('mesh'):problems.append([b['label'],'protected mesh reference'])
            if b['label'].startswith('E10') and x.get('mesh')!=y.get('mesh'):problems.append([b['label'],'E10 mesh changed'])
            if b['label'].startswith('E10') and not S.close(x['rel'],y['rel']):
                if b['label'] not in ['E10Skin_Wall_01','E10_Anchor_01']:problems.append([b['label'],'E10 shape changed'])
        if p not in changed and not S.close(n['root'],b['root']):problems.append([b['label'],'protected relative transform'])
    for p,h in S.load('lift_proof.json').items():
        if S.liftproof(aa[p])!=h:problems.append([B[p]['label'],'lift authored settings'])
    asset_errors=[]
    for p,b in S.load('asset_fingerprints.json').items():
        sm=unreal.load_asset(p)
        if not sm or S.digest(S.meshgeo(sm))!=b['geometry'] or S.materials(sm)!=b['materials']:asset_errors.append(p)
    ls=S.landscape();fr=S.frozen();lsok=ls==S.load('landscape.json');frok=fr==S.load('frozen.json')
    by={a.get_actor_label():a for a in aa.values()};shells={n:bounds(by[n+'Skin_Wall_01']) for n in ['E08','E09','E10']}
    checks={'E09_100m':abs(shells['E09']['size'][2]-10000)<.003,'E09_300m_width':abs(shells['E09']['size'][0]-30000)<.003,'E09_bottom_fixed':abs(shells['E09']['min'][2]-159920)<.003,'E08_E09_contiguous':abs(shells['E08']['max'][2]-shells['E09']['min'][2])<.003,'E09_E10_contiguous':abs(shells['E09']['max'][2]-shells['E10']['min'][2])<.003,'E10_200m':abs(shells['E10']['size'][2]-20000)<.003}
    data={'actors':len(aa),'world_changed':len(changed),'protected_actors':len(B)-len(changed),'problems':problems,'original_asset_errors':asset_errors,'original_static_assets_checked':len(S.load('asset_fingerprints.json')),'Landscape_unchanged':lsok,'Landscape_sources':len(ls['sources']),'frozen_unchanged':frok,'frozen_assets':len(fr),'shells_cm':shells,'dimensions':checks,'anchors':{n:S.vec(by[n+'_Anchor_01'].get_actor_location()) for n in ['E08','E09','E10']},'lift_settings_hashes_preserved':len(S.load('lift_proof.json')),'map_saved':False,'dirty_maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
    S.save('preservation.json',data);return data
def circulation():
    aa=S.actors();actors=list(aa.values());by={a.get_actor_label():a for a in actors};B=S.load('before.json');G={};checks=collections.defaultdict(list)
    ignore=[a for a in actors if isinstance(a,(unreal.LandscapeProxy,unreal.PlayerStart,unreal.TargetPoint)) or a.get_class().get_name()=='BP_UrithiruLift_C']
    endpoints={};rampdata=[]
    for a in actors:
        n=a.get_actor_label()
        if not n.startswith('E09_Ramp_'):continue
        c=a.get_component_by_class(unreal.StaticMeshComponent);sm=c.get_editor_property('static_mesh');key=sm.get_path_name()
        if key not in G:G[key]=S.meshgeo(sm)
        g=G[key];t=c.get_world_transform();angles=sorted(set(round(math.atan2(v[1],v[0]),7) for v in g['v']));groups=[]
        for angle in angles:
            group=[q for q in g['v'] if abs(math.atan2(q[1],q[0])-angle)<1e-6];rad=[math.hypot(q[0],q[1]) for q in group];top=max(q[2] for q in group)
            groups.append((angle,min(rad),max(rad),top))
        ts=P.triangles([S.vec(t.transform_location(unreal.Vector(*v))) for v in g['v']],g['t'])
        for f in [.1,.5,.9]:
            ang=angles[0]+(angles[-1]-angles[0])*f;near=min(groups,key=lambda q:abs(q[0]-ang))
            for rf in [.15,.5,.85]:
                rad=near[1]+(near[2]-near[1])*rf;xy=t.transform_location(unreal.Vector(rad*math.cos(ang),rad*math.sin(ang),0));z=P.surface(xy.x,xy.y,ts);assert z is not None,n
                p=[xy.x,xy.y,z];h=hit([p[0],p[1],z+35],[p[0],p[1],z-35],ignore)
                checks['ramp_support'].append({'actor':n,'ok':bool(h and abs(h['p'][2]-z)<2),'hit':h})
                if rf==.5 and f==.5:
                    h=hit([p[0],p[1],z+120],[p[0],p[1],z+120.1],ignore,True)
                    checks['ramp_capsule_clearance'].append({'actor':n,'ok':h is None,'hit':h})
        end=[]
        for ang,r0,r1,top in [groups[0],groups[-1]]:
            rr=(r0+r1)/2;end.append(S.vec(t.transform_location(unreal.Vector(rr*math.cos(ang),rr*math.sin(ang),top))))
        endpoints[n]=end
        widths=[(x[2]-x[1])*S.xf(t)['s'][0] for x in groups]
        checks['ramp_width_1200'].append({'actor':n,'ok':max(abs(x-1200) for x in widths)<.1,'widths':widths})
    for side in [0,36]:
        for j in range(1,36):
            p=endpoints['E09_Ramp_%02d'%(side+j)][1];q=endpoints['E09_Ramp_%02d'%(side+j+1)][0]
            checks['ramp_seams'].append({'joint':side+j,'ok':abs(p[2]-q[2])<5 and math.hypot(p[0]-q[0],p[1]-q[1])<25,'dz':p[2]-q[2],'xy_gap':math.hypot(p[0]-q[0],p[1]-q[1])})
    for side in [0,180]:
        for r in [5600,5900,6200,6500,6800,7100,7400,7500]:
            x=r*math.cos(math.radians(side));y=r*math.sin(math.radians(side));h=hit([x,y,165030],[x,y,164850],ignore)
            checks['gallery_docking'].append({'side':side,'radius':r,'ok':bool(h and abs(h['p'][2]-165000)<5),'hit':h})
    for n,a in by.items():
        if n.startswith('E09_Floor_') or (n.startswith('E09_Bridge_') and str(a.get_folder_path()).endswith('/Routes')):
            c=a.get_component_by_class(unreal.StaticMeshComponent);g=S.meshgeo(c.get_editor_property('static_mesh'));t=c.get_world_transform();v=[S.vec(t.transform_location(unreal.Vector(*q))) for q in g['v']];top=max(q[2] for q in v);pts=[q for q in v if abs(q[2]-top)<.01];p=[sum(q[j] for q in pts)/len(pts) for j in range(3)]
            h=hit([p[0],p[1],top+5],[p[0],p[1],top-10],ignore);checks['deck_bridge_support'].append({'actor':n,'ok':bool(h and abs(h['p'][2]-top)<5),'hit':h})
    S.save('circulation_detail.json',dict(checks))
    result={k:{'pass':sum(x['ok'] for x in v),'total':len(v),'failures':[x for x in v if not x['ok']][:8]} for k,v in checks.items()}
    S.save('circulation_summary.json',result);return result
