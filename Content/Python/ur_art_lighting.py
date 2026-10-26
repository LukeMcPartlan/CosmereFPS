import unreal,math,json
import ur_artkit as A
from ur_art_architecture import HEIGHTS,RADII

def attach(a,label,zone):
    a.modify();a.set_actor_label(label);a.set_folder_path(A.ROOT+'/'+zone+'/Lighting');a.set_editor_property('tags',['UR_FinalArt','hand_editable','UR_Light'])
    a.attach_to_actor(A.root(zone),'',unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,False)

def point(label,zone,p,intensity,radius,color,shadow=False):
    if any(a.get_actor_label()==label for a in A.EAS.get_all_level_actors()):return None
    a=A.EAS.spawn_actor_from_class(unreal.PointLight,unreal.Vector(*p));attach(a,label,zone)
    c=a.get_component_by_class(unreal.PointLightComponent)
    c.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE);c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);c.set_editor_property('intensity',intensity);c.set_editor_property('attenuation_radius',radius);c.set_editor_property('light_color',unreal.Color(*color));c.set_editor_property('cast_shadows',shadow);c.set_editor_property('volumetric_scattering_intensity',.12);c.set_editor_property('source_radius',100)
    return a

def sun():
    A.world_check();actors=A.EAS.get_all_level_actors()
    a=next(a for a in actors if isinstance(a,unreal.DirectionalLight));c=a.get_component_by_class(unreal.DirectionalLightComponent)
    a.modify();a.set_actor_rotation(unreal.Rotator(pitch=-38,yaw=155,roll=0),False)
    c.set_editor_property('intensity',15000);c.set_editor_property('use_temperature',True);c.set_editor_property('temperature',5100);c.set_editor_property('light_source_angle',1.5);c.set_editor_property('volumetric_scattering_intensity',.45)
    sky=next(a for a in actors if isinstance(a,unreal.SkyLight));sc=sky.get_component_by_class(unreal.SkyLightComponent)
    sc.set_editor_property('intensity',1.1);sc.set_editor_property('real_time_capture',True)
    label='ArtLighting_Exposure_01'
    pp=next((a for a in actors if a.get_actor_label()==label),None)
    if not pp:
        pp=A.EAS.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector());attach(pp,label,'Lighting')
    pp.set_editor_property('unbound',True);pp.set_editor_property('priority',5)
    s=pp.get_editor_property('settings')
    s.set_editor_property('override_auto_exposure_min_brightness',True);s.set_editor_property('override_auto_exposure_max_brightness',True);s.set_editor_property('auto_exposure_min_brightness',2);s.set_editor_property('auto_exposure_max_brightness',12)
    s.set_editor_property('override_auto_exposure_bias',True);s.set_editor_property('auto_exposure_bias',.3)
    s.set_editor_property('override_bloom_intensity',True);s.set_editor_property('bloom_intensity',.12)
    pp.set_editor_property('settings',s)
    fog=next((a for a in actors if a.get_actor_label()=='ArtLighting_Atmosphere_01'),None)
    if not fog:
        fog=A.EAS.spawn_actor_from_class(unreal.ExponentialHeightFog,unreal.Vector(0,0,-1000));attach(fog,'ArtLighting_Atmosphere_01','Lighting')
    fc=fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fc.set_editor_property('fog_density',.002);fc.set_editor_property('fog_height_falloff',.11);fc.set_editor_property('start_distance',12000);fc.set_editor_property('fog_inscattering_luminance',unreal.LinearColor(.26,.33,.4,1));fc.set_editor_property('enable_volumetric_fog',True);fc.set_editor_property('volumetric_fog_distance',100000)
    return {'sun_lux':15000,'temperature':5100,'sky':1.1,'map_exposure_bounds':[2,12]}

def tier(i):
    zone='E%02d'%(i+1);z=HEIGHTS[i];r=RADII[i+1]-3400 if i<9 else 12000
    fixture=A.Geo();out=[]
    for j in range(8):
        ang=22.5+j*45;p=A.polar(ang,r-800,z+1750)
        out.append(point('Art'+zone+'_Light_%02d'%(j+1),zone,p,1600000,8500,(255,205,149)))
        fixture.bevel(A.polar(ang,r-600,z+1500),(220,220,500),30,ang)
        fixture.bevel(A.polar(ang,r-600,z+1790),(350,350,110),22,ang)
    fixture.actor('Art'+zone+'_Lantern_01',zone,'bronze')
    for j in range(4):
        ang=45+j*90;out.append(point('Art'+zone+'_Light_%02d'%(j+9),zone,A.polar(ang,5600,z+1900),650000,5000,(165,199,223)))
    return {'tier':zone,'new_lights':sum(a is not None for a in out)}

def cavern():
    n=0
    for a in A.EAS.get_all_level_actors():
        if a.get_actor_label().startswith('CrystalCavernLighting'):
            c=a.get_component_by_class(unreal.PointLightComponent);c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);c.set_editor_property('intensity',4500000);c.set_editor_property('light_color',unreal.Color(94,205,235,255));c.set_editor_property('attenuation_radius',26000);c.set_editor_property('cast_shadows',True)
    for j in range(9):
        p=A.polar(j*40+15,18200,-11500)
        if point('ArtCrystalCavern_Light_%02d'%(j+1),'CrystalCavern',p,2100000,7000,(83,201,244)):n+=1
    for j,p in enumerate([(52500,21000,-11900),(57000,15000,-11900),(44000,10000,-11900),(28000,4000,-11900)],1):
        if point('ArtCavernAccess_Light_%02d'%j,'CavernAccess',p,750000,6500,(255,193,123)):n+=1
    for j in range(8):
        p=A.polar(j*90,7600,-800-j*1700,46000,21000)
        if point('ArtCavernAccess_Light_%02d'%(j+5),'CavernAccess',p,650000,5000,(147,199,214)):n+=1
    for j,p in enumerate([(43000,-3000,2200),(43000,3000,2200),(76000,-27200,2300),(76000,27200,2300)],1):
        if point('ArtFrontPlaza_Light_%02d'%j,'FrontPlaza',p,1100000,9500,(255,202,146)):n+=1
    return {'new_local_lights':n,'existing_cavern_lights_retuned':4}
