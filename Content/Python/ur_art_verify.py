import unreal,os,json,math,collections
import ur_artkit as A
from ur_art_architecture import HEIGHTS,RADII
W=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
Q=unreal.TraceTypeQuery.ECC_VISIBILITY
DBG=unreal.DrawDebugTrace.NONE

def line(p,q,ignore):return unreal.SystemLibrary.line_trace_single(W,unreal.Vector(*p),unreal.Vector(*q),Q,False,ignore,DBG)
def capsule(p,q,ignore):return unreal.SystemLibrary.capsule_trace_single(W,unreal.Vector(*p),unreal.Vector(*q),42,96,Q,False,ignore,DBG)
def route(name,pts,ignore):
    failures=[];prev=None
    for i,p in enumerate(pts):
        hit=line((p[0],p[1],p[2]+180),(p[0],p[1],p[2]-300),ignore)
        if not hit:failures.append([i,'no_support']);prev=None;continue
        h=hit.to_tuple();z=h[5].z
        if abs(z-p[2])>180 or h[7].z<.7:failures.append([i,'support_grade_or_height']);prev=None;continue
        center=(p[0],p[1],z+104)
        if capsule(center,(center[0],center[1],center[2]+.1),ignore):failures.append([i,'capsule_contact'])
        if prev and capsule(prev,center,ignore):failures.append([i,'sweep_contact'])
        prev=center
    return {'name':name,'samples':len(pts),'failures':failures}

def reachable(pts):
    for a,b in zip(pts,pts[1:]):
        steps=max(1,int(math.dist(a,b)/25000)+1);prev=a
        for i in range(1,steps+1):
            p=[a[k]+(b[k]-a[k])*i/steps for k in range(3)]
            path=unreal.NavigationSystemV1.find_path_to_location_synchronously(W,unreal.Vector(*prev),unreal.Vector(*p))
            if path is None or not path.is_valid() or path.is_partial():return {'ok':False,'failed_leg':[prev,p]}
            prev=p
    return {'ok':True}

def routes():
    actors=A.EAS.get_all_level_actors();ignore=[a for a in actors if isinstance(a,unreal.PlayerStart) or a.get_class().get_name()=='BP_UrithiruLift_C'];out=[];nav=[]
    for i,z in enumerate(HEIGHTS):
        rise=HEIGHTS[i+1]-z if i<9 else 3300;bz=z+rise/2 if i<9 else z+rise
        for offset in [0,180]:
            pts=[A.polar(offset+k,10000,z+rise*k/180) for k in range(181)];name='E%02d_Spiral%s'%(i+1,offset)
            out.append(route(name,pts,ignore));nav.append({'name':name,**reachable(pts[::5])})
        pts=[A.polar(k,8200,bz) for k in range(0,361,3)];out.append(route('E%02d_Balcony'%(i+1),pts,ignore));nav.append({'name':'E%02d_Balcony'%(i+1),**reachable(pts[::5])})
        pts=[A.polar(0,11100,z)]+[A.polar(k,10000,z+rise*k/180) for k in range(0,(90 if i<9 else 180)+1,5)]+[A.polar(90 if i<9 else 180,8200,bz)]
        nav.append({'name':'E%02d_DeckToBalcony'%(i+1),**reachable(pts)})
        for off in [0,180]:
            out.append(route('E%02d_Boarding%s'%(i+1,off),[A.polar(off,r,z) for r in range(11100,9999,-100)],ignore))
            angle=off+90 if i<9 else off+180
            out.append(route('E%02d_BalconyAccess%s'%(i+1,off),[A.polar(angle,r,bz) for r in range(10000,8199,-100)],ignore))
    folder=os.path.join(unreal.Paths.project_saved_dir(),'Aura','LDA','District','UrithiruEnclosure2x')
    descent=json.load(open(os.path.join(folder,'cave_descent_route.json')));tunnel=json.load(open(os.path.join(folder,'cave_tunnel_route.json')))
    for name,pts in [('CavernDescent',descent),('CavernTunnel',tunnel)]:out.append(route(name,pts,ignore))
    entrance=[(44000,0,-80),(34400,0,0),(32200,0,0),(31800,-2850,0),(28500,-2850,0),(28200,0,0),(11000,0,0)]
    nav.append({'name':'PlazaToBase',**reachable(entrance)})
    nav.append({'name':'PlazaToCavern',**reachable([(52500,17000,-80)]+descent[::10]+[descent[-1]]+tunnel[::5]+[tunnel[-1]])})
    for a in actors:
        if isinstance(a,unreal.PlayerStart):
            p=a.get_actor_location();r=math.hypot(p.x,p.y);angle=math.degrees(math.atan2(p.y,p.x));z=p.z-106
            pts=[(p.x,p.y,z),A.polar(angle,14000,z),A.polar(angle,11500,z)];nav.append({'name':a.get_actor_label()+'_ToInterior',**reachable(pts)})
    result={'routes':out,'navigation':nav,'route_samples':sum(r['samples'] for r in out),'contact_failures':sum(len(r['failures']) for r in out),'nav_passes':sum(r['ok'] for r in nav),'nav_failures':[r for r in nav if not r['ok']]}
    A.save_note('route_validation',result);return {k:v for k,v in result.items() if k!='routes' and k!='navigation'}

def preserve():
    actors=A.EAS.get_all_level_actors();by={a.get_actor_label():a for a in actors};before=json.load(open(os.path.join(A.REPORT,'before_scene.json')));game=json.load(open(os.path.join(A.REPORT,'gameplay_before.json')));diff=[];missing=[];lifts=[];starts=[]
    for old in before:
        a=by.get(old['label'])
        if a is None:missing.append(old['label']);continue
        p=a.get_actor_location();s=a.get_actor_scale3d()
        if max(abs(x-y) for x,y in zip([p.x,p.y,p.z]+[s.x,s.y,s.z],old['location']+old['scale']))>1e-5:diff.append(old['label'])
        for cm in old['meshes']:
            comp=next((c for c in a.get_components_by_class(unreal.MeshComponent) if c.get_name()==cm['component']),None)
            if not comp or str(comp.get_collision_enabled())!=cm['collision']:diff.append(old['label']+':collision')
            if isinstance(comp,unreal.StaticMeshComponent) and comp.get_editor_property('static_mesh').get_path_name()!=cm['mesh']:diff.append(old['label']+':mesh')
    for old in game['lifts']:
        a=by[old['label']];a.modify();a.call_method('UserConstructionScript');b,e=a.get_actor_bounds(True);props=old['properties'];unchanged=True
        for prop,val in props.items():
            if prop=='PlatformMaterial':continue
            v=a.get_editor_property(prop);actual=[v.x,v.y,v.z] if isinstance(v,unreal.Vector) else v.get_path_name() if isinstance(v,unreal.Object) else v
            unchanged=unchanged and actual==val
        comp=a.get_component_by_class(unreal.StaticMeshComponent);p=a.get_actor_location();material=a.get_editor_property('PlatformMaterial').get_path_name();applied=comp.get_material(0).get_path_name()
        lifts.append({'label':a.get_actor_label(),'size_cm':[e.x*2,e.y*2,e.z*2],'top_offset_cm':b.z+e.z-p.z,'timings_refs_replication_unchanged':unchanged,'instance_material':material,'constructed_material':applied,'material_persists':material==applied,'bays_clear':not capsule((p.x,p.y,p.z+144),(p.x,p.y,p.z+144.1),[a])})
    ignore=[a for a in actors if isinstance(a,unreal.PlayerStart) or a.get_class().get_name()=='BP_UrithiruLift_C']
    for a in actors:
        if isinstance(a,unreal.PlayerStart):
            p=a.get_actor_location();hit=line((p.x,p.y,p.z+50),(p.x,p.y,p.z-200),ignore)
            starts.append({'label':a.get_actor_label(),'clear':not capsule((p.x,p.y,p.z),(p.x,p.y,p.z+.1),ignore),'supported':bool(hit)})
    art=[a for a in actors if 'UR_FinalArt' in [str(t) for t in a.tags]];solids=[c for a in art for c in a.get_components_by_class(unreal.MeshComponent)];hazards=[a for a in actors if a.get_class().get_name()=='BP_UrithiruSafetyVolume_C'];geom=[a for a in art if a.get_components_by_class(unreal.MeshComponent)];safety_overlaps=[]
    for a in geom:
        b,e=a.get_actor_bounds(False)
        for h in hazards:
            hb,he=h.get_actor_bounds(False)
            if abs(b.x-hb.x)<e.x+he.x and abs(b.y-hb.y)<e.y+he.y and abs(b.z-hb.z)<e.z+he.z:safety_overlaps.append([a.get_actor_label(),h.get_actor_label()])
    gm=W.get_world_settings().get_editor_property('default_game_mode').get_path_name()
    result={'original_count':len(before),'actor_count':len(actors),'missing_original_actors':missing,'original_transform_mesh_collision_differences':diff,'game_mode_unchanged':gm==game['game_mode'],'lifts':lifts,'starts':starts,'safety_count':len(hazards),'navbounds_count':sum(isinstance(a,unreal.NavMeshBoundsVolume) for a in actors),'art_count':len(art),'art_mesh_count':len(solids),'art_classes':dict(collections.Counter(a.get_class().get_name() for a in art)),'new_collision_enabled':sum(c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION for c in solids),'art_safety_aabb_overlaps':safety_overlaps,'art_actor_labels':[a.get_actor_label() for a in art]}
    A.save_note('preservation_validation',result);return {k:v for k,v in result.items() if k not in ['lifts','starts','art_actor_labels']}
