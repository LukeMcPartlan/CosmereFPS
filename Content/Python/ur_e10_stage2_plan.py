import unreal, math, copy, collections
import ur_e10_stage2_state as S
import ur_e09_stage1_plan as U
triangles=U.triangles
worldverts=U.worldverts
def hits(x,y,ts):
    out=[]
    for a,b,c,d,x0,x1,y0,y1 in ts:
        if not x0-.01<=x<=x1+.01 or not y0-.01<=y<=y1+.01:continue
        u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/d;v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/d
        if min(u,v,1-u-v)>=-1e-5:out.append(u*a[2]+v*b[2]+(1-u-v)*c[2])
    return out
def closest(x,y,z,ts):
    hh=hits(x,y,ts)
    return min(hh,key=lambda h:abs(h-z)) if hh else None
def prepare():
    B=S.load('before.json');G=S.unpack('static_geometry.zlib');scope=S.load('scope.json');out={};supports=[];stat=[]
    shell=next(b for b in B.values() if b['label']=='E10Skin_Wall_01');sp=copy.deepcopy(shell['world'])
    bottom=shell['bounds'][0][2]-shell['bounds'][1][2];height=2*shell['bounds'][1][2];assert abs(bottom-169920)<.003 and abs(height-20000)<.003
    sp['p'][2]=bottom+(sp['p'][2]-bottom)*.5;sp['s'][2]*=.5
    sg=S.unpack('shell_geometry.zlib');roof=triangles(worldverts(shell,sg,sp),sg['t'])
    clearance=[]
    for angle in range(0,360,10):
        x=5600*math.cos(math.radians(angle));y=5600*math.sin(math.radians(angle));hh=[h for h in hits(x,y,roof) if h>170500]
        if hh:clearance.append(min(hh))
    assert clearance,'Cannot establish actual roof clearance'
    gallery=math.floor(min(bottom+10000-300,min(clearance)-250)/100)*100
    main=170000.;oldgallery=189840.;rise=gallery-main;factor=rise/(oldgallery-main)
    assert gallery==179600 and 0<factor<1
    for path in scope:
        b=B[path];n=b['label'];cat=b['folder'].split('/')[-1];p=copy.deepcopy(b['world']);v=None;src=None;kind='fixed'
        if n=='E10Skin_Wall_01':p=sp;kind='shell_fixed_bottom'
        elif cat=='InteriorSpirals' and '_Ramp_' in n:
            src=next(c['mesh'] for c in b['components'] if 'mesh' in c);g=G[src];tops={}
            for q in g['v']:
                k=(round(q[0],4),round(q[1],4));tops[k]=max(tops.get(k,-1e30),q[2])
            lo=min(tops.values());hi=max(tops.values());assert hi-lo>1
            u=(b['world']['p'][2]-main)/(oldgallery-main);j=round(u*36);assert abs(u-j/36)<1e-6 and 0<=j<=35
            p['p'][2]=main+rise*j/36
            v=[[q[0],q[1],(tops[(round(q[0],4),round(q[1],4))]-lo)/(hi-lo)*(rise/36)/p['s'][2]+q[2]-tops[(round(q[0],4),round(q[1],4))]] for q in g['v']]
            kind='ramp_reslope_keep_xy_thickness';w=worldverts(b,{'v':v},p);supports.extend(triangles(w,g['t']))
            stat.append({'label':n,'live_u':u,'target_origin':p['p'][2],'target_top':[min(q[2] for q in w),max(q[2] for q in w)],'source':src})
        elif cat=='InteriorSpirals' and '_Rail_' in n:kind='rail_seat_correct_helix'
        elif cat=='Balconies' or (cat=='InteriorSpirals' and '_Floor_' in n) or (cat=='Routes' and 'blk:S10' in b['tags']):
            p['p'][2]+=gallery-oldgallery;kind='gallery_rigid_dimensions'
        elif cat in ['E10','LiftBays','Cover','Routes','Boundaries','Floors','WalkLandings']:pass
        else:raise ValueError(('Unclassified',n,cat))
        out[path]={'label':n,'pose':p,'source':src,'v':v,'kind':kind}
    # Include upper support decks; select each rail's helix by expected elevation, never highest surface.
    for path,r in out.items():
        b=B[path];n=b['label']
        if b['class']=='StaticMeshActor' and ('_Floor_' in n or '_Bridge_' in n):
            src=next(c['mesh'] for c in b['components'] if 'mesh' in c);g=G[src]
            supports.extend(triangles(worldverts(b,g,r['pose']),g['t']))
    seating=[]
    for path,r in out.items():
        if r['kind']!='rail_seat_correct_helix':continue
        b=B[path];p=r['pose'];t=S.transform(b['world']);a=S.vec(t.transform_location(unreal.Vector(-50,0,0)));z=S.vec(t.transform_location(unreal.Vector(50,0,0)))
        expected=main+(b['world']['p'][2]-main)*factor
        h=[]
        for f in [.02,.5,.98]:
            x=a[0]+(z[0]-a[0])*f;y=a[1]+(z[1]-a[1])*f;h.append(closest(x,y,expected,supports))
        assert all(v is not None for v in h),(b['label'],'No support XY',h)
        assert max(abs(v-expected) for v in h)<800,(b['label'],'Wrong helix',h,expected)
        dz=(h[2]-h[0])/.96;run=math.hypot(z[0]-a[0],z[1]-a[1]);pitch=math.atan2(dz,run)
        q=unreal.Rotator(pitch=math.degrees(pitch),yaw=math.degrees(math.atan2(z[1]-a[1],z[0]-a[0])),roll=0).quaternion()
        p['p'][2]=h[1]+p['s'][2]*50/math.cos(pitch);p['s'][0]=math.hypot(run,dz)/100;p['q']=[q.x,q.y,q.z,q.w]
        seating.append({'label':b['label'],'support':h,'mid_error':abs(h[1]-(h[0]+h[2])/2),'section':[p['s'][1]*100,p['s'][2]*100]})
    changed=[p for p,r in out.items() if not S.close(r['pose'],B[p]['world']) or r['v'] is not None]
    S.save('proposal.json',out);S.save('changed.json',changed);S.save('rail_plan.json',seating);S.save('ramp_plan.json',stat)
    summary={'roles':dict(collections.Counter(r['kind'] for r in out.values())),'changed':len(changed),'variants':len(set(S.digest([r['source'],r['v']]) for r in out.values() if r['v'] is not None)),'mainfloor':main,'gallery':gallery,'roof_samples':len(clearance),'roof_min':min(clearance),'outer_gallery_headroom':min(clearance)-gallery,'shell_bottom':bottom,'shell_top':bottom+10000,'rail_mid_error_max':max(r['mid_error'] for r in seating)}
    S.save('proposal_summary.json',summary);return summary
