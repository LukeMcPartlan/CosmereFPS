import unreal, hashlib, struct
import ur_task3_guard as G

def fingerprint(tier):
    a=next(a for a in G.E.get_all_level_actors() if a.get_actor_label()=='E%02dSkin_Wall_01'%tier)
    c=a.dynamic_mesh_component;m=c.get_dynamic_mesh();q=unreal.GeometryScript_MeshQueries
    normal=hashlib.sha256();uv=hashlib.sha256();material=hashlib.sha256();channels=q.get_num_uv_sets(m)
    for i in range(m.get_triangle_count()):
        r=q.get_triangle_normals(m,i)
        for v in r:
            if isinstance(v,unreal.Vector):normal.update(struct.pack('<ddd',v.x,v.y,v.z))
            elif isinstance(v,bool):normal.update(bytes([v]))
        for j in range(channels):
            for v in q.get_triangle_u_vs(m,j,i):
                if isinstance(v,unreal.Vector2D):uv.update(struct.pack('<dd',v.x,v.y))
                elif isinstance(v,bool):uv.update(bytes([v]))
        mid=unreal.GeometryScript_Materials.get_triangle_material_id(m,i)
        material.update(str(mid).encode())
    return {'geometry':G.mesh_state(m),'component':G.component(c,a),'normals':normal.hexdigest(),'uv':uv.hexdigest(),'triangle_materials':material.hexdigest(),'uv_channels':channels}
