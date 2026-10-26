import unreal, math, bisect, collections
import ur_current_repair_inspect as I
B=I.load('scene_before.json')
G=I.unpack('static_geometry.json.zlib')
OLD=[0,14322,29078,43400,57722,72478,86800,101122,115878,130200]
CACHE={}
def xf(b): return unreal.Transform(location=unreal.Vector(*b['loc']),rotation=unreal.Rotator(pitch=b['rot'][0],yaw=b['rot'][1],roll=b['rot'][2]),scale=unreal.Vector(*b['scale']))
def interp(x,knots):
    for (a,b),(c,d) in zip(knots,knots[1:]):
        if x<=c:return b+(d-b)*(x-a)/(c-a)
    a,b=knots[-2];c,d=knots[-1];return d+(x-c)*(d-b)/(c-a)
def radius_at(samples,angle):
    angle=angle%360; keys=sorted(samples)
    j=bisect.bisect_right(keys,angle);a=keys[j-1] if j else keys[-1]-360;b=keys[j] if j<len(keys) else keys[0]+360
    return samples[round(a%360,4)%360]+(samples[round(b%360,4)%360]-samples[round(a%360,4)%360])*(angle-a)/(b-a)
def context(i):
    if i in CACHE:return CACHE[i]
    rings=[b for b in B.values() if b['label'].startswith('E%02d_Floor_'%i) and b['folder'].endswith('/Floors') and '_Ring_' in b['components'][0]['mesh']]
    samples={}
    for b in rings:
        for p in G[b['components'][0]['mesh']]['v']:
            w=xf(b).transform_location(unreal.Vector(*p));r=math.hypot(w.x,w.y);a=math.degrees(math.atan2(w.y,w.x))%360
            a=round(a,4)%360;samples[a]=max(samples.get(a,0),r)
    # Both edges share an angle; outer edge defines the previous deck extent.
    samples={a:r for a,r in samples.items() if r>3600}
    shell=I.shell_world(i-1 if i>1 else 1);segs=I.slice_segments(shell,(i-1)*20000-100 if i>1 else 1000)
    outer={}
    for a in [j*.25 for j in range(1440)]:
        h=I.radial_hits(segs,a)
        # Cardinal door holes are interpolated between their actual jambs, never filled in the shell.
        if h:outer[a]=(max(h)-25 if i>1 else min(h)+2050)
    c={'i':i,'old':samples,'outer':outer,'lo':(i-1)*20000,'hi':i*20000 if i<10 else 199840,'r0':10000 if i<10 else 5000,'r1':10000 if i<9 else 5000,'sweep':180 if i<9 else 360}
    c['rise']=c['hi']-c['lo'];c['oldrise']=(OLD[i]-OLD[i-1]) if i<10 else 7161
    CACHE[i]=c;return c
def floor_radius(c,r,a):
    old=radius_at(c['old'],a);new=radius_at(c['outer'],a);rr=c['r0']
    return interp(r,[(0,0),(3600,3600),(9400,rr-600),(10600,rr+600),(old-1600,new-1600),(old,new)])
def ground_xy(c,p):
    r=math.hypot(p[0],p[1]);a=math.degrees(math.atan2(p[1],p[0]));rn=floor_radius(c,r,a)
    return [rn*math.cos(math.radians(a)),rn*math.sin(math.radians(a))]
def mid(c):return (c['lo']+c['hi'])/2 if c['i']<10 else c['hi']
def elevation(c,u):
    if c['i']==10:return c['lo']+c['rise']*u
    return interp(u,[(0,c['lo']),(17/36,(c['lo']+c['hi'])/2),(19/36,(c['lo']+c['hi'])/2),(1,c['hi'])])
def route(c,u,side=0,dr=0,dz=0):
    r=c['r0']+(c['r1']-c['r0'])*u+dr;a=side+c['sweep']*u
    return [r*math.cos(math.radians(a)),r*math.sin(math.radians(a)),elevation(c,u)+dz]
def rail_line(b):
    p=b['loc'];h=b['scale'][0]*50;a=math.radians(b['rot'][1]);q=math.radians(b['rot'][0]);d=[math.cos(q)*math.cos(a)*h,math.cos(q)*math.sin(a)*h,math.sin(q)*h]
    return [[p[k]-d[k] for k in range(3)],[p[k]+d[k] for k in range(3)]]
def line_pose(b,p,q,width=None,height=None):
    d=[q[k]-p[k] for k in range(3)];run=math.hypot(d[0],d[1]);s=list(b['scale']);s[0]=math.sqrt(sum(v*v for v in d))/100
    if width is not None:s[1]=width/100
    if height is not None:s[2]=height/100
    return {'loc':[(p[k]+q[k])/2 for k in range(3)],'rot':[math.degrees(math.atan2(d[2],run)),math.degrees(math.atan2(d[1],d[0])),0],'scale':s}
def base_pose(b,c):
    p=list(b['loc']);p[2]+=c['lo']-OLD[c['i']-1];return {'loc':p,'rot':b['rot'][:],'scale':b['scale'][:]}
def main_floor_vertices(b,c,p):
    t=xf(b);w=I.vec(t.transform_location(unreal.Vector(*p)));xy=ground_xy(c,w)
    w=[xy[0],xy[1],w[2]+c['lo']-OLD[c['i']-1]]
    return I.vec(xf(base_pose(b,c)).inverse_transform_location(unreal.Vector(*w)))
def spiral_vertices(b,c,p):
    j=round((b['loc'][2]-OLD[c['i']-1])/c['oldrise']*36);side=(b['rot'][1]-j*5)%360
    oldt=xf(b);w=I.vec(oldt.transform_location(unreal.Vector(*p)));a=math.degrees(math.atan2(p[1],p[0]));f=a/5;u=(j+f)/36
    top=max(q[2] for q in G[b['components'][0]['mesh']]['v'] if abs(q[0]-p[0])<.01 and abs(q[1]-p[1])<.01)
    depth=(top-p[2])*b['scale'][2];r=math.hypot(p[0]*b['scale'][0],p[1]*b['scale'][1]);new=route(c,u,side,r-10000,-depth)
    pose=base_pose(b,c);pose['loc']=[0,0,elevation(c,j/36)];pose['rot']=[0,side+c['sweep']*j/36,0]
    return I.vec(xf(pose).inverse_transform_location(unreal.Vector(*new))),pose
