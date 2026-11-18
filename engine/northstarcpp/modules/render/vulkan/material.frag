#version 450
#ifndef NS_GRAPH_BASE
#define NS_GRAPH_BASE(p) vec4(1.0)
#endif
#ifndef NS_GRAPH_ROUGH
#define NS_GRAPH_ROUGH(p) vec4(factors.x)
#endif
#ifndef NS_GRAPH_METAL
#define NS_GRAPH_METAL(p) vec4(factors.y)
#endif
#ifndef NS_GRAPH_EMISSIVE
#define NS_GRAPH_EMISSIVE(p) vec4(0.0)
#endif
#ifndef NS_GRAPH_OPACITY
#define NS_GRAPH_OPACITY(p) vec4(1.0)
#endif
#ifndef NS_BLEND_MODE
#define NS_BLEND_MODE 0
#endif
#ifndef NS_ALPHA_CUTOFF
#define NS_ALPHA_CUTOFF 0.5
#endif
float saturate(float x){return clamp(x,0.0,1.0);}
vec2 saturate(vec2 x){return clamp(x,0.0,1.0);}
vec3 saturate(vec3 x){return clamp(x,0.0,1.0);}
vec4 saturate(vec4 x){return clamp(x,0.0,1.0);}
struct GraphInput {vec2 uv;};
layout(set=0,binding=0) uniform sampler2D albedo_sampler;
layout(set=0,binding=1) uniform sampler2D normal_sampler;
layout(set=0,binding=2) uniform sampler2D roughness_sampler;
layout(set=0,binding=3) uniform sampler2D metallic_sampler;
layout(set=0,binding=4) uniform sampler2D emissive_sampler;
struct GpuLight {
    vec4 position_range;
    vec4 color_intensity;
    vec4 direction_cone;
};
layout(set=0,binding=5,std140) uniform DynamicLighting {
    vec4 source_count;
    GpuLight sources[256];
} lighting;
layout(location=0) in vec2 vertex_uv;
layout(location=1) in vec3 vertex_normal;
layout(location=2) in vec4 vertex_tint;
layout(location=3) in vec4 vertex_params;
layout(location=4) in vec3 vertex_world;
layout(location=0) out vec4 output_color;
#ifdef NS_GBUFFER
layout(location=1) out vec4 output_normal_roughness;
layout(location=2) out vec4 output_emissive_metallic;
#endif
const float PI=3.14159265;
void main() {
    GraphInput p;p.uv=vertex_uv;
    vec4 base_color=vertex_tint;
    vec4 factors=vec4(vertex_params.y,vertex_params.x,vertex_params.z,0.0);
    uvec4 present=uvec4(uint(vertex_params.w+0.5),0u,0u,0u);
    vec4 base=texture(albedo_sampler,vertex_uv)*vertex_tint*NS_GRAPH_BASE(p);
    float opacity=saturate(base.a*saturate((NS_GRAPH_OPACITY(p)).x));
#if NS_BLEND_MODE == 1
    if(opacity < NS_ALPHA_CUTOFF) discard;
#endif
    if(vertex_params.z>.5) {
#ifdef NS_GBUFFER
        output_color=vec4(base.rgb,1);
        output_normal_roughness=vec4(normalize(vertex_normal),-.7);
        output_emissive_metallic=vec4(
            texture(emissive_sampler,vertex_uv).rgb+NS_GRAPH_EMISSIVE(p).rgb,0);
#else
        output_color=vec4(base.rgb+texture(emissive_sampler,vertex_uv).rgb+
                          NS_GRAPH_EMISSIVE(p).rgb,opacity);
#endif
        return;
    }
    float metallic=clamp((NS_GRAPH_METAL(p)).x*
                          texture(metallic_sampler,vertex_uv).r,0.0,1.0);
    float roughness=clamp((NS_GRAPH_ROUGH(p)).x*
                           texture(roughness_sampler,vertex_uv).r,0.055,1.0);
    vec3 N=normalize(vertex_normal);
    vec3 dx=dFdx(vertex_world),dy=dFdy(vertex_world);
    vec2 duv=dFdx(vertex_uv),dvv=dFdy(vertex_uv);
    float determinant=duv.x*dvv.y-duv.y*dvv.x;
    if(abs(determinant)>1e-6) {
        vec3 T=normalize((dx*dvv.y-dy*duv.y)/determinant);
        T=normalize(T-N*dot(T,N));
        vec3 B=normalize(cross(N,T));
        vec3 normalSample=texture(normal_sampler,vertex_uv).xyz*2.0-1.0;
        N=normalize(mat3(T,B,N)*normalSample);
    }
#ifdef NS_GBUFFER
    output_color=vec4(base.rgb,1);
    output_normal_roughness=vec4(N,roughness);
    output_emissive_metallic=vec4(
        texture(emissive_sampler,vertex_uv).rgb+NS_GRAPH_EMISSIVE(p).rgb,
        metallic);
    return;
#endif
    vec3 V=normalize(vec3(0.0,0.0,1.0));
    vec3 f0=mix(vec3(.04),base.rgb,metallic);
    float a=roughness*roughness;
    float a2=a*a;
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    vec3 rgb=base.rgb*.13;
    int count=clamp(int(lighting.source_count.x),0,256);
    for(int i=0;i<max(count,1);++i) {
        vec3 L=normalize(vec3(.45,.82,.35));
        vec3 lightColor=vec3(2.1);
        if(count>0) {
            GpuLight light=lighting.sources[i];
            float kind=light.direction_cone.w;
            if(kind<=-2.5) {
                rgb+=base.rgb*light.color_intensity.rgb*light.color_intensity.w;
                continue;
            }
            float attenuation=1.;
            if(kind<=-1.5) {
                L=normalize(-light.direction_cone.xyz);
            } else {
                vec3 towardLight=light.position_range.xyz-vertex_world;
                float distance=length(towardLight);
                if(distance>=light.position_range.w || distance<.0001)continue;
                L=towardLight/distance;
                float radial=clamp(1.0-distance/light.position_range.w,0.0,1.0);
                attenuation=radial*radial;
            }
            if(kind>=0.0) {
                vec3 direction=normalize(light.direction_cone.xyz);
                float angle=dot(direction,-L);
                attenuation*=smoothstep(light.direction_cone.w,
                                         min(1.0,light.direction_cone.w+.1),angle);
            }
            lightColor=light.color_intensity.rgb*
                (light.color_intensity.w*attenuation);
        }
        float ndl=max(dot(N,L),0.0);
        if(ndl<=.0)continue;
        vec3 H=normalize(L+V);
        float ndv=max(dot(N,V),.001);
        float ndh=max(dot(N,H),.001);
        float vdh=max(dot(V,H),0.0);
        float denom=(ndh*ndh*(a2-1.0)+1.0);
        float D=a2/(PI*denom*denom+1e-6);
        float G=(ndv/(ndv*(1.0-k)+k))*(ndl/(ndl*(1.0-k)+k));
        vec3 F=f0+(1.0-f0)*pow(1.0-vdh,5.0);
        vec3 spec=D*G*F/max(4.0*ndv*ndl,.001);
        vec3 diffuse=(vec3(1.0)-F)*(1.0-metallic)*base.rgb/PI;
        rgb+=(diffuse+spec)*ndl*lightColor;
    }
    rgb+=texture(emissive_sampler,vertex_uv).rgb+NS_GRAPH_EMISSIVE(p).rgb;
    output_color=vec4(max(rgb,vec3(0.0)),opacity);
}
