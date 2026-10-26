"""Scoped asset baking for cathedral_shapes. No graph edits or broad saves."""
import os, json, math, unreal
from kredik_shaw import cathedral_shapes as S
ROOT='/Game/Maps/KendrikShaw/Generated'
MODELS=ROOT+'/Models/Modular/Cathedral'
MATERIALS=ROOT+'/Materials/Cathedral'
REPORT=os.path.join(unreal.Paths.project_saved_dir(),'.Aura/reports/KredikShaw')
NAMES=['Stone','Trim','Warm']
def materials():
    out=[]
    for name,rgb,rough,metal in [('Stone',(.115,.125,.14),.91,0),('Trim',(.235,.225,.205),.84,0),('Warm',(.32,.19,.07),.62,.35)]:
        path=MATERIALS+'/MI_KS_Cathedral'+name
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            m=unreal.EditorAssetLibrary.duplicate_asset(ROOT+'/Materials/Kit/MI_LDA_Wall',path)
            m.modify()
            lib=unreal.MaterialEditingLibrary
            lib.set_material_instance_vector_parameter_value(m,'Color',unreal.LinearColor(*rgb,1))
            for k,v in [('Grid',0),('Roughness',rough),('Metallic',metal),('Specular',.3)]: lib.set_material_instance_scalar_parameter_value(m,k,v)
            lib.update_material_instance(m)
            assert unreal.EditorAssetLibrary.save_loaded_asset(m)
        out.append(unreal.load_asset(path))
    return out
def pick(ret,typ): return next(x for x in (ret if isinstance(ret,tuple) else (ret,)) if isinstance(x,typ))
def dynamic(shape):
    mesh=unreal.DynamicMesh(); mesh.modify()
    lib=unreal.GeometryScript_List; edit=unreal.GeometryScript_MeshEdits
    positions=lib.convert_array_to_vector_list([unreal.Vector(*p) for p in shape.v])
    ids=lib.convert_index_list_to_array(pick(edit.add_vertices_to_mesh(mesh,positions),unreal.GeometryScriptIndexList))
    for mat in sorted(set(shape.m)):
        ts=[unreal.IntVector(ids[a],ids[c],ids[b]) for (a,b,c),m in zip(shape.t,shape.m) if m==mat]
        ix=pick(edit.add_triangles_to_mesh(mesh,lib.convert_array_to_triangle_list(ts)),unreal.GeometryScriptIndexList)
        unreal.GeometryScript_Materials.set_material_id_on_triangles(mesh,ix,int(mat))
    opt=unreal.GeometryScriptSplitNormalsOptions(); opt.opening_angle_deg=40
    unreal.GeometryScript_Normals.compute_split_normals(mesh,opt,unreal.GeometryScriptCalculateNormalsOptions())
    return mesh
def bake_assets():
    mats=materials(); result={}
    for name,shape in S.modules().items():
        path=MODELS+'/SM_KS_Cathedral_'+name
        assert not unreal.EditorAssetLibrary.does_asset_exist(path), 'Refusing to overwrite '+path
        mesh=dynamic(shape)
        opts=unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.set_editor_property('enable_collision',True)
        opts.set_editor_property('collision_mode',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        opts.set_editor_property('enable_recompute_normals',False)
        sm=pick(unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh,path,opts),unreal.StaticMesh)
        sm.modify()
        sm.set_editor_property('static_materials',[unreal.StaticMaterial(material_interface=m,material_slot_name=NAMES[i]) for i,m in enumerate(mats)])
        unreal.EditorAssetLibrary.set_metadata_tag(sm,'KS_Owned','Step2_CathedralGeometry')
        unreal.EditorAssetLibrary.set_metadata_tag(sm,'KS_Pivot','X=0 start; Y=0 lane center or rail datum; Z=0 walking/base datum')
        unreal.EditorAssetLibrary.set_metadata_tag(sm,'KS_Profile','CathedralStone_v1')
        assert unreal.EditorAssetLibrary.save_loaded_asset(sm)
        lo=[min(p[k] for p in shape.v) for k in range(3)]; hi=[max(p[k] for p in shape.v) for k in range(3)]
        bb=sm.get_bounding_box()
        actual_lo=[bb.min.x,bb.min.y,bb.min.z]; actual_hi=[bb.max.x,bb.max.y,bb.max.z]
        assert max(abs(a-b) for a,b in zip(lo+hi,actual_lo+actual_hi))<.1, name+' bounds mismatch'
        assert sm.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
        result[name]={'path':path,'vertices':len(shape.v),'triangles':len(shape.t),'bounds_min':actual_lo,'bounds_max':actual_hi,'collision':'UseComplexAsSimple','slots':NAMES}
    unreal.AuraPythonStatics.wait_for_pending_asset_work()
    os.makedirs(REPORT,exist_ok=True)
    with open(os.path.join(REPORT,'step2_cathedral_meshes.json'),'w') as fp: json.dump(result,fp,indent=2)
    return result
