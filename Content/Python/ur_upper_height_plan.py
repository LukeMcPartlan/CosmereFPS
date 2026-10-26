import unreal, math, collections, copy
import ur_upper_height_state as S
import ur_current_repair_inspect as I
import ur_current_repair_mapping as M
def parameters(i):
    return (160000,160000,.5) if i==9 else (180000,170000,9840/19840)
def elevation(i,z):
    old,new,f=parameters(i);return new+(z-old)*f
def prepare():
    B=S.load('before.json');G=S.unpack('static_geometry.zlib');scope=S.load('scope.json');out={};summary=collections.Counter()
    for p,b in B.items():
        if b['label'].startswith('UR_Lift_09_10_') and b['label'].endswith('UpperLanding') and p not in scope:scope.append(p)
    S.save('scope.json',scope)
    for path in scope:
        b=B[path];n=b['label'];cat=b['folder'].split('/')[-1];p={k:b[k][:] for k in ['loc','rot','scale']};v=None;src=None;kind='rigid'
        if b['class']=='BP_UrithiruLift_C':
            props=b['properties'];land=B[props['UpperLanding'] if props['bStartAtUpper'] else props['LowerLanding']]
            p['loc']=land['loc'][:];p['loc'][2]-=props['PivotToTopOffset']
            if props['bStartAtUpper']:p['loc'][2]-=10000
            kind='lift_placement'
        elif b['class']=='DynamicMeshActor':
            i=9 if n.startswith('E09') else 10;oldbase=159920 if i==9 else 179920;newbase=159920 if i==9 else 169920
            assert max(abs(x) for x in b['rot'])<1e-7
            p['loc'][2]=newbase+.5*(b['loc'][2]-oldbase);p['scale'][2]*=.5;kind='shell_transform'
        else:
            i=9 if (n.startswith('E09') or 'zone:E09' in b['tags']) else 10
            old,new,f=parameters(i);delta=new-old
            if b['class'] in ['BP_UrithiruTierAnchor_C','TargetPoint','PlayerStart']:
                p['loc'][2]+=delta
            else:
                assert b['class']=='StaticMeshActor',(n,b['class']);src=b['components'][0]['mesh'];g=G[src]
                if cat=='InteriorSpirals' and '_Ramp_' in n:
                    assert abs(b['rot'][0])+abs(b['rot'][2])<1e-7
                    p['loc'][2]=elevation(i,b['loc'][2]);tops={}
                    for q in g['v']:
                        key=(round(q[0],4),round(q[1],4));tops[key]=max(tops.get(key,-1e30),q[2])
                    v=[[q[0],q[1],tops[(round(q[0],4),round(q[1],4))]*f+q[2]-tops[(round(q[0],4),round(q[1],4))]] for q in g['v']];kind='ramp_surface_keep_thickness'
                elif cat=='InteriorSpirals' and '_Rail_' in n:
                    ends=M.rail_line(b)
                    for q in ends:q[2]=elevation(i,q[2]-110)+110
                    p=M.line_pose(b,*ends);kind='rail_reslope_keep_section'
                elif cat in ['Balconies'] or (cat=='InteriorSpirals' and '_Floor_' in n) or (cat=='Routes' and any(t=='blk:S%02d'%i for t in b['tags'])):
                    shift=-5000 if i==9 else -20000;p['loc'][2]+=shift;kind='gallery_keep_dimensions'
                elif cat in ['Floors','Routes','Boundaries','Cover','LiftBays','Spawns']:
                    p['loc'][2]+=delta
                else:raise ValueError(('Unclassified',n,cat))
        assert all(math.isfinite(x) for k in ['loc','rot','scale'] for x in p[k])
        out[path]={'path':path,'label':n,'pose':p,'source_mesh':src,'vertices':v,'kind':kind}
        summary[kind]+=1
    S.save('proposal.json',out);S.save('proposal_summary.json',dict(summary))
    return {'scope':len(scope),'roles':dict(summary),'ramp_variants':len(set(I.digest([r['source_mesh'],r['vertices']]) for r in out.values() if r['vertices'])),'new_floor_interfaces_cm':[160000,170000],'galleries_cm':[165000,179840]}
