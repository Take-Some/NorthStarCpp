// NorthStar native D3D12 deferred-lighting resolve.
// RAGE/ZF-inspired MRT separation, but original shader implementation for
// the current engine materials and stable plugin ABI.
#pragma once
namespace northstarcpp::shaders {
inline constexpr char deferred_light_shader[] = R"(
cbuffer LightingCamera : register(b0) {
    row_major float4x4 inverse_vp;
    float4 camera_world;
    float4 viewport_size; // width, height, reciprocal width, reciprocal height
    uint4 tile_info;      // columns, rows, light_count, tile_size (32)
    float4 ao_info;       // enable, world radius, intensity, surface bias
    float4 camera_forward_csm; // camera forward xyz + cascade count
    row_major float4x4 sun_shadow_vp[3];
    float4 shadow_info;   // enabled, texel size, depth bias, PCSS soft filter
    float4 shadow_splits; // far distance of cascades 0,1,2, depth blend fraction
    float4 hdr_settings; // hdr enabled, reserved...
    float4 cascade_depth_bias; // per-cascade normalized depth bias
};
Texture2D<float4> g_albedo : register(t0);    // linear RGB + coverage alpha
Texture2D<float4> g_normal : register(t1);    // world normal, signed roughness
Texture2D<float4> g_surface : register(t2);   // linear emissive RGB, metallic
Texture2D<float> g_depth : register(t3);      // typeless D32 / R32_FLOAT SRV
struct DynamicLight {
    float4 position_range;
    float4 color_intensity;
    float4 direction_cone; // cos(theta) < 0 means point light
};
StructuredBuffer<DynamicLight> dynamic_lights : register(t4);
Buffer<uint> tiled_indices : register(t5);
Texture2DArray<float> sun_shadow_depth : register(t6);
struct FSOut { float4 position : SV_Position; };
FSOut deferred_vs(uint vertex_id : SV_VertexID) {
    FSOut result;
    float2 tex = float2((vertex_id << 1) & 2, vertex_id & 2);
    result.position = float4(tex * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}
float3 encode_srgb(float3 c) {
    c = saturate(c);
    return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055,
                step(0.0031308, c));
}
float sample_sun_cascade(float3 world,uint cascade) {
    float4 clip=mul(float4(world,1),sun_shadow_vp[cascade]);
    float3 shadow_pos=clip.xyz/max(abs(clip.w),1.e-6);
    float2 uv=shadow_pos.xy*float2(.5,-.5)+float2(.5,.5);
    if(any(uv<0)||any(uv>1)||shadow_pos.z<=0||shadow_pos.z>=1)
        return 1.0;
    int texel_count=int(1.0/shadow_info.y);
    int2 coord=clamp(int2(uv*texel_count),0,texel_count-1);
    float receiver=shadow_pos.z-cascade_depth_bias[cascade];
    float radius=1;
    if(shadow_info.w>0.5) {
        static const int2 taps[8]={
            int2(-3,-1),int2(2,3),int2(-1,3),int2(3,-2),
            int2(-4,2),int2(4,1),int2(-2,-4),int2(1,4)};
        float blocker_sum=0,blocker_count=0;
        [unroll]for(int k=0;k<8;++k) {
            int2 xy=clamp(coord+taps[k],0,texel_count-1);
            float depth=sun_shadow_depth.Load(int4(xy,cascade,0));
            if(depth<receiver) {blocker_sum+=depth;blocker_count+=1;}
        }
        if(blocker_count>0)
            radius=clamp(1+(receiver-blocker_sum/blocker_count)*65,1,7);
    }
    float visibility=0;
    [unroll]for(int y=-1;y<=1;++y)
      [unroll]for(int x=-1;x<=1;++x) {
        int2 xy=clamp(coord+int2(round(float2(x,y)*radius)),0,texel_count-1);
        visibility+=(receiver<=sun_shadow_depth.Load(int4(xy,cascade,0)))?1:0;
      }
    return visibility/9;
}
float4 deferred_ps(FSOut input) : SV_Target {
    int2 pixel = int2(input.position.xy);
    float4 base = g_albedo.Load(int3(pixel, 0));
    // The G-buffer is transparent where the material pass did not rasterize.
    clip(base.a - 0.5);
    float4 normal_rough = g_normal.Load(int3(pixel, 0));
    float4 emission_metal = g_surface.Load(int3(pixel, 0));
    float3 albedo = base.rgb;
    float3 emissive = emission_metal.rgb;
    if (normal_rough.a < 0)
        return float4(hdr_settings.x>0.5 ? albedo+emissive :
                      encode_srgb(albedo+emissive),1);
    float rough = clamp(normal_rough.a, 0.04, 1);
    float metal = saturate(emission_metal.a);
    float3 normal = normalize(normal_rough.xyz);
    float depth = g_depth.Load(int3(pixel, 0));
    float2 clipxy = float2(input.position.x * viewport_size.z * 2 - 1,
                         1 - input.position.y * viewport_size.w * 2);
    float4 world_h = mul(float4(clipxy, depth, 1), inverse_vp);
    float3 world = world_h.xyz / max(abs(world_h.w), 1.e-6) *
                   (world_h.w < 0 ? -1 : 1);
    float3 light = normalize(float3(0.45, 0.82, 0.30));
    float3 view = normalize(camera_world.xyz - world);
    float3 half_v = normalize(light + view);
    float NoL = saturate(dot(normal, light));
    float NoV = max(0.001, saturate(dot(normal, view)));
    float NoH = saturate(dot(normal, half_v));
    float VoH = saturate(dot(view, half_v));
    float a = rough * rough;
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1) + 1;
    float distribution = a2 / max(3.14159265 * d * d, 1.e-5);
    float k = (rough + 1) * (rough + 1) * 0.125;
    float geometry = (NoV / (NoV * (1-k) + k)) * (NoL / (NoL * (1-k) + k));
    float3 f0 = lerp(float3(.04, .04, .04), albedo, metal);
    float3 fresnel = f0 + (1-f0) * pow(1-VoH, 5);
    float3 spec = (distribution * geometry * fresnel) / max(4*NoV*NoL, 0.001);
    float3 diffuse = (1-fresnel) * (1-metal) * albedo / 3.14159265;
    // Stable CSM: linear/logarithmic split blend with small boundary fading.
    // The directional shadow field is evaluated only for in-range receivers.
    float sun_visibility=1.0;
    float camera_distance=dot(world-camera_world.xyz,camera_forward_csm.xyz);
    if(shadow_info.x>0.5 && camera_distance>=0 &&
       camera_distance<=shadow_splits.z) {
        uint cascade=camera_distance<shadow_splits.x?0:
                     (camera_distance<shadow_splits.y?1:2);
        float visibility=sample_sun_cascade(world,cascade);
        if(cascade<2) {
            float end_depth=cascade==0?shadow_splits.x:shadow_splits.y;
            float start_depth=cascade==0?0:shadow_splits.x;
            float width=max(0.01,(end_depth-start_depth)*shadow_splits.w);
            float blend=saturate((camera_distance-(end_depth-width))/width);
            if(blend>0)
                visibility=lerp(visibility,sample_sun_cascade(world,cascade+1),blend);
        }
        sun_visibility=visibility;
    }
    // Opt-in, low-cost view-space ambient occlusion prototype:
    // eight depth samples reconstructed in world space. Applied to ambient
    // only, never to direct light or emissive. A separate denoised SSAO
    // render pass remains a later optimization.
    float ao=1.0;
    if (ao_info.x > 0.5) {
        static const int2 taps[8]={
            int2(-3,-1),int2(3,1),int2(-1,3),int2(1,-3),
            int2(-5,-4),int2(5,4),int2(-4,5),int2(4,-5)};
        float occlusion=0;
        [unroll] for(int i=0;i<8;++i) {
            int2 coord=clamp(pixel+taps[i],int2(0,0),
                             int2(viewport_size.xy)-int2(1,1));
            float neighbor_depth=g_depth.Load(int3(coord,0));
            if(neighbor_depth>=0.99999)continue;
            float2 neighbor_ndc=float2((coord.x+0.5)*viewport_size.z*2-1,
                                       1-(coord.y+0.5)*viewport_size.w*2);
            float4 h=mul(float4(neighbor_ndc,neighbor_depth,1),inverse_vp);
            float3 nearby=h.xyz/max(abs(h.w),1.e-6)*
                          (h.w<0?-1:1);
            float3 delta=nearby-world;
            float distance_to_sample=length(delta);
            if(distance_to_sample>1.e-5 && distance_to_sample<ao_info.y) {
                float angular=saturate(dot(normal,delta/distance_to_sample)
                                       -ao_info.w);
                occlusion+=angular*saturate(1-distance_to_sample/ao_info.y);
            }
        }
        ao=saturate(1-occlusion*ao_info.z*0.125);
    }
    float3 lit = (diffuse + spec) * NoL * (3.0*sun_visibility) +
        albedo * (.16*ao) + emissive;
    // Screen-space tiled forward+ style light list, evaluated in the
    // deferred resolve. Indices were conservatively binned on the CPU.
    // An overflow tile evaluates all sources rather than losing lights.
    if (tile_info.z != 0) {
        uint2 tile = min(uint2(input.position.xy) / tile_info.w, tile_info.xy-1);
        uint tile_start = (tile.y * tile_info.x + tile.x) * 65;
        uint candidate_count = tiled_indices.Load(tile_start);
        bool fallback = candidate_count == 0xffffffffu;
        uint candidate_end = fallback ? tile_info.z : min(candidate_count, 64u);
        [loop] for (uint n=0; n<candidate_end; ++n) {
            uint light_index = fallback ? n : tiled_indices.Load(tile_start + 1 + n);
            if (light_index >= tile_info.z) continue;
            DynamicLight source = dynamic_lights[light_index];
            float3 to_light = source.position_range.xyz-world;
            float dist2 = dot(to_light,to_light);
            float radius = source.position_range.w;
            if (dist2 >= radius*radius || radius <= 0) continue;
            float inv_dist = rsqrt(max(dist2, 1.e-6));
            float3 local_light = to_light*inv_dist;
            float local_NoL = saturate(dot(normal,local_light));
            if (local_NoL <= 0) continue;
            float attenuation = saturate(1-sqrt(dist2)/radius);
            attenuation *= attenuation;
            if (source.direction_cone.w >= 0) {
                float3 spot_axis=normalize(source.direction_cone.xyz);
                float spot_angle=dot(spot_axis,-local_light);
                attenuation *= smoothstep(source.direction_cone.w,
                    min(1.0,source.direction_cone.w+0.08),spot_angle);
            }
            if (attenuation <= 0) continue;
            float3 local_half = normalize(local_light+view);
            float local_NoH = saturate(dot(normal,local_half));
            float local_VoH = saturate(dot(view,local_half));
            float local_denom=local_NoH*local_NoH*(a2-1)+1;
            float local_distribution=a2/max(3.14159265*local_denom*local_denom,1.e-5);
            float local_geometry=(NoV/(NoV*(1-k)+k))*
                (local_NoL/(local_NoL*(1-k)+k));
            float3 local_fresnel=f0+(1-f0)*pow(1-local_VoH,5);
            float3 local_spec=(local_distribution*local_geometry*local_fresnel)/
                max(4*NoV*local_NoL,0.001);
            float3 local_diffuse=(1-local_fresnel)*(1-metal)*albedo/3.14159265;
            lit += (local_diffuse+local_spec)*local_NoL*attenuation*
                source.color_intensity.rgb*source.color_intensity.a;
        }
    }
    return float4(hdr_settings.x>0.5 ? max(lit,0) : encode_srgb(lit),1);
}
)";
}
