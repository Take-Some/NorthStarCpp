// Root Engine Sky sensor optics. Added to HDR *after* opaque geometry, so
// the sun and its bloom are occluded by the real scene before flare appears.
// Requires hdr_scene, scene_sampler, sun_screen, sun_optics in the including PS.
float ns_optics_brightness(float2 uv) {
    float3 light=max(hdr_scene.SampleLevel(scene_sampler,saturate(uv),0).rgb,0.);
    return saturate((dot(light,float3(.2126,.7152,.0722))-1.12)*.65);
}
float3 ns_sun_optics(float2 uv) {
    if(sun_optics.x<=0.001 || sun_screen.z<=0.5)
        return float3(0.,0.,0.);
    float2 center=sun_screen.xy;
    float2 pixel=float2(1.0/max(viewport_size.x,1.),1.0/max(viewport_size.y,1.));
    float visibility=0.;
    [unroll]for(int y=-2;y<=2;++y)
        [unroll]for(int x=-2;x<=2;++x)
            visibility=max(visibility,ns_optics_brightness(center+float2(x,y)*pixel*2.));
    if(visibility<.005)return float3(0.,0.,0.);
    float2 axis=(center-float2(.5,.5));
    float2 dp=uv-center;
    // Backlit radial shafts: sample the rendered HDR scene, not the sky
    // uniform alone. World geometry can therefore block the source.
    float shafts=0.;
    [unroll]for(int i=0;i<10;++i) {
        float t=(float(i)+.7)/10.;
        float2 probe=uv-dp*t*.92;
        float nearSun=saturate(1.-length(probe-center)*4.);
        shafts+=ns_optics_brightness(probe)*nearSun*(1.-t*.43);
    }
    shafts*=.10*visibility;
    float3 rays=shafts*float3(.91,.70,.45)*max(0.,sun_optics.y);
    // Optical ghosts lie on the line through the optical axis.
    float2 ghosts[4]={
        center-axis*.72,center-axis*1.53,
        center-axis*2.17,center-axis*2.7
    };
    float radii[4]={.038,.063,.023,.044};
    float strengths[4]={.17,.065,.14,.07};
    float3 colors[4]={
        float3(.25,.52,.82),float3(.90,.32,.15),
        float3(.38,.82,.59),float3(.60,.36,.88)
    };
    float3 flare=0.;
    [unroll]for(int j=0;j<4;++j) {
        float2 ellipse=(uv-ghosts[j])*float2(viewport_size.x/max(viewport_size.y,1.),1.);
        float d=length(ellipse)/radii[j];
        float ghost=exp(-d*d*2.8);
        flare+=colors[j]*ghost*strengths[j];
    }
    float2 streak=(uv-center)*float2(1.,7.);
    float anamorphic=exp(-dot(streak,streak)*13.)*.11;
    flare+=float3(.46,.60,.88)*anamorphic;
    return rays+flare*visibility*max(0.,sun_optics.z);
}
