// Dedicated cloud layer history resolve (NOT scene-color TAA).
// Inputs are current cloud RGB + opacity and first-hit depth in kilometres,
// alongside previous resolved cloud RGB/opacity and previous cloud depth.
// This shader must only be dispatched by a cloud-only temporal Render Pass.
cbuffer CloudTemporalConstants : register(b0) {
    float4 current_origin_km;
    float4 current_forward;
    float4 current_right;
    float4 current_up;
    float4 previous_origin_km;
    float4 previous_forward;
    float4 previous_right;
    float4 previous_up;
    float4 current_projection; // tanHalfY, aspect, viewport width,height
    float4 previous_projection;
    float4 temporal_settings; // maxWeight,opacityThreshold,depthThreshold,velocityScale
    float4 temporal_flags; // x history valid
};
Texture2D<float4> cloud_current : register(t0);
Texture2D<float> cloud_depth_current : register(t1);
Texture2D<float4> cloud_previous : register(t2);
Texture2D<float> cloud_depth_previous : register(t3);
SamplerState history_sampler : register(s0);

struct PixelInput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
PixelInput cloud_temporal_vs(uint id : SV_VertexID) {
    PixelInput output;
    float2 uv=float2((id<<1u)&2u,id&2u);
    output.position=float4(uv*float2(2.,-2.)+float2(-1.,1.),0.,1.);
    output.uv=uv;
    return output;
}
float4 cloud_temporal_ps(PixelInput input) : SV_Target {
    int2 extent=int2(current_projection.zw);
    int2 pixel=clamp(int2(input.position.xy),int2(0,0),extent-1);
    float4 now=cloud_current.Load(int3(pixel,0));
    float now_depth=cloud_depth_current.Load(int3(pixel,0));
    if(temporal_flags.x<.5||now_depth<=0.||now.a<=.015)
        return now;
    float2 uv=(float2(pixel)+.5)/current_projection.zw;
    float2 ndc=float2(uv.x*2.-1.,1.-uv.y*2.);
    float3 dir=normalize(current_forward.xyz+
        current_right.xyz*ndc.x*current_projection.y*current_projection.x+
        current_up.xyz*ndc.y*current_projection.x);
    float3 world=current_origin_km.xyz+dir*now_depth;
    float3 delta=world-previous_origin_km.xyz;
    float forward=dot(delta,previous_forward.xyz);
    if(forward<=.001)return now;
    float2 previous_uv=float2(
        .5+.5*dot(delta,previous_right.xyz)/
                 (forward*previous_projection.y*previous_projection.x),
        .5-.5*dot(delta,previous_up.xyz)/
                 (forward*previous_projection.x));
    if(any(previous_uv<=0.)||any(previous_uv>=1.))return now;
    float4 old=cloud_previous.SampleLevel(history_sampler,previous_uv,0);
    float old_depth=cloud_depth_previous.SampleLevel(history_sampler,previous_uv,0);
    if(old_depth<=0.||
       abs(old.a-now.a)>temporal_settings.y ||
       abs(old_depth-now_depth)/max(now_depth,.001)>temporal_settings.z)
        return now;
    float3 mean=0.,squares=0.,minimum=65504.,maximum=0.;
    [unroll]for(int y=-1;y<=1;++y)
    [unroll]for(int x=-1;x<=1;++x) {
        int2 p=clamp(pixel+int2(x,y),int2(0,0),extent-1);
        float3 value=cloud_current.Load(int3(p,0)).rgb;
        mean+=value/9.;
        squares+=value*value/9.;
        minimum=min(minimum,value);maximum=max(maximum,value);
    }
    float3 sigma=sqrt(max(0.,squares-mean*mean));
    float3 lo=max(minimum,mean-1.5*sigma);
    float3 hi=min(maximum,mean+1.5*sigma);
    float3 history_color=clamp(old.rgb,min(lo,hi),max(lo,hi));
    float2 velocity=(previous_uv-uv)*current_projection.zw;
    float w=saturate(temporal_settings.x)/
            (1.+length(velocity)*max(temporal_settings.w,0.));
    return float4(lerp(now.rgb,history_color,w),now.a);
}
