#version 450
// Full-scene FP16 -> display transform. UI is rendered after this pass.
layout(set=0,binding=0) uniform sampler2D scene_color;
layout(set=0,binding=1) uniform sampler2D bloom_0;
layout(set=0,binding=2) uniform sampler2D bloom_1;
layout(set=0,binding=3) uniform sampler2D bloom_2;
layout(set=0,binding=4) uniform sampler2D bloom_3;
layout(set=0,binding=5) uniform sampler2D luminance_meter;
layout(set=0,binding=6) uniform sampler2D motion_pixels;
layout(push_constant) uniform CompositeParams {
    vec4 viewport; // display width, height, inverse width, inverse height
    vec4 look;     // exposure EV, contrast, saturation, bloom intensity
    vec4 film;     // vignette, grain, chromatic aberration pixels, frame index
    vec4 flags;    // target is SRGB, reserved, reserved, reserved
} params;
layout(location=0) out vec4 output_color;
float luminance(vec3 v){return dot(v,vec3(.2126,.7152,.0722));}
vec3 aces_fitted(vec3 v) {
    v=max(v,vec3(0.));
    return clamp((v*(2.51*v+.03))/(v*(2.43*v+.59)+.14),0.,1.);
}
vec3 bloom_color(vec2 uv) {
    int levels=clamp(int(params.flags.y+.5),0,4);
    if(levels==0)return vec3(0);
    const float weights[4]=float[4](.45,.28,.17,.10);
    vec3 result=vec3(0.);
    float total=0.;
    if(levels>0){result+=texture(bloom_0,uv).rgb*weights[0];total+=weights[0];}
    if(levels>1){result+=texture(bloom_1,uv).rgb*weights[1];total+=weights[1];}
    if(levels>2){result+=texture(bloom_2,uv).rgb*weights[2];total+=weights[2];}
    if(levels>3){result+=texture(bloom_3,uv).rgb*weights[3];total+=weights[3];}
    return result/max(total,0.0001);
}
float film_noise(uvec2 pixel,uint frame) {
    uint seed=(pixel.x*0x9e3779b9u)^(pixel.y*0x85ebca6bu)^(frame*0xc2b2ae35u);
    seed^=seed>>16;seed*=0x7feb352du;
    seed^=seed>>15;seed*=0x846ca68bu;seed^=seed>>16;
    return float(seed&0x00ffffffu)*(1./16777216.)-.5;
}
void main() {
    vec2 uv=gl_FragCoord.xy*params.viewport.zw;
    if(params.flags.w>.5) {
        vec2 velocity=texelFetch(motion_pixels,ivec2(gl_FragCoord.xy),0).rg;
        output_color=vec4(clamp(vec3(.5+velocity.x/32.,
                                  .5+velocity.y/32.,length(velocity)/16.),
                                  0.,1.),1.);
        return;
    }
    vec3 hdr;
    if(params.film.z>0.) {
        vec2 offset=(uv-.5)*params.viewport.zw*params.film.z;
        hdr=vec3(texture(scene_color,clamp(uv+offset,0.,1.)).r,
                 texture(scene_color,uv).g,
                 texture(scene_color,clamp(uv-offset,0.,1.)).b);
    } else hdr=texture(scene_color,uv).rgb;
    hdr=max(hdr,vec3(0.))+max(params.look.w,0.)*bloom_color(uv);
    float exposure_ev=params.look.x;
    if(params.flags.z>.5) {
        // D3D12 uses the current-frame 8x8 log-luminance meter on the
        // first frame. No forced exposure=1 flash before temporal history.
        float mean_log=0.;
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)
            mean_log+=texelFetch(luminance_meter,ivec2(x,y),0).r;
        mean_log/=64.;
        exposure_ev+=clamp(log2(.18)-mean_log,-2.,2.);
    }
    vec3 mapped=aces_fitted(hdr*exp2(exposure_ev));
    mapped=(mapped-.5)*params.look.y+.5;
    mapped=mix(vec3(luminance(mapped)),mapped,params.look.z);
    if(params.film.x>0.) {
        vec2 centered=uv-.5;
        centered.x*=params.viewport.x/max(1.,params.viewport.y);
        mapped*=1.-params.film.x*smoothstep(.03,.55,dot(centered,centered));
    }
    // Store display-linear when swapchain is SRGB (hardware encodes);
    // otherwise encode the UNORM swapchain explicitly.
    mapped=clamp(mapped,0.,1.);
    if(params.flags.x<.5) {
        // Match D3D12's exact IEC 61966-2-1 display encoding, not an
        // approximate gamma 2.2. SRGB swapchains encode in hardware.
        mapped=mix(mapped*12.92,
            1.055*pow(mapped,vec3(1./2.4))-.055,
            step(vec3(.0031308),mapped));
    }
    if(params.film.y>0.) {
        float noise=film_noise(uvec2(gl_FragCoord.xy),uint(params.film.w));
        mapped+=params.film.y*noise;
    }
    output_color=vec4(clamp(mapped,0.,1.),1.);
}
