from pathlib import Path
r=Path("engine/northstarcpp/modules/render")
def modify(path,old,new):
 p=r/path;s=p.read_text("utf-8")
 assert s.count(old)==1,(path,old[:75],s.count(old))
 p.write_text(s.replace(old,new,1),"utf-8")
 print("patched",path,old[:45])
modify(Path("shared/material_hlsl.hpp"),
"""    p.world=world.xyz;p.normal=normal;
#else
    p.clip=mul(float4(v.pos,1),view_projection);""",
"""    p.world=world.xyz;p.normal=normal;
#elif defined(NS_GPU_RIGID)
    float4 world=mul(float4(v.pos,1),rigid_world);
    p.clip=mul(world,view_projection);
    p.world=world.xyz;
    p.normal=normalize(mul(v.normal,(float3x3)rigid_world));
#else
    p.clip=mul(float4(v.pos,1),view_projection);""")
modify(Path("d3d12/renderer_device.cpp"),
"""            IID_PPV_ARGS(&material_pipeline)),"Create authored PBR pipeline");""",
"""            IID_PPV_ARGS(&material_pipeline)),"Create authored PBR pipeline");
        // A second vertex shader reads a GPU-resident local-space indexed
        // mesh and transforms it with a 64-byte instance constant.
        auto rigid_vs=compile_material_variant(
            std::string("#define NS_GPU_RIGID 1\\n")+authored_material_shader,
            "material_vs","vs_5_0");
        auto rigid_pso=material_pso;
        rigid_pso.VS={rigid_vs->GetBufferPointer(),rigid_vs->GetBufferSize()};
        check(device->CreateGraphicsPipelineState(&rigid_pso,
            IID_PPV_ARGS(&rigid_material_pipeline)),
            "Create indexed rigid PBR pipeline");
        rigid_pso.NumRenderTargets=0;
        rigid_pso.PS={nullptr,0};
        rigid_pso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
        rigid_pso.RasterizerState.DepthBias=1200;
        rigid_pso.RasterizerState.SlopeScaledDepthBias=1.5f;
        check(device->CreateGraphicsPipelineState(&rigid_pso,
            IID_PPV_ARGS(&rigid_shadow_pipeline)),
            "Create indexed rigid shadow pipeline");""")
modify(Path("d3d12/renderer_frame.cpp"),
"""            hud_tri_start>grid || !material_draws.empty();""",
"""            hud_tri_start>grid || !material_draws.empty() ||
            !rigid_draws.empty();""")
modify(Path("d3d12/renderer_pass_gbuffer.cpp"),
"""        }
}
} // namespace northstarcpp::render::d3d12::detail""",
"""        }
        // Index buffers are immutable across frames; only the small world
        // matrices/material constants are refreshed. Same HDR/GBuf target.
        if(!rigid_draws.empty()) {
            std::memcpy(material_camera_data,&matrix,sizeof(matrix));
            std::memcpy(material_camera_data+sizeof(matrix),eye_data,sizeof(eye_data));
            record_rigid_opaque();
        }
}
} // namespace northstarcpp::render::d3d12::detail""")
modify(Path("d3d12/renderer_pass_shadow.cpp"),
"""                if(!material_draws.empty()) {
                    commands->SetPipelineState(shadow_material_pso.Get());
                    commands->IASetVertexBuffers(0,1,&material_vertex_view);
                    for(auto const& draw:material_draws) {
                        if(draw.blend_mode==2)continue;
                        commands->DrawInstanced(draw.count,1,draw.first,0);
                    }
                }""",
"""                if(!material_draws.empty()) {
                    commands->SetPipelineState(shadow_material_pso.Get());
                    commands->IASetVertexBuffers(0,1,&material_vertex_view);
                    for(auto const& draw:material_draws) {
                        if(draw.blend_mode==2)continue;
                        commands->DrawInstanced(draw.count,1,draw.first,0);
                    }
                }
                if(!rigid_draws.empty())record_rigid_shadow(cascade);""")
p=Path("engine/northstarcpp/CMakeLists.txt")
s=p.read_text("utf-8")
old="    modules/render/d3d12/renderer_materials.cpp"
assert s.count(old)==1
p.write_text(s.replace(old,old+"\n    modules/render/d3d12/renderer_rigid_instances.cpp",1),"utf-8")
print("D3D12 indexed rigid mesh translation unit registered")
