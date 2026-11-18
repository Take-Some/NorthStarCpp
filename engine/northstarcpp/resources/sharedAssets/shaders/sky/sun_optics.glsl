// Root Engine Sky sensor optics for Vulkan HDR compositor.
// GLSL caller declares scene_color sampler2D, params.viewport, solar screen UV.
float ns_optics_brightness(vec2 uv) {
    vec3 value=max(texture(scene_color,clamp(uv,vec2(0.),vec2(1.))).rgb,vec3(0.));
    return clamp((dot(value,vec3(.2126,.7152,.0722))-1.12)*.65,0.,1.);
}
vec3 ns_sun_optics(vec2 uv, vec4 sun_screen, vec4 sun_optics, vec4 viewport) {
    if(sun_optics.x<=.001 || sun_screen.z<=.5) return vec3(0.);
    vec2 center=sun_screen.xy;
    vec2 pixel=viewport.zw;
    float visibility=0.;
    for(int y=-2;y<=2;++y)
      for(int x=-2;x<=2;++x)
        visibility=max(visibility,
            ns_optics_brightness(center+vec2(x,y)*pixel*2.));
    if(visibility<.005)return vec3(0.);
    vec2 axis=center-vec2(.5);
    vec2 delta=uv-center;
    float shafts=0.;
    for(int i=0;i<10;++i) {
        float t=(float(i)+.7)/10.;
        vec2 probe=uv-delta*t*.92;
        float nearSun=clamp(1.-length(probe-center)*4.,0.,1.);
        shafts+=ns_optics_brightness(probe)*nearSun*(1.-t*.43);
    }
    vec3 rays=shafts*.10*visibility*vec3(.91,.70,.45)*
        max(0.,sun_optics.y);
    vec2 ghosts[4]=vec2[4](center-axis*.72,center-axis*1.53,
                           center-axis*2.17,center-axis*2.7);
    float radii[4]=float[4](.038,.063,.023,.044);
    float strengths[4]=float[4](.17,.065,.14,.07);
    vec3 colors[4]=vec3[4](vec3(.25,.52,.82),vec3(.90,.32,.15),
                            vec3(.38,.82,.59),vec3(.60,.36,.88));
    vec3 flare=vec3(0.);
    for(int j=0;j<4;++j) {
        vec2 ellipse=(uv-ghosts[j])*vec2(viewport.x/max(viewport.y,1.),1.);
        float d=length(ellipse)/radii[j];
        flare+=colors[j]*exp(-d*d*2.8)*strengths[j];
    }
    vec2 streak=(uv-center)*vec2(1.,7.);
    flare+=vec3(.46,.60,.88)*exp(-dot(streak,streak)*13.)*.11;
    return rays+flare*visibility*max(0.,sun_optics.z);
}
