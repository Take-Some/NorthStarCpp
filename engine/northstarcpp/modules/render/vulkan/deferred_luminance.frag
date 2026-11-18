#version 450
// Same-frame GPU luminance meter: 8x8 stratified locations in linear HDR.
// Each RG16F pixel stores log2(luminance). Temporal adaptation is CPU-side.
layout(set=0,binding=0) uniform sampler2D scene;
layout(location=0) out vec2 log_luminance;
void main(){
    vec2 uv=gl_FragCoord.xy/8.;
    vec3 color=max(textureLod(scene,uv,0.).rgb,vec3(0.));
    float luminance=dot(color,vec3(.2126,.7152,.0722));
    log_luminance=vec2(log2(max(luminance,.0001)),0.);
}
