"""Cathedral causeway modules, centimetres, base/edge pivots. No Unreal side effects."""
import math
class Shape:
    def __init__(self): self.v=[]; self.t=[]; self.m=[]
    def poly(self, pts, material=0):
        i=len(self.v); self.v.extend(pts)
        for j in range(1,len(pts)-1): self.t.append((i,i+j,i+j+1)); self.m.append(material)
    def box(self,x0,x1,y0,y1,z0,z1,m=0):
        v=[(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
        for ids in [(3,2,1,0),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)]: self.poly([v[i] for i in ids],m)
    def prism(self,p,y0,y1,m=0,axis='XZ'):
        if axis=='YZ': y0,y1=y1,y0
        def q(a,y): return (a[0],y,a[1]) if axis=='XZ' else (y,a[0],a[1])
        # p is counterclockwise in its two-dimensional plane.
        self.poly([q(a,y0) for a in p],m); self.poly([q(a,y1) for a in reversed(p)],m)
        for a,b in zip(p,p[1:]+p[:1]): self.poly([q(a,y0),q(a,y1),q(b,y1),q(b,y0)],m)
    def merge(self,other,dx=0,dy=0,dz=0,mirror=False):
        off=len(self.v); self.v += [(x+dx,(-y if mirror else y)+dy,z+dz) for x,y,z in other.v]
        self.t += [(a+off,c+off,b+off) if mirror else (a+off,b+off,c+off) for a,b,c in other.t]; self.m += other.m
    def lathe(self,rings,n=8,m=0):
        for (r,z),(s,h) in zip(rings,rings[1:]):
            for j in range(n):
                a=2*math.pi*j/n; b=2*math.pi*(j+1)/n
                self.poly([(r*math.cos(a),r*math.sin(a),z),(r*math.cos(b),r*math.sin(b),z),(s*math.cos(b),s*math.sin(b),h),(s*math.cos(a),s*math.sin(a),h)],m)
        r,z=rings[0]; self.poly([(r*math.cos(2*math.pi*j/n),r*math.sin(2*math.pi*j/n),z) for j in reversed(range(n))],m)
        r,z=rings[-1]
        if r>0: self.poly([(r*math.cos(2*math.pi*j/n),r*math.sin(2*math.pi*j/n),z) for j in range(n)],m)
def ogive(half,spring,rise,n=16):
    # Two intersecting circular arcs: vertical tangents at springs, sharp apex.
    out=[]
    for j in range(2*n+1):
        x=-half+2*half*j/(2*n)
        t=half-abs(x)
        out.append((x,spring+rise*math.sqrt(max(0,1-(1-t/(2*half))**2))/math.sqrt(.75)))
    return out
def ribbon(s,inner,outer,y0,y1,m=1,axis='XZ'):
    for j in range(len(inner)-1): s.prism([inner[j],inner[j+1],outer[j+1],outer[j]],y0,y1,m,axis)
def deck(width=520,length=400,paving=True):
    s=Shape(); s.box(0,length,-width/2,width/2,-64,-.5)
    if paving:
        for ix in range(8):
            for iy in range(int(width/50)):
                ya=-width/2+iy*width/int(width/50); yb=-width/2+(iy+1)*width/int(width/50)
                s.box(ix*50+.25,min(length,(ix+1)*50)-.25,ya+.25,yb-.25,-.6,0,(ix+iy)%3==0)
        # Continuous flush borders at both endpoints; no raised threshold.
        s.box(0,.25,-width/2,width/2,-.6,0); s.box(length-.25,length,-width/2,width/2,-.6,0)
    else: s.box(0,length,-width/2,width/2,-.6,0)
    for y in (-width/2,width/2-10): s.box(0,length,y,y+10,-48,-30,1)
    return s
def parapet(north=False,proxy=False):
    s=Shape()
    if proxy: s.box(0,400,-10,50,0,126)
    else:
        s.box(0,400,-10,50,0,18,1); s.box(0,400,-3,43,18,108)
        s.box(0,400,-10,50,108,118,1); s.box(0,400,-7,47,118,126,1)
        for x in (0,97,197,297,394): s.box(x,min(400,x+6),-8,48,18,108,1)
        for xc in (50,150,250,350):
            inn=[(x+xc,z) for x,z in ogive(29,57,38,8)]
            out=[(x+xc,z+5) for x,z in ogive(35,57,38,8)]
            for a,b in [(-7,-3),(43,47)]:
                ribbon(s,inn,out,a,b)
                s.box(xc-35,xc-29,a,b,26,59,1); s.box(xc+29,xc+35,a,b,26,59,1)
                s.box(xc-35,xc+35,a,b,24,29,1)
    if north:
        q=Shape(); q.merge(s,mirror=True); return q
    return s
def pinnacle():
    s=Shape(); s.lathe([(24,0),(24,12),(18,22),(18,90),(24,99),(24,108),(13,118),(12,178),(20,188),(20,196),(10,210),(.3,330)],8,1)
    for yaw in range(0,360,90):
        x=18*math.cos(math.radians(yaw)); y=18*math.sin(math.radians(yaw))
        p=Shape(); p.lathe([(5,0),(5,56),(.2,102)],4,1); s.merge(p,x,y,108)
    s.lathe([(2,320),(.1,341)],6,2)
    return s
def support():
    s=Shape(); inner=[(x+200,z) for x,z in ogive(170,-630,480,18)]
    # Solid spandrel above the opening, plus proud extrados ribs on both faces.
    for a,b in zip(inner,inner[1:]): s.prism([a,b,(b[0],-64),(a[0],-64)],-20,20)
    outer=[(x,z+32) for x,z in inner]
    for y0,y1 in [(-28,-20),(20,28)]: ribbon(s,inner,outer,y0,y1)
    s.box(0,30,-28,28,-700,-64); s.box(370,400,-28,28,-700,-64)
    s.box(184,216,-31,31,-167,-70,1)
    for x in (0,370): s.box(x,x+30,-32,32,-700,-680,1)
    return s
def vault():
    s=Shape(); inn=ogive(195,-470,375,16); out=[(y,z+22) for y,z in inn]
    for x in (28,350): ribbon(s,inn,out,x,x+22,1,'YZ')
    for side in (-1,1):
        for k in range(20):
            x0=k*20; x1=(k+1)*20; y0=side*(190-380*k/20); y1=side*(190-380*(k+1)/20)
            z0=-90-340*abs(2*k/20-1)**1.5; z1=-90-340*abs(2*(k+1)/20-1)**1.5
            s.prism([(x0,z0),(x1,z1),(x1,z1+16),(x0,z0+16)],min(y0,y1)-9,max(y0,y1)+9,1)
    return s
def portal():
    s=Shape()
    for side in (-1,1):
        a,b=sorted((side*200,side*260)); s.box(-40,40,a,b,0,400)
        a,b=sorted((side*260,side*308)); s.box(-55,55,a,b,0,555)
        a,b=sorted((side*260,side*318)); s.box(-64,64,a,b,0,26,1); s.box(-60,60,a,b,360,385,1)
        p=pinnacle(); s.merge(p,0,side*284,555)
    inn=ogive(200,400,346.41016,22); out=[(y*1.22,400+(z-400)*1.22) for y,z in inn]
    ribbon(s,inn,out,-40,40,0,'YZ')
    mid=[(y*1.05,400+(z-400)*1.05) for y,z in inn]
    for x0,x1 in [(-48,-40),(40,48)]: ribbon(s,inn,mid,x0,x1,1,'YZ')
    outer=[(y*1.29,400+(z-400)*1.29) for y,z in inn]
    ribbon(s,out,outer,-47,-39,1,'YZ'); ribbon(s,out,outer,39,47,1,'YZ')
    return s
def passing_guard():
    s=Shape()
    for a,b,c,d in [(-30,0,200,630),(400,430,200,630),(0,400,600,630)]:
        s.box(a,b,c,d,0,108); s.box(a,b,c,d,108,126,1)
    return s
def modules():
    side=Shape(); side.merge(deck(400),dy=400)
    return {'Deck400':deck(),'Dock400':deck(600),'ParapetNorth400':parapet(True),'ParapetSouth400':parapet(),'Pinnacle':pinnacle(),'ArchSupport400':support(),'RibVault400':vault(),'TowerPortal':portal(),'PassingDeck400':side,'PassingGuard400':passing_guard(),'COL_Deck400':deck(520,paving=False),'COL_ParapetNorth400':parapet(True,True),'COL_ParapetSouth400':parapet(False,True)}
