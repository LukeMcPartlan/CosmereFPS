import unreal,math,os,json
import ur_artkit as A

def walls():
    for sector in range(12):
        g=A.Geo();v=[];t=[];n=36;nz=26;start=max(7,sector*30);end=min(353,(sector+1)*30)
        for layer in [0,1]:
            for j in range(nz+1):
                z=-14100+j*7400/nz
                for i in range(n+1):
                    ang=start+(end-start)*i/n;a=math.radians(ang)
                    relief=140+130*math.sin(a*19+z/600)**2+90*math.sin(a*41-z/350)**2
                    r=23870-relief+layer*410;v.append(A.polar(ang,r,z))
        count=(n+1)*(nz+1)
        def q(a,b,c,d):t.extend([(a,b,c),(a,c,d)])
        for j in range(nz):
            for i in range(n):
                a=j*(n+1)+i;b=a+1;c=b+n+1;d=c-1
                q(a,d,c,b);q(a+count,b+count,c+count,d+count)
        for i in range(n):
            q(i,i+1,i+1+count,i+count);a=nz*(n+1)+i;q(a,a+count,a+count+1,a+1)
        for j in range(nz):
            a=j*(n+1);b=a+n+1;q(a,a+count,b+count,b);a+=n;b+=n;q(a,b,b+count,a+count)
        g.add(v,t);g.actor('ArtCrystalCavern_Rock_%02d'%(sector+1),'CrystalCavern','rock')
    roof=A.Geo();v=[(0,0,-4340)];t=[];n=288;nr=30
    for j in range(1,nr+1):
        r=23850*j/nr
        for i in range(n):
            a=2*math.pi*i/n;z=-7200+3000*(1-r/25000)-140-100*math.sin(a*23+r/900)**2
            v.append((r*math.cos(a),r*math.sin(a),z))
    for i in range(n):t.append((0,1+(i+1)%n,1+i))
    for j in range(nr-1):
        for i in range(n):
            a=1+j*n+i;b=1+j*n+(i+1)%n;c=b+n;d=a+n;t.extend([(a,d,c),(a,c,b)])
    roof.add(v,t);roof.actor('ArtCrystalCavern_Ceiling_01','CrystalCavern','rock')
    return {'wall_sectors':12,'ceiling_triangles':len(t),'portal_clear_angle_deg':14}

def floor_and_crystals():
    g=A.Geo();v=[(0,0,-13999)]+[A.polar(i*360/288,23800,-13999) for i in range(288)];t=[(0,i+1,1+(i+1)%288) for i in range(288)]
    g.add(v,t);g.actor('ArtCrystalCavern_Paving_01','CrystalCavern','interior')
    bronze=A.Geo();cyan=A.Geo()
    for r in [2700,10500]:bronze.annulus([(r,-13998),(r+24,-13998),(r+24,-13996),(r,-13996)],n=192)
    for s in [-1,1]:cyan.bevel((12500,s*1150,-13997),(16000,14,2),.4)
    bronze.actor('ArtCrystalCavern_FloorTrim_01','CrystalCavern','bronze');cyan.actor('ArtCrystalCavern_RouteInlay_01','CrystalCavern','cyan')
    sites=[(-17000,-9500,650,3400),(-18000,5500,800,5100),(-14000,14500,550,3100),(-6000,17500,900,5000),(9000,17500,700,4300),(17000,11000,700,3900),(18000,-9000,800,4700),(6000,-17500,550,3400),(-7500,-18500,500,2700)]
    for k,(x,y,r,h) in enumerate(sites,1):
        socket=A.Geo();vs=[(x,y,-13995)];ts=[];n=18
        for j in range(n):
            ang=j*2*math.pi/n;rr=r*(2.6+.5*math.sin(j*4.7+k));vs.append((x+rr*math.cos(ang),y+rr*math.sin(ang),-13998))
        for j in range(n):
            ang=j*2*math.pi/n;rr=r*(1.3+.2*math.cos(j*3.1+k));vs.append((x+rr*math.cos(ang),y+rr*math.sin(ang),-13750+80*math.sin(j+k)))
        for j in range(n):
            a=1+j;b=1+(j+1)%n;ts.extend([(a,b,b+n),(a,b+n,a+n),(a+n,b+n,0)])
        socket.add(vs,ts);socket.actor('ArtCrystalCavern_MineralSocket_%02d'%k,'CrystalCavern','rock')
    return {'original_crystals_preserved':27,'modeled_mineral_sockets':9,'floor_glow':False}

def access():
    folder=os.path.join(unreal.Paths.project_saved_dir(),'Aura','LDA','District','UrithiruEnclosure2x');pts=json.load(open(os.path.join(folder,'cave_tunnel_route.json')))
    stone=A.Geo();bronze=A.Geo();cyan=A.Geo();count=0
    for i in range(0,len(pts)-1,9):
        p=pts[i];q=pts[min(i+1,len(pts)-1)];dx=q[0]-p[0];dy=q[1]-p[1];yaw=math.degrees(math.atan2(dy,dx));ux=-math.sin(math.radians(yaw));uy=math.cos(math.radians(yaw))
        for sign in [-1,1]:
            stone.bevel((p[0]+ux*sign*1070,p[1]+uy*sign*1070,-12200),(170,140,2900),26,yaw)
            bronze.bevel((p[0]+ux*sign*1005,p[1]+uy*sign*1005,-12100),(100,20,2500),4,yaw)
        stone.bevel((p[0],p[1],-10690),(190,2240,120),22,yaw);count+=1
    descent=json.load(open(os.path.join(folder,'cave_descent_route.json')))
    for i in range(0,len(descent)-1,5):
        p=descent[i];q=descent[min(i+5,len(descent)-1)];dx=q[0]-p[0];dy=q[1]-p[1];yaw=math.degrees(math.atan2(dy,dx));length=math.hypot(dx,dy)
        cyan.bevel(((p[0]+q[0])/2,(p[1]+q[1])/2,(p[2]+q[2])/2+3),(length+5,12,3),.5,yaw)
    stone.actor('ArtCavernAccess_VaultRib_01','CavernAccess','stone');bronze.actor('ArtCavernAccess_Trim_01','CavernAccess','bronze');cyan.actor('ArtCavernAccess_RouteInlay_01','CavernAccess','cyan')
    return {'vault_frames':count,'descent_accents':144,'minimum_decorated_tunnel_width_cm':1980}
