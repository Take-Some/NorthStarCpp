#version 450
// 4-level bloom reduction: level 0 extracts highlights from HDR scene;
// following levels downsample the preceding level with a 9-tap box kernel.
layout(set=0,binding=0) uniform sampler2D previous_level;
layout(push_constant) uniform BloomParams {
    vec4 target; // output width, height, reciprocal width, reciprocal height
    vec4 bloom;  // threshold, knee, intensity, level
} settings;
layout(location=0) out vec4 output_color;
vec3 input_color(vec2 uv) {
    return max(texture(previous_level,clamp(uv,vec2(0.),vec2(1.))).rgb,vec3(0.));
}
void main() {
    vec2 uv=gl_FragCoord.xy*settings.target.zw;
    vec2 texel=1./vec2(textureSize(previous_level,0));
    vec3 result=vec3(0.);
    float total=0.;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        float weight=(x==0?2.:1.)*(y==0?2.:1.);
        // Runtime quality presets: low 1 tap, medium cross 5 taps,
        // high 3x3 Gaussian; no extra allocations or PSO permutations.
        if(settings.bloom.z<1.5 && (x!=0||y!=0))continue;
        if(settings.bloom.z<2.5 && x!=0&&y!=0)continue;
        result+=input_color(uv+vec2(x,y)*texel)*weight;
        total+=weight;
    }
    result/=total;
    if(settings.bloom.w<.5) {
        float brightness=max(max(result.r,result.g),result.b);
        float knee=max(settings.bloom.y,.0001);
        float soft=clamp((brightness-settings.bloom.x+knee)/(2.*knee),0.,1.);
        float contribution=max(brightness-settings.bloom.x,0.)+
                           soft*soft*knee;
        result*=contribution/max(brightness,1e-5);
    }
    output_color=vec4(result,1.);
}
