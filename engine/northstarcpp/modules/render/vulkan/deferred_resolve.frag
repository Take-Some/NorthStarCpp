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
        vec2 pixel=floor(uv*params.viewport.xy);
        float noise=fract(sin(dot(pixel,vec2(12.9898,78.233))+
                              params.film_extra.w*17.371)*43758.5453)-.5;
        value+=noise*params.film.x;
    }
    return clamp(value,vec3(0),vec3(1));
}
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
    if(diagnostic==8){out_color=vec4(vec3(base.a),1);return;}
    if(diagnostic==9){out_color=vec4(vec3(auxiliary.r),1);return;}
    if(diagnostic==10){out_color=vec4(vec3(auxiliary.g),1);return;}
    if(diagnostic==11){out_color=vec4(vec3(auxiliary.b),1);return;}
    if(base.a<.5){
        out_color=vec4(apply_filmic_look(params.sky.rgb,
                       gl_FragCoord.xy*params.viewport.zw),1);
        return;
    }
    if(nr.a<0.){
        out_color=vec4(apply_filmic_look(
            clamp(base.rgb+surface.rgb,0.,1.),
            gl_FragCoord.xy*params.viewport.zw),1.);
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
    if(diagnostic==7){out_color=vec4(vec3(ambient_occlusion),1);return;}
    vec3 lit=base.rgb*(.13*ambient_occlusion*clamp(auxiliary.r,0.,1.))+surface.rgb;
    int count=clamp(int(lighting.source_count.x),0,256);
    if(count==0) {
        lit+=brdf(base.rgb,N,V,normalize(vec3(.45,.82,.35)),
                  roughness,metallic)*vec3(2.1);
    } else {
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
            vec3 d=src.position_range.xyz-world;
            float distance=length(d);
            if(distance<.0001||distance>=src.position_range.w)continue;
            vec3 L=d/distance;
            float radial=saturate(1.-distance/src.position_range.w);
            float attenuation=radial*radial;
            if(src.direction_cone.w>=0.) {
                float cone=dot(normalize(src.direction_cone.xyz),-L);
                attenuation*=smoothstep(src.direction_cone.w,
                    min(1.,src.direction_cone.w+.1),cone);
            }
            lit+=brdf(base.rgb,N,V,L,roughness,metallic)*
                src.color_intensity.rgb*src.color_intensity.w*attenuation;
        }
    }
    out_color=vec4(apply_filmic_look(
        clamp(lit+clamp(auxiliary.g,0.,1.)*0.08*
                  pow(1.-clamp(dot(N,V),0.,1.),5.)+
                  clamp(auxiliary.b,0.,1.)*base.rgb*.06*
                  pow(1.-clamp(dot(N,V),0.,1.),5.),0.,1.),
        gl_FragCoord.xy*params.viewport.zw),1.);
}
