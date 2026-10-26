import unreal, math, os, json
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ROOT = 'UR/Art'
ASSETS = '/Game/Maps/Urithiru/Art/Meshes/'
MATS = {k:'/Game/Generated_Materials/'+v for k,v in {'stone':'MI_UR_FinalStructuralStone','rock':'MI_UR_FinalMountainRock','plaza':'MI_UR_FinalPlazaPaving','interior':'MI_UR_FinalInteriorPaving','bronze':'MI_UR_FinalBronzeTrim','doors':'MI_UR_FinalEntranceDoors','scree':'MI_UR_FinalGroundScree','crystal':'MI_UR_FinalCyanCrystal','cyan':'MI_UR_FinalCyanRouteInlay','violet':'MI_UR_FinalVioletRouteInlay'}.items()}
REPORT = os.path.join(unreal.Paths.project_saved_dir(),'.Aura','reports','UrithiruArt')
def world_check():
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if w.get_path_name().split('.')[0]!='/Game/Maps/Lvl_Urithiru':raise RuntimeError('Wrong world')
def native_material(a,role):
    result=json.loads(unreal.AuraMaterialCommands.apply_material_to_actor(a.get_actor_label(),MATS[role],0))
    c=a.get_component_by_class(unreal.MeshComponent);actual=c.get_material(0)
    if not result.get('success') or not actual or actual.get_path_name().split('.')[0]!=MATS[role]:raise RuntimeError('Native material assignment failed: '+a.get_actor_label())
def root(zone):
    label='Art'+zone+'_Anchor_01';a=next((a for a in EAS.get_all_level_actors() if a.get_actor_label()==label),None)
    if a:return a
    a=EAS.spawn_actor_from_class(unreal.TargetPoint,unreal.Vector());a.modify();a.set_actor_label(label);a.set_folder_path(ROOT+'/'+zone)
    parent=next((p for p in EAS.get_all_level_actors() if p.get_actor_label()=='ArtUrithiru_Anchor_01'),None)
    if parent and parent!=a:a.attach_to_actor(parent,'',unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,False)
    a.set_editor_property('tags',['UR_FinalArt','UR_ArtRoot']);return a
def finish(a,label,zone,role,category='Detail'):
    a.modify();a.set_actor_label(label);a.set_folder_path(ROOT+'/'+zone+'/'+category);a.set_editor_property('tags',['UR_FinalArt','role:'+role,'hand_editable'])
    a.attach_to_actor(root(zone),'',unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,False)
    c=a.get_component_by_class(unreal.MeshComponent);c.modify();c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('generate_overlap_events',False)
    native_material(a,role);return a
def polar(deg,r,z,cx=0,cy=0):
    a=math.radians(deg);return (cx+r*math.cos(a),cy+r*math.sin(a),z)
class Geo:
    def __init__(self):self.v=[];self.t=[]
    def add(self,v,t):
        n=len(self.v);self.v.extend(v);self.t.extend((a+n,b+n,c+n) for a,b,c in t)
    def quad(self,a,b,c,d):self.t.extend([(a,b,c),(a,c,d)])
    def bevel(self,center,size,b=10,yaw=0):
        x,y,z=center;sx,sy,sz=size;b=min(b,min(size)*.24);rings=[]
        for dz,inset in [(-sz/2,b),(-sz/2+b,0),(sz/2-b,0),(sz/2,b)]:
            hx,hy=sx/2-inset,sy/2-inset;d=max(1,b*.65)
            ring=[(-hx+d,-hy),(hx-d,-hy),(hx,-hy+d),(hx,hy-d),(hx-d,hy),(-hx+d,hy),(-hx,hy-d),(-hx,-hy+d)]
            co,si=math.cos(math.radians(yaw)),math.sin(math.radians(yaw));rings.extend([(x+u*co-v*si,y+u*si+v*co,z+dz) for u,v in ring])
        t=[]
        for j in range(3):
            for i in range(8):
                a=j*8+i;c=j*8+(i+1)%8;t.extend([(a,c,c+8),(a,c+8,a+8)])
        for i in range(1,7):t.extend([(0,i+1,i),(24,24+i,25+i)])
        self.add(rings,t)
    def annulus(self,profile,start=0,end=360,n=144,cx=0,cy=0):
        closed=end-start>=359.99;count=n if closed else n+1;v=[];t=[];p=len(profile)
        for i in range(count):v.extend([polar(start+(end-start)*i/n,r,z,cx,cy) for r,z in profile])
        for j in range(n):
            k=j*p;l=((j+1)%count)*p
            for i in range(p):
                ip=(i+1)%p;t.extend([(k+i,l+i,l+ip),(k+i,l+ip,k+ip)])
        if not closed:
            for i in range(1,p-1):t.extend([(0,i+1,i),((count-1)*p,(count-1)*p+i,(count-1)*p+i+1)])
        self.add(v,t)
    def mesh(self):
        d=unreal.DynamicMesh();d.modify()
        ret=unreal.GeometryScript_MeshEdits.add_vertices_to_mesh(d,unreal.GeometryScript_List.convert_array_to_vector_list([unreal.Vector(*p) for p in self.v]))
        idx=next(x for x in ret if isinstance(x,unreal.GeometryScriptIndexList));ids=unreal.GeometryScript_List.convert_index_list_to_array(idx)
        unreal.GeometryScript_MeshEdits.add_triangles_to_mesh(d,unreal.GeometryScript_List.convert_array_to_triangle_list([unreal.IntVector(ids[a],ids[c],ids[b]) for a,b,c in self.t]))
        so=unreal.GeometryScriptSplitNormalsOptions();so.set_editor_property('opening_angle_deg',48);unreal.GeometryScript_Normals.compute_split_normals(d,so,unreal.GeometryScriptCalculateNormalsOptions());return d
    def actor(self,label,zone,role):
        world_check()
        if any(a.get_actor_label()==label for a in EAS.get_all_level_actors()):return None
        a=EAS.spawn_actor_from_class(unreal.DynamicMeshActor,unreal.Vector());c=a.dynamic_mesh_component;c.modify();c.set_dynamic_mesh(self.mesh());c.notify_mesh_modified()
        return finish(a,label,zone,role)
    def asset(self,name):
        path=ASSETS+name
        if unreal.EditorAssetLibrary.does_asset_exist(path):return unreal.load_asset(path)
        opts=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opts.set_editor_property('enable_collision',False);opts.set_editor_property('enable_nanite',False)
        mesh,outcome=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(self.mesh(),path,opts)
        if not mesh:raise RuntimeError('Mesh bake failed '+path)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh);return mesh
def instance(mesh,label,zone,role,location=(0,0,0),rotation=(0,0,0),scale=(1,1,1)):
    if any(a.get_actor_label()==label for a in EAS.get_all_level_actors()):return None
    a=EAS.spawn_actor_from_object(mesh,unreal.Vector(*location),unreal.Rotator(pitch=rotation[0],yaw=rotation[1],roll=rotation[2]));a.modify();a.set_actor_scale3d(unreal.Vector(*scale));return finish(a,label,zone,role)
def save_note(name,data):
    os.makedirs(REPORT,exist_ok=True)
    with open(os.path.join(REPORT,name+'.json'),'w') as f:json.dump(data,f,indent=2)
