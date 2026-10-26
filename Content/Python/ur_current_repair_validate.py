import unreal, math, collections, os
import ur_current_repair_inspect as I
import ur_current_repair_mapping as M
W=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
CH=unreal.TraceTypeQuery.ECC_VISIBILITY
DBG=unreal.DrawDebugTrace.NONE

def unpack_hit(h):
    if h is None:return None
    t=h.to_tuple();return {'point':I.vec(t[5]),'normal':I.vec(t[7]),'actor':t[9].get_actor_label() if t[9] else None,'component':t[10].get_path_name() if t[10] else None}
def hit(p,q,ignore,complex=True):return unpack_hit(unreal.SystemLibrary.line_trace_single(W,unreal.Vector(*p),unreal.Vector(*q),CH,complex,ignore,DBG))
def cap(p,q,ignore):return unpack_hit(unreal.SystemLibrary.capsule_trace_single(W,unreal.Vector(*p),unreal.Vector(*q),42,96,CH,True,ignore,DBG))
def polar(a,r,z):return [r*math.cos(math.radians(a)),r*math.sin(math.radians(a)),z]
def checks(tiers):
    actors=list(I.E.get_all_level_actors());ignore=[a for a in actors if isinstance(a,(unreal.PlayerStart,unreal.LandscapeProxy)) or a.get_class().get_name()=='BP_UrithiruLift_C'];static_ignore=ignore+[a for a in actors if isinstance(a,unreal.StaticMeshActor)];rows=[]
    for i in tiers:
        c=M.context(i);d={'tier':i,'floor_z':c['lo'],'ramp_samples':[],'ramp_seams':[],'obstructions':[],'decks':[],'roof_supports':[],'gallery_bridges':[],'doors':[],'walk_landings':[],'rails':[]}
        for side in [0,180]:
            for j in range(36):
                for f in [.1,.5,.9]:
                    p=M.route(c,(j+f)/36,side);h=hit([p[0],p[1],p[2]+100],[p[0],p[1],p[2]-250],ignore);ok=bool(h and abs(h['point'][2]-p[2])<=45 and h['normal'][2]>.7)
                    d['ramp_samples'].append({'u':(j+f)/36,'side':side,'ok':ok,'hit':h,'ideal':p})
                p=M.route(c,(j+.5)/36,side);h=hit([p[0],p[1],p[2]+100],[p[0],p[1],p[2]-250],ignore)
                if h:
                    pp=[p[0],p[1],h['point'][2]+104];block=cap(pp,[pp[0],pp[1],pp[2]+.1],ignore)
                    if block and not (block['normal'][2]>.7 and block['actor'] and '_Ramp_' in block['actor']):d['obstructions'].append({'u':(j+.5)/36,'side':side,'blocker':block})
            for j in range(1,36):
                zs=[]
                for off in [-.0001,.0001]:
                    p=M.route(c,j/36+off,side);h=hit([p[0],p[1],p[2]+100],[p[0],p[1],p[2]-250],ignore);zs.append(h['point'][2] if h else None)
                d['ramp_seams'].append({'joint':j,'side':side,'ok':all(z is not None for z in zs) and abs(zs[1]-zs[0])<=45,'surface_z':zs})
            angle=side+c['sweep']*(.5 if i<10 else 1);r=c['r0']+(c['r1']-c['r0'])*(.5 if i<10 else 1)
            for rr in [r-1900,r-1200,r-900,r-600,r-300]:
                p=polar(angle,rr,M.mid(c));h=hit([p[0],p[1],p[2]+80],[p[0],p[1],p[2]-150],ignore);d['gallery_bridges'].append({'point':p,'ok':bool(h and abs(h['point'][2]-p[2])<45),'hit':h})
        for angle in range(0,360,15):
            r=M.radius_at(c['outer'],angle)-800;p=polar(angle,r,c['lo']);h=hit([p[0],p[1],p[2]+100],[p[0],p[1],p[2]-150],ignore);d['decks'].append({'angle':angle,'point':p,'ok':bool(h and abs(h['point'][2]-p[2])<10),'hit':h})
            if i>1:
                h=hit(polar(angle,r,c['lo']-79),polar(angle,r,c['lo']-300),static_ignore);d['roof_supports'].append({'angle':angle,'ok':bool(h and abs(h['point'][2]-(c['lo']-80))<.1),'hit':h})
        for angle in [0,180]:
            h0=I.load('shell_sections.json')[i-1]['samples']['1000'][str(angle)];r0=min(h0) if h0 else (7214 if i==10 else 15000)
            if not h0 and i==10:r0=7214
            p=polar(angle,r0-300,c['lo']+104);q=polar(angle,(max(h0) if h0 else 7484)+300,c['lo']+104);h=cap(p,q,ignore);d['doors'].append({'angle':angle,'ok':h is None,'hit':h,'from':p,'to':q})
        for a in actors:
            n=a.get_actor_label()
            if n.startswith('E%02d_WalkLanding_'%i):
                p=I.vec(a.get_actor_location());h=hit([p[0],p[1],p[2]+30],[p[0],p[1],p[2]-100],ignore);d['walk_landings'].append({'actor':n,'ok':bool(h and abs(h['point'][2]-(p[2]-10))<10),'hit':h})
            if n.startswith('E%02d_Rail_'%i) and str(a.get_folder_path()).endswith('/Balconies'):
                p=a.get_actor_location();s=a.get_actor_scale3d();floorz=M.mid(c);h=hit([p.x,p.y,floorz+2],[p.x,p.y,floorz-100],ignore+[a]);d['rails'].append({'actor':n,'ok':bool(h and abs(h['point'][2]-floorz)<10),'hit':h})
        rows.append(d)
    I.save('collision_checks_'+str(tiers[0])+'_'+str(tiers[-1])+'.json',rows)
    summary=[]
    for d in rows:
        s={'tier':d['tier'],'floor_z':d['floor_z'],'obstructions':len(d['obstructions'])}
        for k in ['ramp_samples','ramp_seams','decks','roof_supports','gallery_bridges','doors','walk_landings','rails']:s[k]={'pass':sum(r['ok'] for r in d[k]),'total':len(d[k])}
        s['failures']={k:[r for r in d[k] if not r['ok']][:6] for k in ['ramp_samples','ramp_seams','decks','roof_supports','gallery_bridges','doors','walk_landings','rails'] if any(not r['ok'] for r in d[k])}
        if d['obstructions']:s['blockers']=d['obstructions'][:6]
        summary.append(s)
    return summary
