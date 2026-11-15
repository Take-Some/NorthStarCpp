#version 450
layout(set=0,binding=0) uniform sampler2D sand_albedo;
layout(set=0,binding=1) uniform sampler2D sand_normal;
layout(set=0,binding=2) uniform sampler2D sand_roughness;
layout(set=0,binding=3) uniform sampler2D rock_albedo;
layout(set=0,binding=4) uniform sampler2D rock_normal;
layout(set=0,binding=5) uniform sampler2D rock_roughness;
layout(set=0,binding=6) uniform sampler2D forest_albedo;
layout(set=0,binding=7) uniform sampler2D forest_normal;
layout(set=0,binding=8) uniform sampler2D forest_roughness;
layout(location=0) in vec4 vertex_color;
layout(location=1) in vec3 vertex_world;
layout(location=2) flat in int vertex_surface;
layout(location=0) out vec4 output_color;

vec3 triplanar_albedo(vec3 weights,vec3 pos,int kind) {
    vec2 yz=pos.yz*.65;
    vec2 xz=pos.xz*.65;
    vec2 xy=pos.xy*.65;
    vec3 a,b,c;
    if(kind==1) {
        a=texture(sand_albedo,yz).rgb;
        b=texture(sand_albedo,xz).rgb;
        c=texture(sand_albedo,xy).rgb;
    }else if(kind==2) {
        a=texture(rock_albedo,yz).rgb;
        b=texture(rock_albedo,xz).rgb;
        c=texture(rock_albedo,xy).rgb;
    }else {
        a=texture(forest_albedo,yz).rgb;
        b=texture(forest_albedo,xz).rgb;
        c=texture(forest_albedo,xy).rgb;
    }
    return a*weights.x+b*weights.y+c*weights.z;
}
vec3 triplanar_normal(vec3 weights,vec3 pos,int kind) {
    vec2 yz=pos.yz*.65;
    vec2 xz=pos.xz*.65;
    vec2 xy=pos.xy*.65;
    vec3 a,b,c;
    if(kind==1) {
        a=texture(sand_normal,yz).rgb;
        b=texture(sand_normal,xz).rgb;
        c=texture(sand_normal,xy).rgb;
    }else if(kind==2) {
        a=texture(rock_normal,yz).rgb;
        b=texture(rock_normal,xz).rgb;
        c=texture(rock_normal,xy).rgb;
    }else {
        a=texture(forest_normal,yz).rgb;
        b=texture(forest_normal,xz).rgb;
        c=texture(forest_normal,xy).rgb;
    }
    return a*weights.x+b*weights.y+c*weights.z;
}
float triplanar_rough(vec3 weights,vec3 pos,int kind) {
    vec2 yz=pos.yz*.65;
    vec2 xz=pos.xz*.65;
    vec2 xy=pos.xy*.65;
    float a,b,c;
    if(kind==1) {
        a=texture(sand_roughness,yz).r;
        b=texture(sand_roughness,xz).r;
        c=texture(sand_roughness,xy).r;
    }else if(kind==2) {
        a=texture(rock_roughness,yz).r;
        b=texture(rock_roughness,xz).r;
        c=texture(rock_roughness,xy).r;
    }else {
        a=texture(forest_roughness,yz).r;
        b=texture(forest_roughness,xz).r;
        c=texture(forest_roughness,xy).r;
    }
    return a*weights.x+b*weights.y+c*weights.z;
}
void main() {
    if(vertex_surface==0) { output_color=vertex_color;return; }
    vec3 dx=dFdx(vertex_world),dy=dFdy(vertex_world);
    vec3 surface_normal=normalize(cross(dx,dy));
    vec3 weights=pow(abs(surface_normal),vec3(4.0));
    weights/=max(weights.x+weights.y+weights.z,.0001);
    vec3 albedo=triplanar_albedo(weights,vertex_world,vertex_surface);
    vec3 raw_normal=triplanar_normal(weights,vertex_world,vertex_surface);
    float roughness=triplanar_rough(weights,vertex_world,vertex_surface);
    vec2 tangent=raw_normal.rg*2.0-1.0;
    vec3 world_normal=normalize(surface_normal+vec3(tangent.x,0.0,tangent.y)*.18);
    float light=.56+.36*clamp(dot(abs(world_normal),
                        normalize(vec3(.35,.85,.25))),0.0,1.0);
    float brightness=mix(.92,1.05,clamp(roughness,0.0,1.0));
    vec3 rgb=clamp(albedo*vertex_color.rgb*light*brightness,0.0,1.0);
    // The selected Vulkan swapchain format may be UNORM or sRGB.
    // Keep this shader in linear colors, consistent with attachment format.
    output_color=vec4(rgb,1.0);
}
