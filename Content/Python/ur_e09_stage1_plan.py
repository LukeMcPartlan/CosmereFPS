import unreal, math, copy, collections
import ur_e09_stage1_state as S
def pose(b):return copy.deepcopy(b['world'])
def worldverts(b,g,p=None):
    t=S.transform(p or b['world'])
    return [S.vec(t.transform_location(unreal.Vector(*v))) for v in g['v']]
def triangles(w,tris):
    out=[]
    for ids in tris:
        a,b,c=[w[j] for j in ids];den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-6:continue
        out.append((a,b,c,den,min(a[0],b[0],c[0]),max(a[0],b[0],c[0]),min(a[1],b[1],c[1]),max(a[1],b[1],c[1])))
    return out
def surface(x,y,ts):
    hits=[]
    for a,b,c,d,x0,x1,y0,y1 in ts:
        if not x0-.01<=x<=x1+.01 or not y0-.01<=y<=y1+.01:continue
        u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/d;v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/d
        if min(u,v,1-u-v)>=-1e-5:hits.append(u*a[2]+v*b[2]+(1-u-v)*c[2])
    return max(hits) if hits else None
def prepare():
    B=S.load('before.json');G=S.unpack('static_geometry.zlib');scope=S.load('scope.json');out={};counts=collections.Counter();supports=[];rampstats=[]
    for path in scope:
        b=B[path];n=b['label'];cat=b['folder'].split('/')[-1];p=pose(b);v=None;source=None;kind='unchanged'
        if n=='E09Skin_Wall_01':
            base=b['bounds'][0][2]-b['bounds'][1][2];height=2*b['bounds'][1][2];assert abs(height-20000)<.01
            p['p'][2]=base+(p['p'][2]-base)*.5;p['s'][2]*=.5;kind='E09_shell_fixed_bottom'
        elif n=='E10Skin_Wall_01' or n=='E10_Anchor_01':p['p'][2]-=10000;kind='E10_rigid_root'
        elif b['parent'] and B[b['parent']]['label']=='E10_Anchor_01':p['p'][2]-=10000;kind='E10_inherited_translation'
        elif n=='Pair09_10_S_Lift_01':p['p'][2]-=10000;kind='affected_lift_placement_only'
        elif n.startswith('E09_Ramp_') and cat=='InteriorSpirals':
            source=next(c['mesh'] for c in b['components'] if 'mesh' in c);g=G[source];tops={}
            for q in g['v']:
                key=(round(q[0],4),round(q[1],4));tops[key]=max(tops.get(key,-1e30),q[2])
            # Live origins are already at the intended 100 m route. Only panel top rise remains 200 m profile.
            assert 159999<=p['p'][2]<=170001,(n,p['p'])
            v=[[q[0],q[1],tops[(round(q[0],4),round(q[1],4))]*.5+q[2]-tops[(round(q[0],4),round(q[1],4))]] for q in g['v']]
            kind='E09_panel_reslope_keep_live_thickness'
            w=worldverts(b,{'v':v},p);rampstats.append({'label':n,'top_range':[min(q[2] for q in w),max(q[2] for q in w)]})
        elif n.startswith('E09_Rail_') and cat=='InteriorSpirals':kind='E09_rail_fit_live_support'
        out[path]={'label':n,'pose':p,'source':source,'v':v,'kind':kind}
        if b['class']=='StaticMeshActor' and n.startswith('E09_') and ('_Ramp_' in n or '_Floor_' in n or '_Bridge_' in n):
            src=next(c['mesh'] for c in b['components'] if 'mesh' in c);g=G[src];supports.extend(triangles(worldverts(b,{'v':v or g['v']},p),g['t']))
    seating=[]
    for path,r in out.items():
        if r['kind']!='E09_rail_fit_live_support':continue
        b=B[path];p=r['pose'];t=S.transform(b['world']);a=S.vec(t.transform_location(unreal.Vector(-50,0,0)));z=S.vec(t.transform_location(unreal.Vector(50,0,0)))
        h=[]
        for f in [.02,.5,.98]:
            x=a[0]+(z[0]-a[0])*f;y=a[1]+(z[1]-a[1])*f;h.append(surface(x,y,supports))
        assert all(x is not None for x in h),(b['label'],'Unsupported rail XY',h)
        rise=(h[2]-h[0])/.96;run=math.hypot(z[0]-a[0],z[1]-a[1]);pitch=math.atan2(rise,run)
        center=h[1]+b['world']['s'][2]*50/math.cos(pitch)
        rot=unreal.Rotator(pitch=math.degrees(pitch),yaw=math.degrees(math.atan2(z[1]-a[1],z[0]-a[0])),roll=0).quaternion()
        p['p'][2]=center;p['s'][0]=math.hypot(run,rise)/100;p['q']=[rot.x,rot.y,rot.z,rot.w]
        seating.append({'label':b['label'],'support_z':h,'mid_linear_error':abs(h[1]-(h[0]+h[2])/2),'pitch':math.degrees(pitch),'section_cm':[p['s'][1]*100,p['s'][2]*100]})
    for r in out.values():counts[r['kind']]+=1
    S.save('proposal.json',out);S.save('rail_plan.json',seating);S.save('ramp_plan.json',rampstats)
    changed=[p for p,r in out.items() if not S.close(r['pose'],B[p]['world']) or r['v'] is not None]
    S.save('changed.json',changed)
    return {'roles':dict(counts),'changed':len(changed),'ramps':len(rampstats),'rail_fit':len(seating),'rail_mid_error_max':max(x['mid_linear_error'] for x in seating),'new_variant_count':len(set(S.digest([r['source'],r['v']]) for r in out.values() if r['v'] is not None)),'E09_main_gallery_cm':[160000,165000],'E10_main_gallery_cm':[170000,189840]}
