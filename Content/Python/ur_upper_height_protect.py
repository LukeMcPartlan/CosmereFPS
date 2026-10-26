import unreal, os
import ur_upper_height_state as S
import ur_current_repair_inspect as I
def landscape():
    tex={};props=[]
    for t in unreal.ObjectIterator(unreal.Texture2D):
        if ':PersistentLevel.Landscape' in t.get_path_name():tex[t.get_path_name()]=t.blueprint_get_texture_source_id_string()
    for a in I.E.get_all_level_actors():
        if not isinstance(a,unreal.LandscapeProxy):continue
        p={'actor':I.snapshot(a),'settings':{}}
        for k in ['landscape_material','landscape_hole_material','static_lighting_lod','lod_distribution_setting','lod0_distribution_setting','component_size_quads','subsection_size_quads','num_subsections']:
            try:p['settings'][k]=I.val(a.get_editor_property(k))
            except Exception:pass
        cs=a.get_components_by_class(unreal.LandscapeComponent)
        p['component_count']=len(cs)
        p['component_poses_hash']=I.digest([(c.get_path_name(),c.get_relative_transform().export_text()) for c in cs])
        props.append(p)
    assert len(tex)>=578,'Landscape texture inventory incomplete'
    return {'landscapes':props,'texture_sources':tex}
def finish_checkpoint():
    assert not os.path.exists(os.path.join(S.F,'progress.json'))
    S.save('landscape.json',landscape());S.save('frozen.json',S.frozen())
    S.save('dirty_before.json',{'maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]})
    S.save('progress.json',{'done':[],'assets':{},'fixes':1})
    return {'actors':len(S.load('before.json')),'scope':len(S.load('scope.json')),'static_assets':len(S.load('asset_fingerprints.json')),'landscape_textures':len(S.load('landscape.json')['texture_sources']),'checkpoint':S.F,'scene_mutations':0,'routine_fixes_used':1}
