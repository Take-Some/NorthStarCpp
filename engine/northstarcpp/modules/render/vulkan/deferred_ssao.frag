#version 450
// Independent half-resolution ambient occlusion pass. AO is stored with the
// non-linear depth for bilateral rejection in the deferred lighting resolve.
layout(set=0,binding=1) uniform sampler2D g_normal;
layout(set=0,binding=3) uniform sampler2D g_depth;
layout(push_constant) uniform Push {
    vec4 sky,mode,camera,projection,viewport,ao,film,film_extra;
} params;
layout(location=0) out vec2 ao_depth;
vec3 reconstruct_world(float depth,vec2 pixel) {
    float near_clip=params.projection.y,far_clip=params.projection.z;
    float distance=near_clip*far_clip/max(1e-5,
        far_clip-depth*(far_clip-near_clip));
    vec2 ndc=(pixel*params.viewport.zw)*2.-1.;
    float aspect=params.viewport.x/max(params.viewport.y,1.);
    float horizontal=ndc.x*distance*aspect/params.projection.w;
    float vertical=-ndc.y*distance/params.projection.w;
    float pitch=params.projection.x;
    float forward=distance*cos(pitch)-vertical*sin(pitch);
    float y=vertical*cos(pitch)+distance*sin(pitch);
    float yaw=params.camera.w;
    return params.camera.xyz+vec3(
        horizontal*cos(yaw)+forward*sin(yaw),y,
        -horizontal*sin(yaw)+forward*cos(yaw));
}
void main() {
    ivec2 full_size=ivec2(params.viewport.xy);
    // RenderGraph allocates a ceil(width/2) x ceil(height/2) target.
    ivec2 center=min(ivec2(gl_FragCoord.xy)*2+ivec2(1),full_size-1);
    float depth=texelFetch(g_depth,center,0).r;
    vec4 nr=texelFetch(g_normal,center,0);
    if(depth>=.99999 || nr.a<0.) {
        ao_depth=vec2(1.,depth);return;
    }
    vec3 normal=normalize(nr.xyz);
    vec3 world=reconstruct_world(depth,vec2(center)+vec2(.5));
    const ivec2 offsets[8]=ivec2[8](
        ivec2(-3,-1),ivec2(3,1),ivec2(-1,3),ivec2(1,-3),
        ivec2(-5,-4),ivec2(5,4),ivec2(-4,5),ivec2(4,-5));
    float occluded=0.;
    for(int i=0;i<8;++i) {
        ivec2 pos=clamp(center+offsets[i]*2,ivec2(0),full_size-1);
        float sample_depth=texelFetch(g_depth,pos,0).r;
        if(sample_depth>=.99999)continue;
        vec3 delta=reconstruct_world(sample_depth,vec2(pos)+vec2(.5))-world;
        float d=length(delta);
        if(d>1e-5 && d<params.ao.y) {
            float facing=max(0.,dot(normal,delta/d)-params.ao.w);
            occluded+=facing*(1.-d/params.ao.y);
        }
    }
    ao_depth=vec2(clamp(1.-occluded*params.ao.z*.125,0.,1.),depth);
}
