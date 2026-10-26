import unreal,math
import ur_artkit as A
Q=unreal.GeometryScript_MeshQueries
L=unreal.GeometryScript_List

def source(a):
    d=a.dynamic_mesh_component.get_dynamic_mesh()
    v=L.convert_vector_list_to_array(Q.get_all_vertex_positions(d,False)[1]);t=L.convert_triangle_list_to_array(Q.get_all_triangle_indices(d,False)[1])
    return [(p.x,p.y,p.z) for p in v],[(p.x,p.y,p.z) for p in t if p.x>=0]

def field():
    h={}
    for a in A.EAS.get_all_level_actors():
        if isinstance(a,unreal.DynamicMeshActor) and a.get_actor_label().startswith('Background_Floor_'):
            v,t=source(a)
            for x,y,z in v:
                k=(round(x/2000),round(y/2000));h[k]=max(h.get(k,-1e9),z)
    return h

def height(h,x,y):
    x=max(-130000,min(130000,x));y=max(-130000,min(130000,y));gx=x/2000;gy=y/2000;ix=math.floor(gx);iy=math.floor(gy);u=gx-ix;v=gy-iy
    return sum(h.get((ix+i,iy+j),-360)*(u if i else 1-u)*(v if j else 1-v) for i in [0,1] for j in [0,1])

def edge():
    A.world_check();h=field();total=0
    # Closed, irregular escarpment tiles cover the previous straight square boundary.
    for sector in range(12):
        g=A.Geo();v=[];t=[];n=48;m=18
        for layer in [0,1]:
            for j in range(n+1):
                a=math.radians((sector+j/n)*30);co,si=math.cos(a),math.sin(a);norm=max(abs(co),abs(si));outer=140000+1300*math.sin(a*17)+600*math.cos(a*31)
                for i in range(m+1):
                    u=i/m;d=121000+(outer-121000)*u;x=d*co/norm;y=d*si/norm
                    base=height(h,x,y);ridge=math.sin(math.pi*u)**2*(1200+900*math.sin(a*9)**2)
                    z=base+ridge+120*math.sin(a*71+u*26)*math.sin(math.pi*u)-3200*u**4-layer*180
                    v.append((x,y,z))
        count=(n+1)*(m+1)
        def q(a,b,c,d):t.extend([(a,b,c),(a,c,d)])
        for j in range(n):
            for i in range(m):
                a=j*(m+1)+i;b=a+1;c=b+m+1;d=c-1
                q(a,b,c,d);q(a+count,d+count,c+count,b+count)
        for j in range(n):
            a=j*(m+1);b=a+m+1;q(a,b,b+count,a+count)
            a+=m;b+=m;q(a,a+count,b+count,b)
        for i in range(m):
            a=i;b=i+1;q(a,a+count,b+count,b)
            a=n*(m+1)+i;b=a+1;q(a,b,b+count,a+count)
        g.add(v,t);g.actor('ArtSite_RockApron_%02d'%(sector+1),'Site','rock');total+=len(t)
    return {'edge_tiles':12,'triangles':total,'max_xy_abs_cm':141900,'collision':'None; legacy bearing remains'}

def mountains():
    results=[]
    for a in list(A.EAS.get_all_level_actors()):
        if not isinstance(a,unreal.DynamicMeshActor) or not a.get_actor_label().startswith('Background_Floor_'):continue
        src,tri=source(a);g=A.Geo()
        for ia,ic,ib in tri:
            p,q,r=src[ia],src[ib],src[ic]
            cx=(p[0]+q[0]+r[0])/3;cy=(p[1]+q[1]+r[1])/3;cz=(p[2]+q[2]+r[2])/3
            if cz<3500 or math.hypot(cx,cy)<47500 or (cx>20000 and abs(cy)<43000):continue
            if max(p[2],q[2],r[2])-min(p[2],q[2],r[2])<600:continue
            v=[];t=[];ids={};n=5
            for i in range(n+1):
                for j in range(n+1-i):
                    u=i/n;w=j/n;xyz=[p[k]*(1-u-w)+q[k]*u+r[k]*w for k in range(3)]
                    x,y,z=xyz;relief=85+45*math.sin(x/430+y/730)+30*math.sin(x/970-y/390)
                    ids[i,j]=len(v);v.append((x,y,z+relief))
            for i in range(n):
                for j in range(n-i):
                    t.append((ids[i,j],ids[i+1,j],ids[i,j+1]))
                    if j<n-i-1:t.append((ids[i+1,j],ids[i+1,j+1],ids[i,j+1]))
            g.add(v,t)
        if g.t:
            suffix=a.get_actor_label().split('_')[-1];g.actor('ArtMountains_Relief_'+suffix,'Mountains','rock');results.append({'source':a.get_actor_label(),'triangles':len(g.t)})
    return results

def plaza_transition():
    h=field();g=A.Geo();v=[];t=[];nx=28;ny=80
    for j in range(ny+1):
        y=-38000+j*950
        for i in range(nx+1):
            u=i/nx;x=92500+16000*u;existing=height(h,x,y);top=-2250 if abs(y)<29000 else existing
            z=top*(1-u)+existing*u+90*math.sin(x/1100+y/900)*math.sin(math.pi*u)
            if x<97500 and abs(y)<27500:z=-2390
            v.append((x,y,z))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;b=a+1;c=b+nx+1;d=c-1;t.extend([(a,b,c),(a,c,d)])
    g.add(v,t);g.actor('ArtFrontPlaza_ScreeTransition_01','FrontPlaza','scree')
    return {'transition_triangles':len(t),'route_collision_unchanged':True}
