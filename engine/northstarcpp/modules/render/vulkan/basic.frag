#version 450
layout(location=0) in vec4 vertex_color;
layout(location=1) in vec3 vertex_world;
layout(location=0) out vec4 output_color;
// Shared 32-byte fragment-only push contract for primitive and BCn world.
layout(push_constant) uniform ForwardAtmosphere {
    vec4 camera;     // eye world XYZ (metres), authored density in w
    vec4 haze;       // atmosphere-scattered RGB, remaining channel unused
} atmosphere;
vec3 resolve_forward_haze(vec3 rgb,vec3 world) {
    if(atmosphere.camera.w<=0.)return rgb;
    vec3 delta=world-atmosphere.camera.xyz;
    float distance=length(delta);
    float h0=exp(clamp(-.025*atmosphere.camera.y,-8.,8.));
    float h1=exp(clamp(-.025*world.y,-8.,8.));
    float tau=min(20.,atmosphere.camera.w*distance*
                  clamp(.5*(h0+h1),.01,8.));
    float extinction=min(.96,1.-exp(-tau));
    return mix(rgb,atmosphere.haze.rgb,extinction);
}
void main() {
    output_color=vec4(resolve_forward_haze(vertex_color.rgb,vertex_world),1.);
}
