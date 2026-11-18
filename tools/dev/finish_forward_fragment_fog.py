#!/usr/bin/env python3
"""One-time precision migration: Vulkan forward extinction evaluated per pixel."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
f=ROOT/"engine/northstarcpp/modules/render/vulkan/renderer_frame.cpp"
s=f.read_text(encoding="utf8")
marker="        if(textured_world)"
start=s.index("void Renderer::record_opaque(")
idx=s.index(marker,start)
end=s.index("    if(!material_draws_.empty()",start)
assert idx<end
code="""        // Forward fog push: camera origin and density, sky-derived haze tint.
        // Both primitive and BCn world pipelines use this 32-byte layout.
        struct ForwardAtmosphere {float camera[4];float haze[4];} fog{};
        static_assert(sizeof(fog)==32);
        fog.camera[0]=frame.camera_x;
        fog.camera[1]=frame.camera_y;
        fog.camera[2]=frame.camera_z;
        fog.camera[3]=height_fog_density_;
        const float daylight=std::clamp(gpu_sky_uniforms_.atmosphere[0],0.f,1.f);
        const float night_haze[3]={.055f,.075f,.11f};
        const float day_haze[3]={.23f,.315f,.395f};
        for(unsigned c=0;c<3;++c)
            fog.haze[c]=night_haze[c]+daylight*(day_haze[c]-night_haze[c]);
        vkCmdPushConstants(cmd,textured_world?world_pipeline_layout_:pipeline_layout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(fog),&fog);
"""
assert "struct ForwardAtmosphere" not in s[start:end]
s=s[:idx]+code+s[idx:]
f.write_text(s,encoding="utf8")
print("PASS Vulkan forward fog push constants")
f=ROOT/"tools/dev/embed_vulkan_forward_fog_shaders.py"
s=f.read_text(encoding="utf8")
line='    patch_array(VK/"shader_spirv.hpp","basic_fragment",'
assert s.count(line)==1
s=s.replace(line,'    patch_array(VK/"shader_spirv.hpp","basic_vertex",\n                compile_one("basic.vert"))\n'+line)
f.write_text(s,encoding="utf8")
print("PASS basic vertex SPIR-V generation")
