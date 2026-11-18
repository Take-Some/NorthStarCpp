// NorthStar Engine Sky (Vulkan) — canonical global physically based
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
