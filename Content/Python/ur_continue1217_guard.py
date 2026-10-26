import unreal, os, json, hashlib, datetime, shutil
import ur_task3_guard as G
F=os.path.join(unreal.Paths.project_saved_dir(),'Aura/LDA/District/UrithiruApprovedV1')
R=os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/UrithiruRevision')
PROPS=['LowerLanding','UpperLanding','PlatformSizeCm','PivotToTopOffset','PlatformMaterial','TravelSeconds','DwellSeconds','bStartAtUpper','bInitialized']
def write(p,v):
    with open(p,'w',encoding='utf-8') as f:json.dump(v,f,indent=2,sort_keys=True,allow_nan=False)
def read(p):
    with open(p,encoding='utf-8') as f:return json.load(f)
def digest(p):
    with open(p,'rb') as f:return hashlib.sha256(f.read()).hexdigest()
def stable(x):
    if isinstance(x,dict):return {k:stable(v) for k,v in x.items() if k!='actor_guid'}
    if isinstance(x,list):return [stable(v) for v in x]
    return x
def frozen():
    manifest=read(os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/Core/20261014T140724_567949Z/frozen_manifest.json'))
    paths=set()
    def collect(v):
        if isinstance(v,dict):
            for x in v.values():collect(x)
        elif isinstance(v,list):
            for x in v:collect(x)
        elif isinstance(v,str) and v.startswith('/Game/Blockouts/Blueprints/'):
            p=unreal.Paths.project_content_dir()+v[6:].split('.')[0]+'.uasset'
            if os.path.isfile(p):paths.add(os.path.abspath(p))
    collect(manifest)
    return {p:digest(p) for p in sorted(paths)}
def sources():
    out={}
    for base,dirs,files in os.walk(F):
        dirs[:]=[d for d in dirs if d!='__pycache__']
        for n in files:
            if n.endswith(('.py','.json','.bin')):
                p=os.path.join(base,n);out[os.path.relpath(p,F)]=digest(p)
    return out
def extra():
    out={}
    for a in G.E.get_all_level_actors():
        row={'hidden_editor':a.is_hidden_ed(),'hidden_game':a.get_editor_property('bHidden'),'bodies':{},'instances':{}}
        for c in a.get_components_by_class(unreal.PrimitiveComponent):
            row['bodies'][c.get_path_name()]=c.get_editor_property('body_instance').export_text()
            if isinstance(c,unreal.InstancedStaticMeshComponent):
                row['instances'][c.get_path_name()]=[c.get_instance_transform(i,False).export_text() for i in range(c.get_instance_count())]
        if a.get_class().get_name()=='BP_UrithiruLift_C':row['lift']={k:G.val(a.get_editor_property(k)) for k in PROPS}
        out[a.get_path_name()]=row
    return out
def begin(task,allowed):
    assert G.W.get_path_name().split(':')[0]=='/Game/Maps/Lvl_Urithiru.Lvl_Urithiru'
    cp=os.path.join(R,'task%d_retry_%s'%(task,datetime.datetime.now().strftime('%Y%m%d_%H%M%S')));os.makedirs(cp)
    write(os.path.join(cp,'allowed_fields.json'),allowed)
    write(os.path.join(cp,'scene_before.json'),stable(G.snapshot()))
    write(os.path.join(cp,'extra_before.json'),extra())
    write(os.path.join(cp,'protected_before.json'),stable(G.protected()[0]))
    write(os.path.join(cp,'frozen_before.json'),frozen())
    write(os.path.join(cp,'source_hashes_before.json'),sources())
    write(os.path.join(cp,'dirty_before.json'),G.dirty())
    p=os.path.join(unreal.Paths.project_content_dir(),'Maps/Lvl_Urithiru.umap')
    write(os.path.join(cp,'map_disk_before.json'),{'path':p,'sha256':digest(p)})
    os.makedirs(os.path.join(cp,'sources'))
    for n in sources():
        dst=os.path.join(cp,'sources',n);os.makedirs(os.path.dirname(dst),exist_ok=True);shutil.copy2(os.path.join(F,n),dst)
    write(os.path.join(R,'continue1217_current.json'),{'task':task,'checkpoint':cp})
    return cp
def finish(cp,status,details,permitted=()):
    before=read(os.path.join(cp,'scene_before.json'));after=stable(G.snapshot())
    allowed=set(permitted);removed=set(before)-set(after);added=set(after)-set(before)
    unexpected=G.diff({k:v for k,v in before.items() if k not in allowed},{k:v for k,v in after.items() if k in before and k not in allowed})
    ex=extra();oldex=read(os.path.join(cp,'extra_before.json'))
    unexpected+=G.diff({k:v for k,v in oldex.items() if k not in allowed},{k:v for k,v in ex.items() if k in oldex and k not in allowed})
    protected=G.diff(read(os.path.join(cp,'protected_before.json')),stable(G.protected()[0]))
    files=G.diff(read(os.path.join(cp,'frozen_before.json')),frozen())
    disk=read(os.path.join(cp,'map_disk_before.json'));disk_same=disk['sha256']==digest(disk['path'])
    changed={p:G.diff(before[p],after[p]) for p in set(before)&set(after) if before[p]!=after[p]}
    out={'status':status,'checkpoint':cp,'actor_count_before':len(before),'actor_count_after':len(after),'added':{p:after[p]['identity']['label'] for p in sorted(added)},'removed':{p:before[p]['identity']['label'] for p in sorted(removed)},'existing_differences':changed,'unexpected_differences':unexpected,'protected_differences':protected,'frozen_file_differences':files,'extra_differences':G.diff(oldex,ex),'source_differences':G.diff(read(os.path.join(cp,'source_hashes_before.json')),sources()),'dirty_after':G.dirty(),'map_disk_unchanged':disk_same,'map_saved':False,'PIE':False,'details':details}
    write(os.path.join(cp,'scene_after.json'),after);write(os.path.join(cp,'result.json'),out)
    assert not unexpected and not protected and not files and disk_same,'Preservation mismatch; inspect result.json'
    return {k:out[k] for k in ['status','checkpoint','actor_count_before','actor_count_after','added','removed','unexpected_differences','protected_differences','frozen_file_differences','dirty_after','map_disk_unchanged']}
