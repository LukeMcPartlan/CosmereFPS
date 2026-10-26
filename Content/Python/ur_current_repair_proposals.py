import unreal, math, re, collections
import ur_current_repair_inspect as I
import ur_current_repair_mapping as M

def proposal(b,c):
    cat=b['folder'].split('/')[-1];name=b['label'];p=M.base_pose(b,c);verts=None
    if b['class']=='BP_UrithiruTierAnchor_C':p['loc'][2]=c['lo'];return p,None
    if b['class'] in ['TargetPoint','PlayerStart']:
        if 'WalkLanding' in name or b['class']=='PlayerStart':p['loc'][:2]=M.ground_xy(c,b['loc'])
        return p,None
    assert b['class']=='StaticMeshActor',(name,b['class'])
    mesh=b['components'][0]['mesh'];g=M.G[mesh];t=M.xf(b);nt=M.xf(p)
    if cat=='Floors':
        verts=[M.main_floor_vertices(b,c,q) for q in g['v']]
    elif cat=='InteriorSpirals' and '_Ramp_' in name:
        pairs=[M.spiral_vertices(b,c,q) for q in g['v']];verts=[v[0] for v in pairs];p=pairs[0][1]
    elif cat=='InteriorSpirals' and abs(b['rot'][0])>.1:
        u=(b['loc'][2]-M.OLD[c['i']-1]-110)/c['oldrise'];oldr=math.hypot(b['loc'][0],b['loc'][1]);oldangle=math.degrees(math.atan2(b['loc'][1],b['loc'][0]));side=(oldangle-u*180)%360
        side=round(side/180)*180;dr=oldr/math.cos(math.radians(2.5))-10000
        p0=M.route(c,u-1/72,side,dr,110);p1=M.route(c,u+1/72,side,dr,110)
        p=M.line_pose(b,p0,p1);p['scale'][0]+=0.16
    elif cat=='InteriorSpirals':
        assert c['i']==10,name
        a=round(math.degrees(math.atan2(b['loc'][1],b['loc'][0]))/180)*180;delta=c['sweep']-180;co=math.cos(math.radians(delta));si=math.sin(math.radians(delta))
        d=[b['loc'][0]-10000*math.cos(math.radians(a)),b['loc'][1]-10000*math.sin(math.radians(a))]
        p['loc']=[c['r1']*math.cos(math.radians(a+delta))+co*d[0]-si*d[1],c['r1']*math.sin(math.radians(a+delta))+si*d[0]+co*d[1],c['hi']+b['loc'][2]-M.OLD[9]-7161];p['rot'][1]+=delta
    elif cat=='Balconies':
        delta=(c['sweep']-180)/2 if c['i']<10 else 0;shift=(c['r0']+c['r1'])/2-10000 if c['i']<10 else c['r1']-10000
        p['loc'][2]=M.mid(c)+(b['loc'][2]-(M.OLD[c['i']-1]+c['oldrise']/2 if c['i']<10 else M.OLD[9]+7161));p['rot'][1]+=delta
        if '_Floor_' in name:
            nt=M.xf(p);verts=[]
            for q in g['v']:
                r=math.hypot(q[0]*b['scale'][0],q[1]*b['scale'][1]);a=math.atan2(q[1],q[0]);rn=r+shift
                verts.append([rn*math.cos(a)/b['scale'][0],rn*math.sin(a)/b['scale'][1],q[2]])
        else:
            r=math.hypot(b['loc'][0],b['loc'][1]);angle=math.atan2(b['loc'][1],b['loc'][0])+math.radians(delta);rn=r+shift*math.cos(math.radians(7.5));p['loc'][:2]=[rn*math.cos(angle),rn*math.sin(angle)];p['scale'][0]*=rn/r
    elif cat=='Routes' and any(s.startswith('blk:S') for s in b['tags']):
        oldangle=math.degrees(math.atan2(b['loc'][1],b['loc'][0]));oldmid=90 if c['i']<10 else 180;side=round((oldangle-oldmid)/180)*180;u=.5 if c['i']<10 else 1
        aa=side+c['sweep']*u;r=c['r0']+(c['r1']-c['r0'])*u;inner=r-1200-40;outer=r-600+40
        pp=[inner*math.cos(math.radians(aa)),inner*math.sin(math.radians(aa)),M.mid(c)-40];qq=[outer*math.cos(math.radians(aa)),outer*math.sin(math.radians(aa)),M.mid(c)-40];p=M.line_pose(b,pp,qq)
    elif cat=='Routes':
        axis=[math.cos(math.radians(b['rot'][1])),math.sin(math.radians(b['rot'][1]))];perp=[-axis[1],axis[0]];nt=M.xf(p);verts=[]
        for q in g['v']:
            w=I.vec(t.transform_location(unreal.Vector(*q)));along=w[0]*axis[0]+w[1]*axis[1];across=w[0]*perp[0]+w[1]*perp[1]
            rn=M.floor_radius(c,abs(along),b['rot'][1]+(180 if along<0 else 0));rn=rn if along>=0 else -rn
            nw=[rn*axis[0]+across*perp[0],rn*axis[1]+across*perp[1],w[2]+c['lo']-M.OLD[c['i']-1]];verts.append(I.vec(nt.inverse_transform_location(unreal.Vector(*nw))))
    elif cat=='Boundaries':
        ends=M.rail_line(b);new=[]
        for q in ends:
            xy=M.ground_xy(c,q);new.append(xy+[q[2]+c['lo']-M.OLD[c['i']-1]])
        p=M.line_pose(b,*new)
    elif cat in ['Cover','Spawns']:
        p['loc'][:2]=M.ground_xy(c,b['loc'])
    elif cat=='LiftBays':pass
    else:raise ValueError(('Unclassified owned geometry',name,cat))
    if verts is not None and max(abs(a-bb) for q,r in zip(verts,g['v']) for a,bb in zip(q,r))<.001:verts=None
    return p,verts

def prepare(tiers):
    out={};summary=[];paths=set(I.load('target_paths.json'))
    for i in tiers:
        c=M.context(i);rows=[]
        for path,b in M.B.items():
            if path not in paths or b['class']=='BP_UrithiruLift_C':continue
            owned=b['label'].startswith('E%02d_'%i) or 'zone:E%02d'%i in b['tags']
            if not owned:continue
            p,v=proposal(b,c);row={'path':path,'label':b['label'],'pose':p,'vertices':v,'tier':i,'source_mesh':b['components'][0].get('mesh') if b['components'] else None}
            assert all(math.isfinite(x) for k in ['loc','rot','scale'] for x in p[k]),b['label']
            if v is not None:assert all(math.isfinite(x) for q in v for x in q),b['label']
            rows.append(row)
        out.update({r['path']:r for r in rows})
        summary.append({'tier':i,'actors':len(rows),'mesh_variants_needed':len(set(I.digest([r['source_mesh'],[[round(x,4) for x in q] for q in r['vertices']]]) for r in rows if r['vertices'] is not None)),'floor_z':c['lo'],'ramp_top_z':c['hi'],'spiral_sweep':c['sweep'],'spiral_radii':[c['r0'],c['r1']],'outer_deck_radii':[min(c['outer'].values()),max(c['outer'].values())]})
    I.save('proposals.json',out);I.save('proposal_summary.json',summary);return summary
