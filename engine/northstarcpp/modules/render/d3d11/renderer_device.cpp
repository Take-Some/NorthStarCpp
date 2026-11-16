// D3D11 device methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
RendererState::RendererState(HWND target, bool software) : window(target){
        HRESULT result = E_FAIL;
        if (!software)
            result = create_device(D3D_DRIVER_TYPE_HARDWARE);
        if (FAILED(result))
            result = create_device(D3D_DRIVER_TYPE_WARP);
        check(result, "Create Direct3D 11 device");

        Com<IDXGIDevice> dxgi_device;
        Com<IDXGIAdapter> adapter;
        Com<IDXGIFactory2> factory;
        check(device->QueryInterface(__uuidof(IDXGIDevice),
            reinterpret_cast<void**>(dxgi_device.put())), "Get DXGI device");
        check(dxgi_device->GetAdapter(adapter.put()), "Get DXGI adapter");
        // Size only the immutable texture residency cache to real hardware.
        // No LOD/texture quality is reduced; unused GPU residency is evicted.
        DXGI_ADAPTER_DESC adapter_info{};
        if(SUCCEEDED(adapter->GetDesc(&adapter_info)) &&
           adapter_info.DedicatedVideoMemory) {
            auto const quarter=std::uint64_t(adapter_info.DedicatedVideoMemory)/4;
            material_texture_cache_budget=std::clamp(quarter,
                512ull*1024ull*1024ull,4096ull*1024ull*1024ull);
        }
        check(adapter->GetParent(__uuidof(IDXGIFactory2),
            reinterpret_cast<void**>(factory.put())), "Get DXGI factory");

        DXGI_SWAP_CHAIN_DESC1 chain{};
        chain.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        chain.SampleDesc.Count = 1;
        chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        chain.BufferCount = 2;
        chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        check(factory->CreateSwapChainForHwnd(device.get(), window, &chain, nullptr, nullptr,
            swap_chain.put()), "Create viewport swap chain");
        check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
            "Configure viewport window");

        Com<ID3DBlob> code;
        compile("vertex_main", "vs_4_0", code.put());
        check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, vertex_shader.put()), "Create vertex shader");
        D3D11_INPUT_ELEMENT_DESC const elements[]{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, offsetof(Vertex, surface), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        check(device->CreateInputLayout(elements, 3, code->GetBufferPointer(), code->GetBufferSize(),
            layout.put()), "Create vertex layout");
        compile("pixel_main", "ps_4_0", code.put());
        check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, pixel_shader.put()), "Create pixel shader");

        // No geometry is submitted for the editor grid: a fullscreen
        // triangle reconstructs world-plane coordinates per pixel.
        auto compile_grid=[&](char const* entry,char const* profile) {
            Com<ID3DBlob> binary,errors;
            auto const& source=northstarcpp::shaders::analytic_editor_grid_hlsl;
            auto const hr=D3DCompile(source,sizeof(source)-1,
                "NorthStar Editor Grid",nullptr,nullptr,entry,profile,
                D3DCOMPILE_ENABLE_STRICTNESS,0,binary.put(),errors.put());
            if(FAILED(hr) && errors.get())
                throw std::runtime_error(
                    static_cast<char const*>(errors->GetBufferPointer()));
            check(hr,"Compile D3D11 analytic editor grid");
            if(entry[5]=='v')
                check(device->CreateVertexShader(binary->GetBufferPointer(),
                    binary->GetBufferSize(),nullptr,grid_vs.put()),
                    "Create D3D11 grid vertex shader");
            else {
                auto output=std::strcmp(entry,"grid_temporal_ps")==0?
                    grid_temporal_ps.put():
                    std::strcmp(entry,"grid_composite_ps")==0?
                    grid_composite_ps.put():grid_ps.put();
                check(device->CreatePixelShader(binary->GetBufferPointer(),
                    binary->GetBufferSize(),nullptr,output),
                    "Create D3D11 grid pixel shader");
            }
        };
        compile_grid("grid_vs","vs_4_0");
        compile_grid("grid_ps","ps_4_0");
        compile_grid("grid_temporal_ps","ps_4_0");
        compile_grid("grid_composite_ps","ps_4_0");
        D3D11_BUFFER_DESC grid_buffer{};
        grid_buffer.ByteWidth=sizeof(northstarcpp::render::EditorGridConstants);
        grid_buffer.Usage=D3D11_USAGE_DEFAULT;
        grid_buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&grid_buffer,nullptr,grid_constants.put()),
            "Create D3D11 grid constants");
        check(device->CreateBuffer(&grid_buffer,nullptr,
            grid_previous_buffer.put()),"Create D3D11 previous grid constants");
        D3D11_SAMPLER_DESC grid_sampling{};
        grid_sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        grid_sampling.AddressU=grid_sampling.AddressV=
            grid_sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        grid_sampling.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&grid_sampling,grid_sampler.put()),
            "Create grid history sampler");
        D3D11_BLEND_DESC grid_blend_desc{};
        auto& grid_rt=grid_blend_desc.RenderTarget[0];
        grid_rt.BlendEnable=TRUE;
        grid_rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;
        grid_rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        grid_rt.BlendOp=D3D11_BLEND_OP_ADD;
        grid_rt.SrcBlendAlpha=D3D11_BLEND_ONE;
        grid_rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        grid_rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        grid_rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&grid_blend_desc,grid_blend.put()),
            "Create D3D11 grid alpha blend");
        D3D11_DEPTH_STENCIL_DESC no_grid_depth{};
        no_grid_depth.DepthEnable=FALSE;
        no_grid_depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        check(device->CreateDepthStencilState(&no_grid_depth,
            grid_no_depth.put()),"Create D3D11 grid depth state");

        compile_character("character_vs","vs_4_0",code.put());
        check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),
            nullptr,character_vs.put()),"Create character vertex shader");
        D3D11_INPUT_ELEMENT_DESC const character_elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(NorthStarCharacterVertex,position),D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(NorthStarCharacterVertex,normal),D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(NorthStarCharacterVertex,uv),D3D11_INPUT_PER_VERTEX_DATA,0}
        };
        check(device->CreateInputLayout(character_elements,3,code->GetBufferPointer(),
            code->GetBufferSize(),character_layout.put()),"Create character input layout");
        compile_character("character_ps","ps_4_0",code.put());
        check(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),
            nullptr,character_ps.put()),"Create character pixel shader");
        D3D11_BUFFER_DESC material_buffer{};
        material_buffer.ByteWidth = 16;
        material_buffer.Usage = D3D11_USAGE_DEFAULT;
        material_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&material_buffer,nullptr,character_material.put()),
            "Create character material buffer");

        compile_authored_material("material_vs","vs_4_0",code.put());
        check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),
            nullptr,authored_vs.put()),"Create authored UV vertex shader");
        D3D11_INPUT_ELEMENT_DESC const authored_elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
                offsetof(NorthStarMaterialVertex,x),D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
                offsetof(NorthStarMaterialVertex,nx),D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,
                offsetof(NorthStarMaterialVertex,u),D3D11_INPUT_PER_VERTEX_DATA,0}
        };
        check(device->CreateInputLayout(authored_elements,3,code->GetBufferPointer(),
            code->GetBufferSize(),authored_layout.put()),"Create authored UV layout");
        // A second versioned vertex path deforms the original eight YDD
        // influences on GPU. It shares the exact PBR pixel shader.
        {
            std::string const shader_source=
                std::string("#define NS_GPU_SKINNING 1\n")+
                "#define NS_D3D11_DYNAMIC_LIGHTS 1\n"+
                authored_material_shader;
            Com<ID3DBlob> errors;
            HRESULT const hr=D3DCompile(shader_source.data(),shader_source.size(),
                "NorthStar Native GPU Skin",nullptr,nullptr,
                "material_vs","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,
                code.put(),errors.put());
            if(FAILED(hr)&&errors.get())
                throw std::runtime_error(
                    static_cast<char const*>(errors->GetBufferPointer()));
            check(hr,"Compile native GPU skin vertex shader");
            check(device->CreateVertexShader(code->GetBufferPointer(),
                code->GetBufferSize(),nullptr,skinned_vs.put()),
                "Create native GPU skin VS");
            D3D11_INPUT_ELEMENT_DESC const attributes[]{
                {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
                    offsetof(NorthStarSkinMaterialVertex,x),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
                    offsetof(NorthStarSkinMaterialVertex,nx),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,
                    offsetof(NorthStarSkinMaterialVertex,u),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"BLENDINDICES",0,DXGI_FORMAT_R16G16B16A16_UINT,0,
                    offsetof(NorthStarSkinMaterialVertex,joints),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"BLENDINDICES",1,DXGI_FORMAT_R16G16B16A16_UINT,0,
                    offsetof(NorthStarSkinMaterialVertex,joints)+8,D3D11_INPUT_PER_VERTEX_DATA,0},
                {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,
                    offsetof(NorthStarSkinMaterialVertex,weights),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"BLENDWEIGHT",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,
                    offsetof(NorthStarSkinMaterialVertex,weights)+16,D3D11_INPUT_PER_VERTEX_DATA,0}
            };
            check(device->CreateInputLayout(attributes,7,code->GetBufferPointer(),
                code->GetBufferSize(),skinned_layout.put()),
                "Create native skinning vertex layout");
            D3D11_BUFFER_DESC skin_constants{};
            skin_constants.ByteWidth=NORTHSTARCPP_SKIN_MAX_JOINTS*16u*sizeof(float);
            skin_constants.Usage=D3D11_USAGE_DEFAULT;
            skin_constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            check(device->CreateBuffer(&skin_constants,nullptr,
                skinned_palette_buffer.put()),"Create GPU bone palette");
        }
        compile_authored_material("material_ps","ps_4_0",code.put());
        check(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),
            nullptr,authored_ps.put()),"Create authored PBR shader");
        D3D11_BUFFER_DESC authored_constants{};
        authored_constants.ByteWidth=sizeof(MaterialParams);
        authored_constants.Usage=D3D11_USAGE_DEFAULT;
        authored_constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&authored_constants,nullptr,authored_material.put()),
              "Create material GPU constants");
        D3D11_BUFFER_DESC camera_constants{};
        camera_constants.ByteWidth=sizeof(northstarcpp::math::float4x4)+16;
        camera_constants.Usage=D3D11_USAGE_DEFAULT;
        camera_constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&camera_constants,nullptr,authored_camera.put()),
              "Create authored material camera constants");
        D3D11_BUFFER_DESC lighting{};
        lighting.ByteWidth=16u+sizeof(NorthStarRenderLight)*
            NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS;
        lighting.Usage=D3D11_USAGE_DEFAULT;
        lighting.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&lighting,nullptr,dynamic_light_buffer.put()),
              "Create shared ABI dynamic light constant buffer");
        D3D11_BLEND_DESC blend{};
        auto& rt=blend.RenderTarget[0];
        rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha=D3D11_BLEND_ONE;
        rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&blend,material_alpha_blend.put()),
              "Create material alpha blend state");
        D3D11_DEPTH_STENCIL_DESC transparent{};
        transparent.DepthEnable=TRUE;
        transparent.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        transparent.DepthFunc=D3D11_COMPARISON_LESS;
        check(device->CreateDepthStencilState(&transparent,
            material_transparent_depth.put()),"Create transparent material depth state");
        // Independent unlit sky program: UVs are authored in the YDD asset.
        compile_sky("sky_vertex_main", "vs_4_0", code.put());
        check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, sky_vertex_shader.put()), "Create skydome vertex shader");
        D3D11_INPUT_ELEMENT_DESC const sky_elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(SkyVertex,x),D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(SkyVertex,u),D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        check(device->CreateInputLayout(sky_elements,2,code->GetBufferPointer(),
            code->GetBufferSize(),sky_layout.put()), "Create skydome vertex layout");
        compile_sky("sky_pixel_main", "ps_4_0", code.put());
        check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr,sky_pixel_shader.put()), "Create skydome pixel shader");
        D3D11_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sampler_desc,sky_sampler.put()),
            "Create skydome sampler");
        D3D11_DEPTH_STENCIL_DESC no_depth{};
        no_depth.DepthEnable=FALSE;
        no_depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        no_depth.StencilEnable=FALSE;
        check(device->CreateDepthStencilState(&no_depth,sky_no_depth.put()),
            "Create skydome background depth state");

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Vertex) * min_vertex_capacity;
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&buffer, nullptr, vertex_buffer.put()), "Create vertex buffer");
        vertex_buffer_capacity=min_vertex_capacity;
        buffer.ByteWidth = sizeof(northstarcpp::math::float4x4);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = 0;
        check(device->CreateBuffer(&buffer, nullptr, camera.put()), "Create camera buffer");
        D3D11_BUFFER_DESC tint_buffer{};
        tint_buffer.ByteWidth = sizeof(float) * 4;
        tint_buffer.Usage = D3D11_USAGE_DEFAULT;
        tint_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&tint_buffer,nullptr,sky_tint.put()),
            "Create skydome tint buffer");

        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&raster, rasterizer.put()), "Create rasterizer state");
        vertices.reserve(min_vertex_capacity);
    }

HRESULT RendererState::create_device(D3D_DRIVER_TYPE driver){
        constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0};
        return D3D11CreateDevice(nullptr, driver, nullptr, 0, levels, 3, D3D11_SDK_VERSION,
            device.put(), nullptr, context.put());
    }

}
