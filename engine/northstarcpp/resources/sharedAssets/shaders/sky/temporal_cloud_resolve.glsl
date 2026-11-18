#version 450
// Vulkan cloud-only history resolve. Binding and std140 layout match the
// dedicated HLSL cloud temporal interface; these are not scene TAA resources.
layout(set=0,binding=0) uniform sampler2D cloud_current;
layout(set=0,binding=1) uniform sampler2D cloud_depth_current;
layout(set=0,binding=2) uniform sampler2D cloud_previous;
layout(set=0,binding=3) uniform sampler2D cloud_depth_previous;
layout(set=0,binding=4,std140) uniform CloudTemporalConstants {
    vec4 current_origin_km;
    vec4 current_forward;
    vec4 current_right;
    vec4 current_up;
    vec4 previous_origin_km;
    vec4 previous_forward;
    vec4 previous_right;
    vec4 previous_up;
    vec4 current_projection;
    vec4 previous_projection;
    vec4 temporal_settings;
    vec4 temporal_flags;
} p;
layout(location=0) out vec4 output_color;
void main() {
    ivec2 extent=ivec2(p.current_projection.zw);
    ivec2 pixel=clamp(ivec2(gl_FragCoord.xy),ivec2(0),extent-1);
    vec4 now=texelFetch(cloud_current,pixel,0);
    float now_depth=texelFetch(cloud_depth_current,pixel,0).r;
    if(p.temporal_flags.x<.5||now_depth<=0.||now.a<=.015) {
        output_color=now;return;
    }
    vec2 uv=(vec2(pixel)+.5)/p.current_projection.zw;
    vec2 ndc=vec2(uv.x*2.-1.,1.-uv.y*2.);
    vec3 direction=normalize(p.current_forward.xyz+
        p.current_right.xyz*ndc.x*p.current_projection.y*p.current_projection.x+
        p.current_up.xyz*ndc.y*p.current_projection.x);
    vec3 world=p.current_origin_km.xyz+direction*now_depth;
    vec3 delta=world-p.previous_origin_km.xyz;
    float forward=dot(delta,p.previous_forward.xyz);
    if(forward<=.001){output_color=now;return;}
    vec2 previous_uv=vec2(
        .5+.5*dot(delta,p.previous_right.xyz)/
                 (forward*p.previous_projection.y*p.previous_projection.x),
        .5-.5*dot(delta,p.previous_up.xyz)/
                 (forward*p.previous_projection.x));
    if(any(lessThanEqual(previous_uv,vec2(0.)))||
       any(greaterThanEqual(previous_uv,vec2(1.)))) {
        output_color=now;return;
    }
    vec4 old=textureLod(cloud_previous,previous_uv,0.);
    float old_depth=textureLod(cloud_depth_previous,previous_uv,0.).r;
    if(old_depth<=0.||
       abs(old.a-now.a)>p.temporal_settings.y||
       abs(old_depth-now_depth)/max(now_depth,.001)>p.temporal_settings.z) {
        output_color=now;return;
    }
    vec3 mean=vec3(0.),squares=vec3(0.),minimum=vec3(65504.),maximum=vec3(0.);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 q=clamp(pixel+ivec2(x,y),ivec2(0),extent-1);
        vec3 value=texelFetch(cloud_current,q,0).rgb;
        mean+=value/9.;
        squares+=value*value/9.;
        minimum=min(minimum,value);
        maximum=max(maximum,value);
    }
    vec3 sigma=sqrt(max(vec3(0.),squares-mean*mean));
    vec3 low=max(minimum,mean-1.5*sigma);
    vec3 high=min(maximum,mean+1.5*sigma);
    vec3 history_color=clamp(old.rgb,min(low,high),max(low,high));
    vec2 velocity=(previous_uv-uv)*p.current_projection.zw;
    float weight=clamp(p.temporal_settings.x,0.,1.)/
        (1.+length(velocity)*max(p.temporal_settings.w,0.));
    output_color=vec4(mix(now.rgb,history_color,weight),now.a);
}
