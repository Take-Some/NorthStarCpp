// AUTO-GENERATED. DO NOT EDIT. Edit sharedAssets/shaders/sky/ instead.
#pragma once
namespace northstarcpp::shaders {
// atmosphere.hlsl SHA-256 64dea9e87b251a2107bb067c4979be4fe06dac206e3cd2f844f94fcab29e2d95
inline constexpr char sky_atmosphere_hlsl[] = R"NS_SKY_SHADER(// NorthStar Engine Sky (D3D12) — per-pixel atmospheric single scattering
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
)NS_SKY_SHADER";
// atmosphere.glsl SHA-256 435bc379661ff25be9a5b9a44f8d4ee8a8c937c873217022dfc72ee28acf2e17
inline constexpr char sky_atmosphere_glsl[] = R"NS_SKY_SHADER(// NorthStar Engine Sky (Vulkan) — canonical global physically based
// single-scattering atmosphere + height-bounded, 3D volumetric cloud march.
// Included into NS_GPU_SKY material variant; requires gpu_sky and albedo_sampler.
// Units: kilometres; Earth-like planet with a local ground datum.
const float NS2_PI=3.14159265359;
const float NS2_PLANET_R=6360.0;
const float NS2_ATMOS_TOP=6460.0;
const vec3 NS2_BETA_R=vec3(.005802,.013558,.033100);
const vec3 NS2_BETA_O3=vec3(.000650,.001881,.000085);
float ns2_hash3(vec3 p) {
    p=fract(p*.1031);
    p+=dot(p,p.yzx+33.33);
    return fract((p.x+p.y)*p.z);
}
float ns2_noise3(vec3 p) {
    vec3 i=floor(p),f=fract(p);
    f=f*f*(3.-2.*f);
    float n000=ns2_hash3(i);
    float n100=ns2_hash3(i+vec3(1,0,0));
    float n010=ns2_hash3(i+vec3(0,1,0));
    float n110=ns2_hash3(i+vec3(1,1,0));
    float n001=ns2_hash3(i+vec3(0,0,1));
    float n101=ns2_hash3(i+vec3(1,0,1));
    float n011=ns2_hash3(i+vec3(0,1,1));
    float n111=ns2_hash3(i+vec3(1,1,1));
    return mix(mix(mix(n000,n100,f.x),mix(n010,n110,f.x),f.y),
               mix(mix(n001,n101,f.x),mix(n011,n111,f.x),f.y),f.z);
}
// Distances are in km. eye_height is relative to the local ground datum.
vec2 ns2_sphere_interval(float eye_height,vec3 ray,float layer_height) {
    float b=(NS2_PLANET_R+eye_height)*ray.y;
    float c=(eye_height-layer_height)*(2.0*NS2_PLANET_R+eye_height+layer_height);
    float d=b*b-c;
    if(d<0.0)return vec2(1.0,-1.0);
    float root=sqrt(d);
    // Compute the smaller-magnitude root without subtracting nearly equal values.
    float q=-b-(b>=0.0?root:-root);
    float alternate=abs(q)>1.e-6?c/q:0.0;
    return vec2(min(q,alternate),max(q,alternate));
}
float ns2_altitude(vec3 from_ground) {
    float y=from_ground.y;
    return (2.0*NS2_PLANET_R*y+dot(from_ground,from_ground))/
        (length(from_ground+vec3(0.0,NS2_PLANET_R,0.0))+NS2_PLANET_R);
}
vec3 ns2_horizon_color() {
    return mix(vec3(.055,.075,.11),vec3(.23,.315,.395),clamp(gpu_sky.atmo.x,0.0,1.0));
}
float ns2_air_mass(float cosine) {
    return 1./max(.055,cosine+.13*exp(-max(cosine,0.)*13.));
}
float ns2_mie_phase(float mu,float g) {
    float gg=g*g;
    return (1.-gg)/(4.*NS2_PI*pow(max(.035,1.+gg-2.*g*mu),1.5));
}
vec3 ns2_atmosphere(vec3 viewdir,vec3 sun) {
    float eye_height=clamp(gpu_sky.camera_world.y*.001,.001,95.0);
    vec3 eye=vec3(0.0,eye_height,0.0);
    vec3 horizon_color=ns2_horizon_color();
    float above=smoothstep(-.015,.105,viewdir.y);
    float below=1.0-smoothstep(-.38,-.06,viewdir.y);
    vec3 lower=horizon_color*mix(1.0,.56,below);
    if(above<=0.0)return lower;
    vec3 scatter_dir=normalize(vec3(viewdir.x,max(.015,viewdir.y),viewdir.z));
    // Cap the numerical integration length where optical depth is saturated.
    float top=min(480.0,ns2_sphere_interval(eye_height,scatter_dir,
        NS2_ATMOS_TOP-NS2_PLANET_R).y);
    vec3 accumulated=vec3(0.);
    vec3 view_transmittance=vec3(1.);
    float mu=clamp(dot(viewdir,sun),-1.,1.);
    float phase_r=3.*(1.+mu*mu)/(16.*NS2_PI);
    float phase_m=ns2_mie_phase(mu,.76);
    float haze=clamp(gpu_sky.atmo.y,0.,1.);
    float beta_m=.003996*(1.+2.2*haze);
    const int VIEW_STEPS=8;
    for(int i=0;i<VIEW_STEPS;++i) {
        float a=float(i)/float(VIEW_STEPS);
        float b=float(i+1)/float(VIEW_STEPS);
        float t0=top*a*a;
        float t1=top*b*b;
        float ds=t1-t0;
        vec3 pos=eye+scatter_dir*(.5*(t0+t1));
        float h=max(0.,ns2_altitude(pos));
        float rho_r=exp(-h/8.);
        float rho_m=exp(-h/1.2);
        float rho_o=exp(-max(0.,h-23.)*.07)*
                    smoothstep(8.,20.,h)*(1.-smoothstep(40.,65.,h));
        vec3 extinction=NS2_BETA_R*rho_r+
                        vec3(beta_m*rho_m)+NS2_BETA_O3*rho_o;
        float sun_mu=dot(normalize(pos+vec3(0.0,NS2_PLANET_R,0.0)),sun);
        float visibility=smoothstep(-.085,.015,sun_mu);
        float air_mass=ns2_air_mass(sun_mu);
        vec3 trans_sun=exp(-(NS2_BETA_R*(8.*rho_r)+
            vec3(beta_m*(1.2*rho_m))+NS2_BETA_O3*(13.*rho_o))*air_mass);
        vec3 scatter=NS2_BETA_R*(rho_r*phase_r)+
                     vec3(beta_m*rho_m*phase_m);
        vec3 interval=exp(-extinction*ds);
        vec3 source=scatter*trans_sun*visibility*
                    max(0.,gpu_sky.atmo.z)*14.;
        accumulated+=view_transmittance*source*
                     (vec3(1.)-interval)/max(extinction,vec3(.00001));
        view_transmittance*=interval;
    }
    vec3 night=vec3(.0022,.0030,.0080)*
        (1.+2.*pow(1.-abs(viewdir.y),4.));
    vec3 color=accumulated+view_transmittance*night*
               (1.-clamp(gpu_sky.atmo.x,0.,1.));
    if(gpu_sky.flags.y>.5 && gpu_sky.sun_dir.y>-.06) {
        float disc=smoothstep(.999985,.999990,mu);
        vec3 radiance=vec3(18.,16.4,13.5)*max(0.,gpu_sky.atmo.z);
        vec3 eye_trans=exp(-(NS2_BETA_R*8.+
            vec3(beta_m*1.2)+NS2_BETA_O3*13.)*ns2_air_mass(sun.y));
        color+=radiance*disc*eye_trans*smoothstep(0.0,.02,viewdir.y)*smoothstep(-.06,.02,sun.y);
    }
    float horizon=1.0-smoothstep(.04,.36,viewdir.y);
    color=mix(color,horizon_color,(.12+.32*haze)*horizon);
    return mix(lower,max(color,vec3(0.0,0.0,0.0)),above);
}
// Canonical Vulkan parity with HLSL: 3D Worley/Perlin shapes, fine erosion,
// 2D weather coverage and stable screen-space raymarch jitter.
float ns2_worley3(vec3 p) {
    vec3 base=floor(p),local=fract(p);
    float nearest=10.;
    for(int z=-1;z<=1;++z)
    for(int y=-1;y<=1;++y)
    for(int x=-1;x<=1;++x) {
        vec3 offset=vec3(x,y,z);
        float id=ns2_hash3(base+offset);
        vec3 feature=fract(vec3(id,id*1.6180339,id*2.41421356)*
                            vec3(1.,1.3,1.7));
        vec3 d=offset+feature-local;
        nearest=min(nearest,dot(d,d));
    }
    return sqrt(nearest);
}
float ns2_cloud_density(vec3 pos,float phase,float coverage,bool detailed,float lod) {
    float height=ns2_altitude(pos);
    vec2 wind=vec2(phase*9.0,phase*3.5);
    vec2 world=pos.xz+gpu_sky.camera_world.xz*.001+wind;
    vec2 weather_uv=fract(world*.0105);
    vec4 weather=textureLod(albedo_sampler,weather_uv,0.0);
    float local_coverage=clamp(coverage*(.55+.80*weather.r),0.0,1.0);
    // Weather controls the extent and top of cloud cells, while volumetric
    // noise supplies the silhouettes and vertical structure.
    float cloud_top=mix(3.25,4.90,weather.g);
    float vertical=smoothstep(1.65,2.05,height)*
                   (1.0-smoothstep(cloud_top-.75,cloud_top,height));
    if(vertical<.001 || local_coverage<.004)return 0.0;
    vec3 cell=vec3(world.x*.36,height*.68,world.y*.36);
    float broad=ns2_noise3(cell*.49);
    float billow=mix(ns2_noise3(cell),broad,lod*.8);
    float shape=.76*billow+.24*broad;
    float threshold=mix(.78,.31,local_coverage);
    if(shape<threshold-.16)return 0.0;
    if(detailed && lod<.82) {
        float cellular=1.0-clamp(ns2_worley3(cell*.91)*.87,0.0,1.0);
        shape=mix(shape,.67*shape+.33*cellular,1.0-smoothstep(.28,.82,lod));
    }
    float density=smoothstep(threshold-.075,threshold+.15,shape)*vertical;
    if(detailed && lod<.72 && density>.005) {
        vec3 detail=cell*3.2+vec3(8.3,3.7,5.2);
        float erosion=.72*ns2_noise3(detail)+.28*ns2_noise3(detail*2.17);
        density=clamp(density-(1.0-erosion)*.28*(1.0-density)*(1.0-smoothstep(.18,.72,lod)),0.0,1.0);
    }
    return density;
}
float ns2_light_optical_depth(vec3 p,vec3 sun,float phase,float coverage) {
    const int LIGHT_STEPS=6;
    const float LIGHT_DISTANCE=4.2;
    float tau=0.;
    for(int j=0;j<LIGHT_STEPS;++j) {
        float dist=(float(j)+.5)*LIGHT_DISTANCE/float(LIGHT_STEPS);
        float density=ns2_cloud_density(p+sun*dist,phase,coverage,false,0.);
        tau+=density*(LIGHT_DISTANCE/float(LIGHT_STEPS));
    }
    return tau;
}
vec3 ns2_cloud_volume(vec3 viewdir,vec3 sun,vec3 background) {
    if(gpu_sky.flags.w<.5 || gpu_sky.clouds.x<.006)return background;
    float eye_height=clamp(gpu_sky.camera_world.y*.001,.001,95.0);
    vec3 eye=vec3(0.0,eye_height,0.0);
    vec2 outer=ns2_sphere_interval(eye_height,viewdir,4.90);
    vec2 inner=ns2_sphere_interval(eye_height,viewdir,1.65);
    float start=max(0.0,outer.x);
    float end=min(160.0,outer.y);
    // Take the first visible interval of the shell. A camera inside the
    // cloud layer starts integrating at the eye, including above the ground.
    if(inner.y>start) {
        if(inner.x<=start)start=inner.y;
        else end=min(end,inner.x);
    }
    vec2 ground=ns2_sphere_interval(eye_height,viewdir,0.0);
    if(ground.x>0.0)end=min(end,ground.x);
    if(end<=start)return background;
    vec3 radiance=vec3(0.0,0.0,0.0);
    float throughput=1.0;
    float phase=gpu_sky.clouds.z;
    float sun_strength=clamp((sun.y+.09)*3.,0.,1.)*
                       max(0.,gpu_sky.atmo.z);
    float mu=clamp(dot(viewdir,sun),-1.,1.);
    float phase_forward=ns2_mie_phase(mu,.60);
    float phase_backward=ns2_mie_phase(mu,-.23);
    float cloud_phase=min(3.,phase_forward*.73+phase_backward*.27);
    float travel=end-start;
    const int CLOUD_STEPS=44;
    int active_steps=int(clamp(ceil(travel/.32),20.0,float(CLOUD_STEPS)));
    float step_size=travel/float(active_steps);
    // Suppress visible grain at the horizon; only near, well-resolved cloud
    // volumes use a small stable sub-step offset.
    float jitter=.5+(texelFetch(albedo_sampler,ivec2(gl_FragCoord.xy)&ivec2(63),0).b-.5)*
        .32*(1.0-smoothstep(7.0,38.0,start));
    float shadow_tau=0.;
    bool shadow_valid=false;
    for(int i=0;i<CLOUD_STEPS;++i) {
        if(i>=active_steps)break;
        float t=start+(float(i)+jitter)*step_size;
        vec3 p=eye+viewdir*t;
        float lod=max(smoothstep(14.0,78.0,t),smoothstep(.32,2.6,step_size));
        float density=ns2_cloud_density(p,phase,gpu_sky.clouds.x,true,lod);
        if(density<.003)continue;
        float distance_visibility=1.0-smoothstep(80.0,160.0,t);
        float extinction=density*1.18*distance_visibility;
        float step_trans=exp(-extinction*step_size);
        if(sun_strength>.01 && (!shadow_valid || i%3==0)) {
            shadow_tau=ns2_light_optical_depth(p,sun,phase,gpu_sky.clouds.x);
            shadow_valid=true;
        }
        float sunlight=sun_strength*exp(-shadow_tau*1.05);
        float powder=1.0+.28*(1.0-exp(-density*1.8));
        float daylight=clamp(gpu_sky.atmo.x,0.0,1.0);
        float upper=smoothstep(1.65,4.9,ns2_altitude(p));
        vec3 ambient=mix(vec3(.025,.038,.063),vec3(.28,.36,.46),daylight)*
            (.74+.35*upper);
        vec3 solar=vec3(.96,.89,.77)*sunlight*(.40+.30*cloud_phase)*powder;
        float aerial=1.0-exp(-t*(.010+.026*clamp(gpu_sky.atmo.y,0.0,1.0)));
        vec3 cloud_light=mix(ambient+solar,ns2_horizon_color(),aerial);
        radiance+=throughput*(1.0-step_trans)*cloud_light;
        throughput*=step_trans;
        if(throughput<.018)break;
    }
    return background*throughput+radiance;
}
vec4 ns_render_gpu_sky(vec3 viewdir,vec4 tint) {
    vec3 sun=normalize(gpu_sky.sun_dir.xyz+vec3(0.,.000001,0.));
    vec3 sky=ns2_atmosphere(viewdir,sun);
    if(gpu_sky.flags.z>.5 && gpu_sky.moon_dir.y>0.) {
        float lunar=dot(viewdir,normalize(gpu_sky.moon_dir.xyz));
        sky+=vec3(.46,.51,.63)*gpu_sky.atmo.w*gpu_sky.clouds.w*
             smoothstep(.99993,.99996,lunar);
    }
    sky=ns2_cloud_volume(viewdir,sun,sky);
    return vec4(max(sky*tint.rgb,vec3(0.)),1.);
}
)NS_SKY_SHADER";
// sun_optics.hlsl SHA-256 12a9eaeb91b0a1a859eea2670f4d72f973dc722d47eeea62b3dc597ca1127ab4
inline constexpr char sky_sun_optics_hlsl[] = R"NS_SKY_SHADER(// Root Engine Sky sensor optics. Added to HDR *after* opaque geometry, so
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
)NS_SKY_SHADER";
// sun_optics.glsl SHA-256 e5998d04d1bf1fa6ef07071a8184ea498469174ab5ab977790ba629592735dd1
inline constexpr char sky_sun_optics_glsl[] = R"NS_SKY_SHADER(// Root Engine Sky sensor optics for Vulkan HDR compositor.
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
)NS_SKY_SHADER";
} // namespace northstarcpp::shaders
