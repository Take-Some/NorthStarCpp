// NorthStar Engine Sky (D3D12) — per-pixel atmospheric single scattering
// and altitude-bounded 3D volumetric cloud ray marching.
// Canonical global shader: sharedAssets/shaders/sky/atmosphere.hlsl.
// Units: kilometres. Planet model: Earth-like spherical atmosphere.
// GPU simulation and camera layout follows NorthStarGpuSkyUniforms.
cbuffer Camera : register(b0) { row_major float4x4 view_projection; };
cbuffer SkyTint : register(b1) {
    float4 sky_tint;
    float4 sky_sun;
    float4 sky_moon;
    float4 sky_atmo;  // daylight, haze, sun intensity, moon intensity
    float4 sky_cloud; // coverage, speed, cloud animation phase, moon illumination
    float4 sky_flags; // enabled, sun, moon, clouds
    float4 sky_camera_world; // actual camera origin, game-world metres
    float4 sky_camera_view;  // yaw, pitch, vertical FOV radians, aspect
};
Texture2D sky_texture : register(t0); // static cloud weather/flow field
SamplerState sky_sampler : register(s0);
struct SkyVertex { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct SkyPixel { float4 position : SV_Position; float2 uv : TEXCOORD0; };
static const float NS_PI=3.14159265359;
static const float NS_PLANET_R=6360.0;
static const float NS_ATMOS_TOP=6460.0;
static const float3 NS_BETA_R=float3(.005802,.013558,.033100);
static const float3 NS_BETA_O3=float3(.000650,.001881,.000085);

// Backward-compatible geometry skydome pipeline (POSITION + TEXCOORD).
SkyPixel sky_vertex_main(SkyVertex v) {
    SkyPixel p;
    p.position=mul(float4(v.position*100.0,1.0),view_projection);
    p.uv=v.uv;
    return p;
}
// Root GPU sky uses a separate shader without vertex-buffer input semantics.
SkyPixel sky_fullscreen_vertex_main(uint vertex_id:SV_VertexID) {
    SkyPixel p;
    uint c=vertex_id%6u;
    float2 corner=c==0u?float2(-1.,-1.):
                  c==1u?float2(-1., 1.):
                  c==2u?float2( 1.,-1.):
                  c==3u?float2( 1.,-1.):
                  c==4u?float2(-1., 1.):float2(1.,1.);
    p.position=float4(corner,.9999,1.);
    p.uv=float2(corner.x*.5+.5,.5-corner.y*.5);
    return p;
}
float ns_hash3(float3 p) {
    p=frac(p*.1031);
    p+=dot(p,p.yzx+33.33);
    return frac((p.x+p.y)*p.z);
}
float ns_noise3(float3 p) {
    float3 i=floor(p),f=frac(p);
    f=f*f*(3.0-2.0*f);
    float n000=ns_hash3(i);
    float n100=ns_hash3(i+float3(1,0,0));
    float n010=ns_hash3(i+float3(0,1,0));
    float n110=ns_hash3(i+float3(1,1,0));
    float n001=ns_hash3(i+float3(0,0,1));
    float n101=ns_hash3(i+float3(1,0,1));
    float n011=ns_hash3(i+float3(0,1,1));
    float n111=ns_hash3(i+float3(1,1,1));
    return lerp(lerp(lerp(n000,n100,f.x),lerp(n010,n110,f.x),f.y),
                lerp(lerp(n001,n101,f.x),lerp(n011,n111,f.x),f.y),f.z);
}
// Distances are in km. eye_height is relative to the local ground datum.
float2 ns_sphere_interval(float eye_height,float3 ray,float layer_height) {
    float b=(NS_PLANET_R+eye_height)*ray.y;
    float c=(eye_height-layer_height)*(2.0*NS_PLANET_R+eye_height+layer_height);
    float d=b*b-c;
    if(d<0.0)return float2(1.0,-1.0);
    float root=sqrt(d);
    // Compute the smaller-magnitude root without subtracting nearly equal values.
    float q=-b-(b>=0.0?root:-root);
    float alternate=abs(q)>1.e-6?c/q:0.0;
    return float2(min(q,alternate),max(q,alternate));
}
float ns_altitude(float3 from_ground) {
    float y=from_ground.y;
    return (2.0*NS_PLANET_R*y+dot(from_ground,from_ground))/
        (length(from_ground+float3(0.0,NS_PLANET_R,0.0))+NS_PLANET_R);
}
float3 ns_horizon_color() {
    return lerp(float3(.055,.075,.11),float3(.23,.315,.395),saturate(sky_atmo.x));
}
float ns_air_mass(float cosine) {
    // Smoothed air mass near the horizon; avoids a 1/cos(theta) singularity.
    return rcp(max(.055,cosine+.13*exp(-max(cosine,0.0)*13.0)));
}
float ns_mie_phase(float mu,float g) {
    float gg=g*g;
    return (1.0-gg)/(4.0*NS_PI*pow(max(.035,1.0+gg-2.0*g*mu),1.5));
}
float3 ns_atmosphere(float3 viewdir,float3 sun) {
    float eye_height=clamp(sky_camera_world.y*.001,.001,95.0);
    float3 eye=float3(0.0,eye_height,0.0);
    float3 horizon_color=ns_horizon_color();
    float above=smoothstep(-.015,.105,viewdir.y);
    float below=1.0-smoothstep(-.38,-.06,viewdir.y);
    float3 lower=horizon_color*lerp(1.0,.56,below);
    if(above<=0.0)return lower;
    float3 scatter_dir=normalize(float3(viewdir.x,max(.015,viewdir.y),viewdir.z));
    // Cap the numerical integration length where optical depth is saturated.
    float top=min(480.0,ns_sphere_interval(eye_height,scatter_dir,
        NS_ATMOS_TOP-NS_PLANET_R).y);
    float3 accumulated=0.0;
    float3 view_transmittance=1.0;
    float mu=clamp(dot(viewdir,sun),-1.0,1.0);
    float phase_r=3.0*(1.0+mu*mu)/(16.0*NS_PI);
    float phase_m=ns_mie_phase(mu,.76);
    float haze=saturate(sky_atmo.y);
    float beta_m=.003996*(1.0+2.2*haze);
    const int VIEW_STEPS=8;
    [unroll]for(int i=0;i<VIEW_STEPS;++i) {
        // Quadratic spacing is essential at the horizon: the dense atmosphere
        // near the camera receives finer integration steps than empty space.
        float a=float(i)/float(VIEW_STEPS);
        float b=float(i+1)/float(VIEW_STEPS);
        float t0=top*a*a;
        float t1=top*b*b;
        float ds=t1-t0;
        float3 pos=eye+scatter_dir*(.5*(t0+t1));
        float h=max(0.0,ns_altitude(pos));
        float rho_r=exp(-h/8.0);
        float rho_m=exp(-h/1.2);
        float rho_o=exp(-max(0.0,h-23.0)*.07)*
                    smoothstep(8.0,20.0,h)*(1.0-smoothstep(40.0,65.0,h));
        float3 extinction=NS_BETA_R*rho_r+beta_m*rho_m+
                          NS_BETA_O3*rho_o;
        float sun_mu=dot(normalize(pos+float3(0.0,NS_PLANET_R,0.0)),sun);
        float solar_visibility=smoothstep(-.085,.015,sun_mu);
        float optical_sun=ns_air_mass(sun_mu);
        float3 sun_trans=exp(-(NS_BETA_R*(8.0*rho_r)+
                    beta_m*(1.2*rho_m)+NS_BETA_O3*(13.0*rho_o))*optical_sun);
        float3 scatter=NS_BETA_R*(rho_r*phase_r)+
                       float3(beta_m*rho_m*phase_m,
                              beta_m*rho_m*phase_m,
                              beta_m*rho_m*phase_m);
        float3 interval=exp(-extinction*ds);
        float3 source=scatter*sun_trans*solar_visibility*max(0.0,sky_atmo.z)*14.0;
        accumulated+=view_transmittance*source*
                     (1.0-interval)/max(extinction,float3(.00001,.00001,.00001));
        view_transmittance*=interval;
    }
    float3 night=float3(.0022,.0030,.0080)*
        (1.0+2.0*pow(1.0-abs(viewdir.y),4.0));
    float3 color=accumulated+view_transmittance*night*
        (1.0-saturate(sky_atmo.x));
    if(sky_flags.y>.5 && sky_sun.y>-.06) {
        float disc=smoothstep(.999985,.999990,mu);
        float3 radiance=float3(18.0,16.4,13.5)*max(0.0,sky_atmo.z);
        float3 eye_trans=exp(-(NS_BETA_R*8.0+
            beta_m*1.2+NS_BETA_O3*13.0)*ns_air_mass(sun.y));
        color+=radiance*disc*eye_trans*smoothstep(0.0,.02,viewdir.y)*smoothstep(-.06,.02,sun.y);
    }
    float horizon=1.0-smoothstep(.04,.36,viewdir.y);
    color=lerp(color,horizon_color,(.12+.32*haze)*horizon);
    return lerp(lower,max(color,float3(0.0,0.0,0.0)),above);
}
// Three-dimensional Worley F1 with wrapped cellular neighbourhood.
// Evaluated only in the primary march (not every secondary light probe).
float ns_worley3(float3 p) {
    float3 base=floor(p), local=frac(p);
    float nearest=10.0;
    [unroll]for(int z=-1;z<=1;++z)
    [unroll]for(int y=-1;y<=1;++y)
    [unroll]for(int x=-1;x<=1;++x) {
        float3 offset=float3(x,y,z);
        float id=ns_hash3(base+offset);
        float3 feature=frac(float3(id,id*1.6180339,id*2.41421356)*
                              float3(1.0,1.3,1.7));
        float3 d=offset+feature-local;
        nearest=min(nearest,dot(d,d));
    }
    return sqrt(nearest);
}
float ns_cloud_density(float3 pos,float phase,float coverage,bool detailed,float lod) {
    float height=ns_altitude(pos);
    float2 wind=float2(phase*9.0,phase*3.5);
    float2 world=pos.xz+sky_camera_world.xz*.001+wind;
    float2 weather_uv=frac(world*.0105);
    float4 weather=sky_texture.SampleLevel(sky_sampler,weather_uv,0);
    float local_coverage=saturate(coverage*(.55+.80*weather.r));
    // Weather controls the extent and top of cloud cells, while volumetric
    // noise supplies the silhouettes and vertical structure.
    float cloud_top=lerp(3.25,4.90,weather.g);
    float vertical=smoothstep(1.65,2.05,height)*
                   (1.0-smoothstep(cloud_top-.75,cloud_top,height));
    if(vertical<.001 || local_coverage<.004)return 0.0;
    float3 cell=float3(world.x*.36,height*.68,world.y*.36);
    float broad=ns_noise3(cell*.49);
    float billow=lerp(ns_noise3(cell),broad,lod*.8);
    float shape=.76*billow+.24*broad;
    float threshold=lerp(.78,.31,local_coverage);
    if(shape<threshold-.16)return 0.0;
    if(detailed && lod<.82) {
        float cellular=1.0-saturate(ns_worley3(cell*.91)*.87);
        shape=lerp(shape,.67*shape+.33*cellular,1.0-smoothstep(.28,.82,lod));
    }
    float density=smoothstep(threshold-.075,threshold+.15,shape)*vertical;
    if(detailed && lod<.72 && density>.005) {
        float3 detail=cell*3.2+float3(8.3,3.7,5.2);
        float erosion=.72*ns_noise3(detail)+.28*ns_noise3(detail*2.17);
        density=saturate(density-(1.0-erosion)*.28*(1.0-density)*(1.0-smoothstep(.18,.72,lod)));
    }
    return density;
}
float ns_light_optical_depth(float3 p,float3 sun,float phase,float coverage) {
    // Six shadow probes accumulate the actual Beer-Lambert optical depth.
    // A cheap low-frequency density field avoids high-frequency shadow noise.
    const int LIGHT_STEPS=6;
    float tau=0.0;
    const float LIGHT_DISTANCE=4.2;
    [unroll]for(int j=0;j<LIGHT_STEPS;++j) {
        float distance=(float(j)+.5)*LIGHT_DISTANCE/float(LIGHT_STEPS);
        float density=ns_cloud_density(p+sun*distance,phase,coverage,false,0.0);
        tau+=density*(LIGHT_DISTANCE/float(LIGHT_STEPS));
    }
    return tau;
}
float3 ns_cloud_volume(float3 viewdir,float3 sun,float3 background,
                       float2 pixel) {
    if(sky_flags.w<.5 || sky_cloud.x<.006)return background;
    float eye_height=clamp(sky_camera_world.y*.001,.001,95.0);
    float3 eye=float3(0.0,eye_height,0.0);
    float2 outer=ns_sphere_interval(eye_height,viewdir,4.90);
    float2 inner=ns_sphere_interval(eye_height,viewdir,1.65);
    float start=max(0.0,outer.x);
    float end=min(160.0,outer.y);
    // Take the first visible interval of the shell. A camera inside the
    // cloud layer starts integrating at the eye, including above the ground.
    if(inner.y>start) {
        if(inner.x<=start)start=inner.y;
        else end=min(end,inner.x);
    }
    float2 ground=ns_sphere_interval(eye_height,viewdir,0.0);
    if(ground.x>0.0)end=min(end,ground.x);
    if(end<=start)return background;
    float3 radiance=float3(0.0,0.0,0.0);
    float throughput=1.0;
    float phase=sky_cloud.z;
    float sun_strength=saturate((sun.y+.09)*3.0)*max(0.0,sky_atmo.z);
    float mu=clamp(dot(viewdir,sun),-1.0,1.0);
    float phase_forward=ns_mie_phase(mu,.60);
    float phase_backward=ns_mie_phase(mu,-.23);
    float cloud_phase=min(3.0,phase_forward*.73+phase_backward*.27);
    float travel=end-start;
    const int CLOUD_STEPS=44;
    int active_steps=int(clamp(ceil(travel/.32),20.0,float(CLOUD_STEPS)));
    float step_size=travel/float(active_steps);
    // Suppress visible grain at the horizon; only near, well-resolved cloud
    // volumes use a small stable sub-step offset.
    float jitter=.5+(sky_texture.Load(int3(int2(int(pixel.x)&63,int(pixel.y)&63),0)).b-.5)*
        .32*(1.0-smoothstep(7.0,38.0,start));
    float shadow_tau=0.0;
    bool shadow_valid=false;
    [loop]for(int i=0;i<CLOUD_STEPS;++i) {
        if(i>=active_steps)break;
        float t=start+(float(i)+jitter)*step_size;
        float3 pos=eye+viewdir*t;
        float lod=max(smoothstep(14.0,78.0,t),smoothstep(.32,2.6,step_size));
        float density=ns_cloud_density(pos,phase,sky_cloud.x,true,lod);
        if(density<.003)continue;
        float distance_visibility=1.0-smoothstep(80.0,160.0,t);
        float extinction=density*1.18*distance_visibility;
        float step_trans=exp(-extinction*step_size);
        if(sun_strength>.01 && (!shadow_valid || i%3==0)) {
            shadow_tau=ns_light_optical_depth(pos,sun,phase,sky_cloud.x);
            shadow_valid=true;
        }
        float sunlight=sun_strength*exp(-shadow_tau*1.05);
        float powder=1.0+.28*(1.0-exp(-density*1.8));
        float daylight=saturate(sky_atmo.x);
        float upper=smoothstep(1.65,4.9,ns_altitude(pos));
        float3 ambient=lerp(float3(.025,.038,.063),float3(.28,.36,.46),daylight)*
            (.74+.35*upper);
        float3 solar=float3(.96,.89,.77)*sunlight*(.40+.30*cloud_phase)*powder;
        float aerial=1.0-exp(-t*(.010+.026*saturate(sky_atmo.y)));
        float3 cloud_light=lerp(ambient+solar,ns_horizon_color(),aerial);
        radiance+=throughput*(1.0-step_trans)*cloud_light;
        throughput*=step_trans;
        if(throughput<.018)break;
    }
    return background*throughput+radiance;
}
float4 sky_pixel_main(SkyPixel input) : SV_Target {
    if(sky_flags.x<.5) {
        float2 uv=float2(frac(input.uv.x),saturate(input.uv.y));
        float3 legacy=sky_texture.Sample(sky_sampler,uv).rgb;
        return float4(saturate(legacy*sky_tint.rgb),1.0);
    }
    const float2 uv=saturate(input.uv);
    // Reconstruct each pixel's world ray from the camera, not from a
    // meshed sky dome or UV coordinates. Exact sky coverage at all pitches.
    float yaw=sky_camera_view.x;
    float pitch=sky_camera_view.y;
    float fov=sky_camera_view.z;
    float aspect=sky_camera_view.w;
    float3 forward=float3(sin(yaw)*cos(pitch),sin(pitch),
                          cos(yaw)*cos(pitch));
    float3 right=float3(cos(yaw),0.,-sin(yaw));
    float3 up=float3(-sin(yaw)*sin(pitch),cos(pitch),
                     -cos(yaw)*sin(pitch));
    float2 ndc=float2(uv.x*2.-1.,1.-uv.y*2.);
    float factor=tan(fov*.5);
    float3 ray=normalize(forward+right*ndc.x*aspect*factor+
                        up*ndc.y*factor);
    float3 sun=normalize(sky_sun.xyz+float3(0.0,.000001,0.0));
    float3 sky=ns_atmosphere(ray,sun);
    if(sky_flags.z>.5 && sky_moon.y>0.0) {
        float lunar=dot(ray,normalize(sky_moon.xyz));
        sky+=float3(.46,.51,.63)*sky_atmo.w*sky_cloud.w*
              smoothstep(.99993,.99996,lunar);
    }
    sky=ns_cloud_volume(ray,sun,sky,input.position.xy);
    return float4(max(sky*sky_tint.rgb,0.0),1.0);
}
