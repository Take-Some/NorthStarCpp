#version 450
layout(set=0,binding=0) uniform sampler2D g_albedo;
layout(set=0,binding=1) uniform sampler2D g_normal;
layout(set=0,binding=2) uniform sampler2D g_surface;
layout(set=0,binding=3) uniform sampler2D g_depth;
struct GpuLight {
    vec4 position_range;
    vec4 color_intensity;
    vec4 direction_cone;
};
layout(set=0,binding=4,std140) uniform DynamicLighting {
    vec4 source_count;
    GpuLight sources[256];
} lighting;
layout(set=0,binding=5,std430) readonly buffer TiledLights {
    uint indices[];
} tiled;
#ifdef NS_GBUFFER_V2
layout(set=0,binding=6) uniform sampler2D g_auxiliary;
#endif
#ifdef NS_SSAO_HALF
layout(set=0,binding=7) uniform sampler2D g_half_ssao;
#endif
#ifdef NS_CSM
layout(set=0,binding=8) uniform sampler2DArray directional_shadow;
layout(set=0,binding=9,std140) uniform ShadowMatrices {
    mat4 light_vp[3];
    vec4 cascade_splits;
} shadow_data;
#endif
layout(push_constant) uniform Push {
    vec4 sky;
    vec4 mode;
    vec4 camera;     // xyz, yaw
    vec4 projection; // pitch, near clip, far clip, fov scale
    vec4 viewport;   // width, height, 1/width, 1/height
    vec4 ao; // enabled, world radius, intensity, surface bias
    vec4 film;       // grain, vignette, chromatic (reserved), saturation
    vec4 film_extra; // contrast, exposure EV, reserved, animated frame
} params;
layout(location=0) out vec4 out_color;
const float PI=3.141592653589793;
float saturate(float x){return clamp(x,0.,1.);}
vec3 reconstruct_world(float depth,vec2 pixel) {
    float near_clip=params.projection.y;
    float far_clip=params.projection.z;
    float distance=far_clip*near_clip/max(
        1e-5,far_clip-depth*(far_clip-near_clip));
    vec2 ndc=(pixel*params.viewport.zw)*2.-1.;
    float aspect=params.viewport.x/params.viewport.y;
    float horizontal=ndc.x*distance*aspect/params.projection.w;
    float vertical=-ndc.y*distance/params.projection.w;
    float pitch=params.projection.x;
    float forward=distance*cos(pitch)-vertical*sin(pitch);
    float y=vertical*cos(pitch)+distance*sin(pitch);
    float yaw=params.camera.w;
    return params.camera.xyz+vec3(
        horizontal*cos(yaw)+forward*sin(yaw),
        y,
        -horizontal*sin(yaw)+forward*cos(yaw));
}
vec3 brdf(vec3 base,vec3 normal,vec3 view,vec3 light,
          float roughness,float metallic) {
    float nol=saturate(dot(normal,light));
    if(nol<=0.)return vec3(0.);
    float nov=max(.001,saturate(dot(normal,view)));
    vec3 half_dir=normalize(light+view);
    float noh=saturate(dot(normal,half_dir));
    float voh=saturate(dot(view,half_dir));
    float a=roughness*roughness,a2=a*a;
    float denom=noh*noh*(a2-1.)+1.;
    float distribution=a2/max(PI*denom*denom,1e-5);
    float k=(roughness+1.)*(roughness+1.)*.125;
    float geometry=(nov/(nov*(1.-k)+k))*(nol/(nol*(1.-k)+k));
    vec3 f0=mix(vec3(.04),base,metallic);
    vec3 fresnel=f0+(vec3(1.)-f0)*pow(1.-voh,5.);
    vec3 spec=distribution*geometry*fresnel/max(4.*nov*nol,.001);
    vec3 diffuse=(vec3(1.)-fresnel)*(1.-metallic)*base/PI;
    return (diffuse+spec)*nol;
}
vec3 apply_filmic_look(vec3 color,vec2 uv) {
    // Scene-only post grade. HUD is drawn by an independent later pass.
    vec3 value=max(color,vec3(0))*exp2(params.film_extra.y);
    float lum=dot(value,vec3(.2126,.7152,.0722));
    value=mix(vec3(lum),value,params.film.w);
    value=(value-vec3(.5))*params.film_extra.x+vec3(.5);
    if(params.film.y>0.) {
        vec2 centered=uv-vec2(.5);
        centered.x*=params.viewport.x/max(params.viewport.y,1.);
        value*=1.-params.film.y*smoothstep(.03,.55,dot(centered,centered));
    }
    if(params.film.x>0.) {
        // Same integer film-grain hash as the D3D12 Filmic compositor.
        uvec2 pixel=uvec2(uv*params.viewport.xy);
        uint seed=(pixel.x*0x9e3779b9u)^(pixel.y*0x85ebca6bu)^
                  (uint(params.film_extra.w)*0xc2b2ae35u);
        seed^=seed>>16; seed*=0x7feb352du;
        seed^=seed>>15; seed*=0x846ca68bu; seed^=seed>>16;
        float noise=float(seed&0x00ffffffu)*(1.0/16777216.0)-.5;
        value+=noise*params.film.x;
    }
    return clamp(value,vec3(0),vec3(1));
}

 // Short, opt-in screen-space contact shadow for the directional sun.
 // Only geometry visible in g_depth can occlude; this is NOT a shadow map.
 // Projection matches reconstruct_world() exactly (Vulkan top-left UV).
 float sun_contact_visibility(vec3 world,vec3 normal) {
     const vec3 sun=normalize(vec3(.45,.82,.35));
     if(dot(normal,sun)<=.01)return 1.;
     float cy=cos(params.camera.w),sy=sin(params.camera.w);
     float cp=cos(params.projection.x),sp=sin(params.projection.x);
     float near_clip=params.projection.y,far_clip=params.projection.z;
     float inv_aspect=params.viewport.y/max(params.viewport.x,1.);
     for(int i=0;i<8;++i) {
         float distance_along_sun=.12+float(i)*.22;
         vec3 probe=world+normal*.045+sun*distance_along_sun;
         vec3 delta=probe-params.camera.xyz;
         float forward=delta.x*sy+delta.z*cy;
         float eye_depth=forward*cp+delta.y*sp;
         if(eye_depth<=near_clip)break;
         float horizontal=delta.x*cy-delta.z*sy;
         float vertical=delta.y*cp-forward*sp;
         vec2 uv=vec2(.5+.5*horizontal*params.projection.w*
                         inv_aspect/eye_depth,
                      .5-.5*vertical*params.projection.w/eye_depth);
         if(any(lessThan(uv,vec2(0.)))||
            any(greaterThanEqual(uv,vec2(1.))))break;
         ivec2 sample_pixel=ivec2(uv*params.viewport.xy);
         float sampled_depth=texelFetch(g_depth,sample_pixel,0).r;
         if(sampled_depth>=.99999)continue;
         float surface_depth=near_clip*far_clip/
             max(1e-5,far_clip-sampled_depth*(far_clip-near_clip));
         float separation=eye_depth-surface_depth;
         // Thickness check suppresses false shadows from distant surfaces;
         // normal offset and minimum gap suppress self-shadow acne.
         if(separation>.055 && separation<.30)
             return mix(.20,.60,float(i)/7.);
     }
     return 1.;
 }

#ifdef NS_CSM
float sample_csm_layer(vec3 world,vec3 N,vec3 sun,uint cascade) {
    vec4 clip=shadow_data.light_vp[cascade]*vec4(world+N*.035,1.);
    vec3 q=clip.xyz/max(1e-6,abs(clip.w));
    vec2 uv=q.xy*.5+.5;
    if(any(lessThan(uv,vec2(0.)))||any(greaterThanEqual(uv,vec2(1.)))||
       q.z<=0.||q.z>=1.)return 1.;
    ivec2 dim=textureSize(directional_shadow,0).xy;
    ivec2 pixel=clamp(ivec2(uv*vec2(dim)),ivec2(0),dim-1);
    float bias=max(.0005,.0015*(1.-max(0.,dot(N,sun))));
    float visibility=0.;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 coord=clamp(pixel+ivec2(x,y),ivec2(0),dim-1);
        float caster=texelFetch(directional_shadow,
                               ivec3(coord,int(cascade)),0).r;
        visibility+=(q.z-bias<=caster)?1.:0.;
    }
    return visibility/9.;
}
float csm_visibility(vec3 world,vec3 N,vec3 sun) {
    vec3 fwd=vec3(sin(params.camera.w)*cos(params.projection.x),
                  sin(params.projection.x),
                  cos(params.camera.w)*cos(params.projection.x));
    float distance=dot(world-params.camera.xyz,fwd);
    if(distance<0.||distance>shadow_data.cascade_splits.z)return 1.;
    uint cascade=distance<shadow_data.cascade_splits.x?0u:
                 (distance<shadow_data.cascade_splits.y?1u:2u);
    float visibility=sample_csm_layer(world,N,sun,cascade);
    if(cascade<2u) {
        float end_depth=cascade==0u?shadow_data.cascade_splits.x:
                                     shadow_data.cascade_splits.y;
        float start_depth=cascade==0u?0.:
            shadow_data.cascade_splits.x;
        float blend_width=max(.01,
            (end_depth-start_depth)*shadow_data.cascade_splits.w);
        float transition=clamp((distance-(end_depth-blend_width))/
                                  blend_width,0.,1.);
        if(transition>0.)
            visibility=mix(visibility,
                sample_csm_layer(world,N,sun,cascade+1u),transition);
    }
    return visibility;
}
#endif

void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 base=texelFetch(g_albedo,pixel,0);
    vec4 nr=texelFetch(g_normal,pixel,0);
    vec4 surface=texelFetch(g_surface,pixel,0);
    vec4 auxiliary=vec4(1.,0.,0.,0.);
#ifdef NS_GBUFFER_V2
    auxiliary=texelFetch(g_auxiliary,pixel,0);
#endif
    float depth=texelFetch(g_depth,pixel,0).r;
    int diagnostic=int(params.mode.x+.5);
    // D3D12 clips uncovered GBuffer pixels and preserves the already
    // cleared sky background for every debug view except Coverage.
    // Vulkan must not draw black over uncovered pixels in debug modes.
    if(diagnostic==8){out_color=vec4(vec3(base.a),1);return;}
    if(base.a<.5){
#ifdef NS_HDR_LINEAR
        out_color=vec4(max(params.sky.rgb,vec3(0.)),1.);
#else
        out_color=vec4(apply_filmic_look(params.sky.rgb,
                       gl_FragCoord.xy*params.viewport.zw),1);
#endif
        return;
    }
    if(diagnostic==1){out_color=vec4(base.rgb,1);return;}
    if(diagnostic==2){
        out_color=vec4(base.a>.5?normalize(nr.xyz)*.5+.5:vec3(0),1);return;
    }
    if(diagnostic==3){out_color=vec4(abs(nr.a));return;}
    if(diagnostic==4){out_color=vec4(surface.a);return;}
    if(diagnostic==5){out_color=vec4(surface.rgb,1);return;}
    if(diagnostic==6){
        float distance=params.projection.y*params.projection.z/
            max(1e-5,params.projection.z-depth*
                (params.projection.z-params.projection.y));
        out_color=vec4(vec3(pow(clamp(distance/params.projection.z,0.,1.),.25)),1);
        return;
    }
    if(diagnostic==9){out_color=vec4(vec3(auxiliary.r),1);return;}
    if(diagnostic==10){out_color=vec4(vec3(auxiliary.g),1);return;}
    if(diagnostic==11){out_color=vec4(vec3(auxiliary.b),1);return;}
    if(nr.a<0.){
#ifdef NS_HDR_LINEAR
        out_color=vec4(max(base.rgb+surface.rgb,vec3(0.)),1.);
#else
        out_color=vec4(apply_filmic_look(
            clamp(base.rgb+surface.rgb,0.,1.),
            gl_FragCoord.xy*params.viewport.zw),1.);
#endif
        return;
    }
    vec3 world=reconstruct_world(depth,gl_FragCoord.xy);
    vec3 N=normalize(nr.xyz);
    vec3 V=normalize(params.camera.xyz-world);
    float roughness=clamp(nr.a,.055,1.);
    float metallic=saturate(surface.a);
    // Bounded 8-tap world-space SSAO. Ambient-only modulation avoids
    // incorrectly darkening direct lights and emissive materials.
    float ambient_occlusion=1.0;
#ifdef NS_SSAO_HALF
    if(params.ao.x>0.5 || diagnostic==7) {
        // Five-tap depth/normal bilateral reconstruction from half-res AO.
        // Rejects contributions across disocclusions instead of blurring
        // the silhouettes of foreground geometry into the background.
        ivec2 ao_extent=textureSize(g_half_ssao,0);
        ivec2 origin=clamp(pixel/2,ivec2(0),ao_extent-1);
        const ivec2 offsets[5]=ivec2[5](
            ivec2(0,0),ivec2(-1,0),ivec2(1,0),
            ivec2(0,-1),ivec2(0,1));
        float near_clip=params.projection.y,far_clip=params.projection.z;
        float world_z=near_clip*far_clip/
            max(1e-5,far_clip-depth*(far_clip-near_clip));
        float weighted=0.,total=0.;
        for(int i=0;i<5;++i) {
            ivec2 ao_pixel=clamp(origin+offsets[i],
                                 ivec2(0),ao_extent-1);
            vec2 data=texelFetch(g_half_ssao,ao_pixel,0).rg;
            float sample_z=near_clip*far_clip/
                max(1e-5,far_clip-data.g*(far_clip-near_clip));
            ivec2 source=clamp(ao_pixel*2+ivec2(1),
                               ivec2(0),ivec2(params.viewport.xy)-1);
            vec3 source_normal=texelFetch(g_normal,source,0).xyz;
            float normal_sq=dot(source_normal,source_normal);
            float normal_cos=normal_sq>1e-6?
                max(0.,dot(N,source_normal*inversesqrt(normal_sq))):0.;
            float normal_pow2=normal_cos*normal_cos;
            float normal_pow4=normal_pow2*normal_pow2;
            float normal_weight=normal_pow4*normal_pow4;
            float depth_weight=exp2(-abs(sample_z-world_z)*4.);
            float spatial_weight=i==0?1.:.60;
            float weight=normal_weight*depth_weight*spatial_weight;
            weighted+=data.r*weight;total+=weight;
        }
        ambient_occlusion=total>1e-5?clamp(weighted/total,0.,1.):1.;
    }
#else
    if(params.ao.x>0.5 || diagnostic==7) {
        const ivec2 taps[8]=ivec2[8](
            ivec2(-3,-1),ivec2(3,1),ivec2(-1,3),ivec2(1,-3),
            ivec2(-5,-4),ivec2(5,4),ivec2(-4,5),ivec2(4,-5));
        float occluded=0.0;
        for(int i=0;i<8;++i) {
            ivec2 nearby=clamp(pixel+taps[i],ivec2(0),
                               ivec2(params.viewport.xy)-ivec2(1));
            float nearby_depth=texelFetch(g_depth,nearby,0).r;
            if(nearby_depth>=0.99999)continue;
            vec3 delta=reconstruct_world(nearby_depth,vec2(nearby)+vec2(.5))-world;
            float distance_to_sample=length(delta);
            if(distance_to_sample>1e-5 &&
               distance_to_sample<params.ao.y) {
                float angle=max(0.,dot(N,delta/distance_to_sample)-params.ao.w);
                occluded+=angle*(1.-distance_to_sample/params.ao.y);
            }
        }
        ambient_occlusion=clamp(1.-occluded*params.ao.z*.125,0.,1.);
    }
#endif
    if(diagnostic==7){out_color=vec4(vec3(ambient_occlusion),1);return;}
    // Directional sunlight remains active when local light sources exist.
    // Contact shadows only modulate direct sun, not ambient or emission.
    vec3 sun_direction=normalize(vec3(.45,.82,.35));
    float sun_visibility=1.;
#ifdef NS_CSM
    if(dot(N,sun_direction)>0.001)
        sun_visibility=csm_visibility(world,N,sun_direction);
#endif
    if(params.film_extra.z>.5 || diagnostic==12)
        sun_visibility*=sun_contact_visibility(world,N);
    if(diagnostic==13){out_color=vec4(vec3(sun_visibility),1.);return;}
    if(diagnostic==12){
        out_color=vec4(vec3(sun_visibility),1.);
        return;
    }
    vec3 lit=base.rgb*(.13*ambient_occlusion*clamp(auxiliary.r,0.,1.))+surface.rgb;
    lit+=brdf(base.rgb,N,V,sun_direction,roughness,metallic)*
         (2.1*sun_visibility);
    int count=clamp(int(lighting.source_count.x),0,256);
    if(count>0) {
        uint columns=max(1u,uint(params.mode.y+.5));
        uint rows=max(1u,uint(params.mode.w+.5));
        uint tile_size=max(1u,uint(params.mode.z+.5));
        uvec2 tile=min(uvec2(gl_FragCoord.xy)/tile_size,
                       uvec2(columns-1u,rows-1u));
        uint offset=(tile.y*columns+tile.x)*65u;
        uint stored=tiled.indices[offset];
        bool fallback=stored==0xffffffffu;
        int candidates=fallback?count:min(count,int(stored));
        for(int i=0;i<candidates;++i) {
            uint index=fallback?uint(i):tiled.indices[offset+1u+uint(i)];
            if(index>=uint(count))continue;
            GpuLight src=lighting.sources[index];
            float kind=src.direction_cone.w;
            if(kind<=-2.5) {
                lit+=base.rgb*src.color_intensity.rgb*
                    src.color_intensity.w*ambient_occlusion*clamp(auxiliary.r,0.,1.);
                continue;
            }
            vec3 L;
            float attenuation=1.;
            if(kind<=-1.5) {
                L=normalize(-src.direction_cone.xyz);
            } else {
                vec3 d=src.position_range.xyz-world;
                float distance=length(d);
                if(distance<.0001||distance>=src.position_range.w)continue;
                L=d/distance;
                float radial=saturate(1.-distance/src.position_range.w);
                attenuation=radial*radial;
            }
            if(kind>=0.) {
                float cone=dot(normalize(src.direction_cone.xyz),-L);
                attenuation*=smoothstep(src.direction_cone.w,
                    min(1.,src.direction_cone.w+.1),cone);
            }
            lit+=brdf(base.rgb,N,V,L,roughness,metallic)*
                src.color_intensity.rgb*src.color_intensity.w*attenuation;
        }
    }
    vec3 linear_color=max(lit+clamp(auxiliary.g,0.,1.)*.08*
                  pow(1.-clamp(dot(N,V),0.,1.),5.)+
                  clamp(auxiliary.b,0.,1.)*base.rgb*.06*
                  pow(1.-clamp(dot(N,V),0.,1.),5.),vec3(0.));
    // Scene-depth atmospheric perspective: same approximation, color and
    // height-falloff constants as D3D12. No extra texture samples.
    if(params.sky.w>0.) {
        float dist=length(world-params.camera.xyz);
        float h0=exp(clamp(-.025*params.camera.y,-8.,8.));
        float h1=exp(clamp(-.025*world.y,-8.,8.));
        float extinction=min(20.,params.sky.w*dist*
            clamp(.5*(h0+h1),.01,8.));
        float haze=min(.96,1.-exp(-extinction));
        linear_color=mix(linear_color,vec3(.23,.315,.395),haze);
    }
#ifdef NS_HDR_LINEAR
    out_color=vec4(linear_color,1.);
#else
    out_color=vec4(apply_filmic_look(clamp(linear_color,0.,1.),
                                   gl_FragCoord.xy*params.viewport.zw),1.);
#endif
}
