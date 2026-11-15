// Device subsystem: independent D3D12 implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
RendererState::RendererState(HWND window,bool software):hwnd(window){
        if(!IsWindow(hwnd)) throw std::runtime_error("Invalid D3D12 HWND");
        if(software)sun_shadow_resolution=256;
        // Set NORTHSTARCPP_DISABLE_FXAA=1 for pixel-perfect output / A-B QA.
        char disable_fxaa[4]{};
        fxaa_enabled=!(GetEnvironmentVariableA("NORTHSTARCPP_DISABLE_FXAA",
            disable_fxaa,sizeof(disable_fxaa))>0 && disable_fxaa[0]=='1');
        char enable_bloom[8]{};
        bloom_enabled=GetEnvironmentVariableA("NORTHSTARCPP_ENABLE_BLOOM",
            enable_bloom,sizeof(enable_bloom))>0 && enable_bloom[0]=='1';
        char enable_hdr[8]{};
        hdr_enabled=GetEnvironmentVariableA("NORTHSTARCPP_ENABLE_HDR",
            enable_hdr,sizeof(enable_hdr))>0 && enable_hdr[0]=='1';
        char enable_aliasing[8]{};
        transient_aliasing_enabled=bloom_enabled &&
            GetEnvironmentVariableA("NORTHSTARCPP_ENABLE_TRANSIENT_ALIASING",
                enable_aliasing,sizeof(enable_aliasing))>0 &&
            enable_aliasing[0]=='1';
        char enable_shadows[8]{};
        shadows_enabled=GetEnvironmentVariableA("NORTHSTARCPP_ENABLE_SHADOWS",
            enable_shadows,sizeof(enable_shadows))>0&&enable_shadows[0]=='1';
        char enable_pcss[8]{};
        pcss_enabled=shadows_enabled &&
            GetEnvironmentVariableA("NORTHSTARCPP_ENABLE_PCSS",
                enable_pcss,sizeof(enable_pcss))>0 && enable_pcss[0]=='1';
        // Experimental eight-tap ambient depth occlusion; opt in for QA.
        char enable_ssao[4]{};
        ssao_enabled=GetEnvironmentVariableA("NORTHSTARCPP_ENABLE_SSAO",
            enable_ssao,sizeof(enable_ssao))>0 &&
            enable_ssao[0]=='1';
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create DXGI");
        if(software) {
            ComPtr<IDXGIAdapter> warp;
            check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"Get WARP adapter");
            check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device)),"Create WARP D3D12");
        } else {
            for(UINT i=0;;++i) {
                ComPtr<IDXGIAdapter1> adapter;
                if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 info{};adapter->GetDesc1(&info);
                if(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
                if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device))))break;
            }
            if(!device)throw std::runtime_error("D3D12 hardware unavailable");
        }
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue)),"Create queue");
        // Optional GPU timings do not block backend initialization if the
        // adapter cannot expose timestamp queries. This is intentionally
        // separate from the CPU+Present/vsync stress measurements.
        if(SUCCEEDED(queue->GetTimestampFrequency(&timestamp_frequency)) &&
            timestamp_frequency!=0) {
            D3D12_QUERY_HEAP_DESC desc{};
            desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            desc.Count=timestamp_count;
            if(SUCCEEDED(device->CreateQueryHeap(&desc,
                IID_PPV_ARGS(&timestamp_heap)))) {
                auto readback=heap(D3D12_HEAP_TYPE_READBACK);
                auto bytes=memory(timestamp_count*sizeof(UINT64));
                if(FAILED(device->CreateCommittedResource(&readback,
                    D3D12_HEAP_FLAG_NONE,&bytes,D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,IID_PPV_ARGS(&timestamp_readback))))
                    timestamp_heap.Reset();
            }
        }
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)),"Create allocator");
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create commands");
        check(commands->Close(),"Close initial commands");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event)throw std::runtime_error("Create D3D12 fence event");
        D3D12_DESCRIPTOR_HEAP_DESC dh{};
        dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;dh.NumDescriptors=buffer_count;
        check(device->CreateDescriptorHeap(&dh,IID_PPV_ARGS(&rtvs)),"Create RTV heap");
        stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;dh.NumDescriptors=1;
        check(device->CreateDescriptorHeap(&dh,IID_PPV_ARGS(&dsvs)),"Create DSV heap");
        D3D12_ROOT_PARAMETER root_param{};
        root_param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_param.Descriptor.ShaderRegister=0;
        root_param.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC root{};
        root.NumParameters=1;root.pParameters=&root_param;
        root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> binary,error;
        check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,
            &binary,&error),"Serialize root signature");
        check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),
            IID_PPV_ARGS(&signature)),"Create root signature");
        auto vs=compile("vs"),ps=compile("ps");
        D3D12_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,x),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,r),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature=signature.Get();
        pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        pso.InputLayout={elements,2};pso.SampleMask=UINT_MAX;pso.SampleDesc.Count=1;
        pso.NumRenderTargets=1;pso.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
        pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable=TRUE;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.DepthStencilState.DepthEnable=TRUE;
        pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&triangles)),
            "Create D3D12 triangles PSO");
        auto gbuffer_ps=compile("ps_gbuffer");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC geometry_pso=pso;
        geometry_pso.PS={gbuffer_ps->GetBufferPointer(),gbuffer_ps->GetBufferSize()};
        geometry_pso.NumRenderTargets=3;
        geometry_pso.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        geometry_pso.RTVFormats[1]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        geometry_pso.RTVFormats[2]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        for(UINT i=0;i<3;++i)
            geometry_pso.BlendState.RenderTarget[i].RenderTargetWriteMask=
                D3D12_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateGraphicsPipelineState(&geometry_pso,
            IID_PPV_ARGS(&gbuffer_triangles)),
            "Create deferred opaque geometry pipeline");
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&lines)),
            "Create D3D12 lines PSO");
        if(hdr_enabled) {
            auto hdr_pso=pso;
            hdr_pso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            check(device->CreateGraphicsPipelineState(&hdr_pso,
                IID_PPV_ARGS(&hdr_lines)),"Create HDR debug-line PSO");
        }
        pso.DepthStencilState.DepthEnable=FALSE;
        pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&hud_triangles_pso)),
            "Create HUD triangle PSO");
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&hud_lines_pso)),
            "Create HUD line PSO");
        // First-class authored material root: camera b0, parameters b1,
        // five independent UV textures at t0..t4 with a static wrap sampler.
        D3D12_ROOT_PARAMETER mat_params[3]{};
        mat_params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        mat_params[0].Descriptor.ShaderRegister=0;
        mat_params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        mat_params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        mat_params[1].Descriptor.ShaderRegister=1;
        mat_params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE mat_range{};
        mat_range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        mat_range.NumDescriptors=5;
        mat_range.BaseShaderRegister=0;
        mat_params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        mat_params[2].DescriptorTable.NumDescriptorRanges=1;
        mat_params[2].DescriptorTable.pDescriptorRanges=&mat_range;
        mat_params[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC mat_sampler{};
        mat_sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        mat_sampler.AddressU=mat_sampler.AddressV=mat_sampler.AddressW=
            D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        mat_sampler.MaxLOD=D3D12_FLOAT32_MAX;
        mat_sampler.MaxAnisotropy=1;
        mat_sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        mat_sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC material_root{};
        material_root.NumParameters=3;
        material_root.pParameters=mat_params;
        material_root.NumStaticSamplers=1;
        material_root.pStaticSamplers=&mat_sampler;
        material_root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        binary.Reset();error.Reset();
        check(D3D12SerializeRootSignature(&material_root,D3D_ROOT_SIGNATURE_VERSION_1,
            &binary,&error),"Serialize authored material root");
        check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),
            IID_PPV_ARGS(&material_signature)),"Create authored material root signature");
        auto mat_vs=compile_authored_material("material_vs","vs_5_0");
        auto mat_ps=compile_authored_material("material_gbuffer_ps","ps_5_0");
        D3D12_INPUT_ELEMENT_DESC material_elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(NorthStarMaterialVertex,x),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(NorthStarMaterialVertex,nx),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(NorthStarMaterialVertex,u),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC material_pso=pso;
        material_pso.pRootSignature=material_signature.Get();
        material_pso.VS={mat_vs->GetBufferPointer(),mat_vs->GetBufferSize()};
        material_pso.PS={mat_ps->GetBufferPointer(),mat_ps->GetBufferSize()};
        material_pso.InputLayout={material_elements,3};
        material_pso.DepthStencilState.DepthEnable=TRUE;
        material_pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        material_pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        // Only authored materials are deferred. Existing world/sky/HUD passes
        // remain direct-to-backbuffer and share the depth buffer.
        material_pso.NumRenderTargets=3;
        material_pso.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        material_pso.RTVFormats[1]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        material_pso.RTVFormats[2]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        for(UINT i=0;i<3;++i)
            material_pso.BlendState.RenderTarget[i].RenderTargetWriteMask=
                D3D12_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateGraphicsPipelineState(&material_pso,
            IID_PPV_ARGS(&material_pipeline)),"Create authored PBR pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC material_heap{};
        material_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        material_heap.NumDescriptors=NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES*5;
        material_heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&material_heap,
            IID_PPV_ARGS(&material_srv_heap)),"Create material SRV descriptor heap");
        material_descriptor_increment=device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // Deferred full-screen resolve: one camera CBV and four SRVs
        // (albedo, packed normal/roughness, emissive/metallic and depth).
        D3D12_ROOT_PARAMETER deferred_params[2]{};
        deferred_params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        deferred_params[0].Descriptor.ShaderRegister=0;
        deferred_params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE deferred_range{};
        deferred_range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        deferred_range.NumDescriptors=7;
        deferred_range.BaseShaderRegister=0;
        deferred_params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        deferred_params[1].DescriptorTable.NumDescriptorRanges=1;
        deferred_params[1].DescriptorTable.pDescriptorRanges=&deferred_range;
        deferred_params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC deferred_root{};
        deferred_root.NumParameters=2;
        deferred_root.pParameters=deferred_params;
        deferred_root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        binary.Reset();error.Reset();
        check(D3D12SerializeRootSignature(&deferred_root,D3D_ROOT_SIGNATURE_VERSION_1,
            &binary,&error),"Serialize deferred resolve root");
        check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),
            IID_PPV_ARGS(&deferred_signature)),"Create deferred resolve root");
        auto resolve_vs=compile_deferred("deferred_vs","vs_5_0");
        auto resolve_ps=compile_deferred("deferred_ps","ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC resolve_pso=pso;
        resolve_pso.pRootSignature=deferred_signature.Get();
        resolve_pso.VS={resolve_vs->GetBufferPointer(),resolve_vs->GetBufferSize()};
        resolve_pso.PS={resolve_ps->GetBufferPointer(),resolve_ps->GetBufferSize()};
        resolve_pso.InputLayout={nullptr,0};
        resolve_pso.DepthStencilState.DepthEnable=FALSE;
        resolve_pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        resolve_pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        check(device->CreateGraphicsPipelineState(&resolve_pso,
            IID_PPV_ARGS(&deferred_pipeline)),"Create deferred lighting PSO");
        if(hdr_enabled) {
            resolve_pso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            check(device->CreateGraphicsPipelineState(&resolve_pso,
                IID_PPV_ARGS(&hdr_deferred_pipeline)),"Create HDR deferred lighting PSO");
        }
        D3D12_DESCRIPTOR_HEAP_DESC deferred_heap{};
        deferred_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        deferred_heap.NumDescriptors=3;
        check(device->CreateDescriptorHeap(&deferred_heap,
            IID_PPV_ARGS(&deferred_rtvs)),"Create deferred RTV heap");
        deferred_rtv_stride=device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        deferred_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        deferred_heap.NumDescriptors=7;
        deferred_heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&deferred_heap,
            IID_PPV_ARGS(&deferred_srvs)),"Create deferred SRV heap");
        deferred_srv_stride=device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // First directional shadow region. Its resource is independent of
        // swapchain resize; in the disabled mode t6 is a valid null SRV.
        D3D12_SHADER_RESOURCE_VIEW_DESC sun_shadow_srv{};
        sun_shadow_srv.Format=DXGI_FORMAT_R32_FLOAT;
        sun_shadow_srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sun_shadow_srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sun_shadow_srv.Texture2DArray.MipLevels=1;
        sun_shadow_srv.Texture2DArray.ArraySize=cascade_count;
        auto shadow_srv_handle=deferred_srvs->GetCPUDescriptorHandleForHeapStart();
        shadow_srv_handle.ptr+=UINT64(6)*deferred_srv_stride;
        if(shadows_enabled) {
            auto shadow_vs=compile_shadow("shadow_vs","vs_5_0");
            D3D12_INPUT_ELEMENT_DESC shadow_vertex_element[]{
                {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
            D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow_pso=pso;
            shadow_pso.VS={shadow_vs->GetBufferPointer(),shadow_vs->GetBufferSize()};
            shadow_pso.PS={nullptr,0};
            shadow_pso.InputLayout={shadow_vertex_element,1};
            shadow_pso.NumRenderTargets=0;
            for(auto& format:shadow_pso.RTVFormats)
                format=DXGI_FORMAT_UNKNOWN;
            shadow_pso.BlendState.RenderTarget[0].RenderTargetWriteMask=0;
            shadow_pso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
            shadow_pso.DepthStencilState.DepthEnable=TRUE;
            shadow_pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
            shadow_pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
            shadow_pso.RasterizerState.DepthBias=1000;
            shadow_pso.RasterizerState.SlopeScaledDepthBias=1.2f;
            check(device->CreateGraphicsPipelineState(&shadow_pso,
                IID_PPV_ARGS(&shadow_geometry_pso)),"Create directional shadow PSO");
            // Vertex layout uses only POSITION; material vertex offsets match.
            shadow_material_pso=shadow_geometry_pso;
            D3D12_DESCRIPTOR_HEAP_DESC shadow_heap{};
            shadow_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            shadow_heap.NumDescriptors=cascade_count;
            check(device->CreateDescriptorHeap(&shadow_heap,
                IID_PPV_ARGS(&sun_shadow_dsv_heap)),"Create sun shadow DSV heap");
            D3D12_RESOURCE_DESC shadow_desc{};
            shadow_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            shadow_desc.Width=sun_shadow_resolution;
            shadow_desc.Height=sun_shadow_resolution;
            shadow_desc.DepthOrArraySize=cascade_count;
            shadow_desc.MipLevels=1;
            shadow_desc.Format=DXGI_FORMAT_R32_TYPELESS;
            shadow_desc.SampleDesc.Count=1;
            shadow_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_CLEAR_VALUE shadow_clear{};
            shadow_clear.Format=DXGI_FORMAT_D32_FLOAT;
            shadow_clear.DepthStencil.Depth=1.f;
            auto default_heap=heap(D3D12_HEAP_TYPE_DEFAULT);
            check(device->CreateCommittedResource(&default_heap,D3D12_HEAP_FLAG_NONE,
                &shadow_desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &shadow_clear,IID_PPV_ARGS(&sun_shadow_map)),
                "Create first directional shadow depth map");
            D3D12_DEPTH_STENCIL_VIEW_DESC shadow_dsv{};
            shadow_dsv.Format=DXGI_FORMAT_D32_FLOAT;
            shadow_dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            shadow_dsv.Texture2DArray.ArraySize=1;
            shadow_dsv_stride=device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            auto dsv_handle=sun_shadow_dsv_heap->GetCPUDescriptorHandleForHeapStart();
            for(UINT cascade=0;cascade<cascade_count;++cascade) {
                shadow_dsv.Texture2DArray.FirstArraySlice=cascade;
                device->CreateDepthStencilView(sun_shadow_map.Get(),&shadow_dsv,dsv_handle);
                dsv_handle.ptr+=shadow_dsv_stride;
            }
            device->CreateShaderResourceView(sun_shadow_map.Get(),&sun_shadow_srv,
                shadow_srv_handle);
        } else {
            device->CreateShaderResourceView(nullptr,&sun_shadow_srv,
                shadow_srv_handle);
        }
        if(postfx_enabled()) {
            // Dedicated screen-space AA follows 3D rendering, but precedes HUD.
            D3D12_ROOT_PARAMETER fxaa_params[2]{};
            fxaa_params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
            fxaa_params[0].Descriptor.ShaderRegister=0;
            fxaa_params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_DESCRIPTOR_RANGE fxaa_range{};
            fxaa_range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            fxaa_range.NumDescriptors=3; // scene, bloom, exposure
            fxaa_range.BaseShaderRegister=0;
            fxaa_params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            fxaa_params[1].DescriptorTable.NumDescriptorRanges=1;
            fxaa_params[1].DescriptorTable.pDescriptorRanges=&fxaa_range;
            fxaa_params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_STATIC_SAMPLER_DESC fxaa_sampler{};
            fxaa_sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            fxaa_sampler.AddressU=fxaa_sampler.AddressV=fxaa_sampler.AddressW=
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            fxaa_sampler.MaxLOD=D3D12_FLOAT32_MAX;
            fxaa_sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
            fxaa_sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_ROOT_SIGNATURE_DESC fxaa_root{};
            fxaa_root.NumParameters=2;
            fxaa_root.pParameters=fxaa_params;
            fxaa_root.NumStaticSamplers=1;
            fxaa_root.pStaticSamplers=&fxaa_sampler;
            fxaa_root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            binary.Reset();error.Reset();
            check(D3D12SerializeRootSignature(&fxaa_root,D3D_ROOT_SIGNATURE_VERSION_1,
                &binary,&error),"Serialize FXAA root");
            check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),
                IID_PPV_ARGS(&fxaa_signature)),"Create FXAA root");
            auto fxaa_vs=compile_fxaa("fxaa_vs","vs_5_0");
            auto fxaa_ps=compile_fxaa("fxaa_ps","ps_5_0");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC fxaa_pso=pso;
            fxaa_pso.pRootSignature=fxaa_signature.Get();
            fxaa_pso.VS={fxaa_vs->GetBufferPointer(),fxaa_vs->GetBufferSize()};
            fxaa_pso.PS={fxaa_ps->GetBufferPointer(),fxaa_ps->GetBufferSize()};
            fxaa_pso.InputLayout={nullptr,0};
            fxaa_pso.DepthStencilState.DepthEnable=FALSE;
            fxaa_pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
            fxaa_pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            check(device->CreateGraphicsPipelineState(&fxaa_pso,
                IID_PPV_ARGS(&fxaa_pipeline)),"Create FXAA pipeline");
            if(bloom_enabled && !fxaa_enabled) {
                auto simple_ps=compile_fxaa("bloom_additive_ps","ps_5_0");
                D3D12_GRAPHICS_PIPELINE_STATE_DESC simple_pso=fxaa_pso;
                simple_pso.PS={simple_ps->GetBufferPointer(),simple_ps->GetBufferSize()};
                auto& blend=simple_pso.BlendState.RenderTarget[0];
                blend.BlendEnable=TRUE;
                blend.SrcBlend=D3D12_BLEND_ONE;
                blend.DestBlend=D3D12_BLEND_ONE;
                blend.BlendOp=D3D12_BLEND_OP_ADD;
                blend.SrcBlendAlpha=D3D12_BLEND_ZERO;
                blend.DestBlendAlpha=D3D12_BLEND_ONE;
                blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
                check(device->CreateGraphicsPipelineState(&simple_pso,
                    IID_PPV_ARGS(&bloom_composite_pipeline)),
                    "Create additive bloom overlay pipeline");
            }
            if(bloom_enabled) {
                auto bloom_vs=compile_bloom("bloom_vs","vs_5_0");
                auto bloom_ps=compile_bloom("bloom_ps","ps_5_0");
                D3D12_GRAPHICS_PIPELINE_STATE_DESC bloom_pso=fxaa_pso;
                bloom_pso.VS={bloom_vs->GetBufferPointer(),bloom_vs->GetBufferSize()};
                bloom_pso.PS={bloom_ps->GetBufferPointer(),bloom_ps->GetBufferSize()};
                bloom_pso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
                check(device->CreateGraphicsPipelineState(&bloom_pso,
                    IID_PPV_ARGS(&bloom_pipeline)),"Create quarter-res bloom pipeline");
            }
            D3D12_DESCRIPTOR_HEAP_DESC fxaa_heap{};
            fxaa_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            fxaa_heap.NumDescriptors=1;
            check(device->CreateDescriptorHeap(&fxaa_heap,
                IID_PPV_ARGS(&scene_rtv_heap)),"Create scene color RTV heap");
            if(bloom_enabled)
                check(device->CreateDescriptorHeap(&fxaa_heap,
                    IID_PPV_ARGS(&bloom_rtv_heap)),"Create bloom RTV heap");
            fxaa_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            fxaa_heap.NumDescriptors=3;
            fxaa_heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            check(device->CreateDescriptorHeap(&fxaa_heap,
                IID_PPV_ARGS(&scene_srv_heap)),"Create scene color SRV heap");
            if(hdr_enabled) {
                auto hdr_vs=compile_hdr("hdr_vs","vs_5_0");
                auto exposure_ps=compile_hdr("exposure_ps","ps_5_0");
                auto tonemap_ps=compile_hdr("hdr_tonemap_ps","ps_5_0");
                D3D12_GRAPHICS_PIPELINE_STATE_DESC hdr_pso=fxaa_pso;
                hdr_pso.VS={hdr_vs->GetBufferPointer(),hdr_vs->GetBufferSize()};
                hdr_pso.PS={exposure_ps->GetBufferPointer(),exposure_ps->GetBufferSize()};
                hdr_pso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
                check(device->CreateGraphicsPipelineState(&hdr_pso,
                    IID_PPV_ARGS(&hdr_exposure_pipeline)),
                    "Create HDR log-exposure pipeline");
                hdr_pso.PS={tonemap_ps->GetBufferPointer(),tonemap_ps->GetBufferSize()};
                hdr_pso.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
                check(device->CreateGraphicsPipelineState(&hdr_pso,
                    IID_PPV_ARGS(&hdr_tonemap_pipeline)),
                    "Create ACES SDR tonemapping pipeline");
                D3D12_DESCRIPTOR_HEAP_DESC hdr_heap{};
                hdr_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                hdr_heap.NumDescriptors=1;
                check(device->CreateDescriptorHeap(&hdr_heap,
                    IID_PPV_ARGS(&hdr_rtv_heap)),"Create HDR scene RTV heap");
                hdr_heap.NumDescriptors=2;
                check(device->CreateDescriptorHeap(&hdr_heap,
                    IID_PPV_ARGS(&hdr_exposure_rtv_heap)),"Create HDR exposure RTV heap");
                hdr_exposure_rtv_stride=device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                hdr_heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                hdr_heap.NumDescriptors=3;
                hdr_heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                for(UINT i=0;i<2;++i) {
                    check(device->CreateDescriptorHeap(&hdr_heap,
                        IID_PPV_ARGS(&hdr_srvs[i])),"Create HDR tonemap SRV heap");
                    check(device->CreateDescriptorHeap(&hdr_heap,
                        IID_PPV_ARGS(&hdr_exposure_srvs[i])),"Create HDR metering SRV heap");
                }
                check(device->CreateDescriptorHeap(&hdr_heap,
                    IID_PPV_ARGS(&hdr_bloom_srvs)),"Create HDR bloom SRV heap");
            }
        }
        // The skydome root uses b0 for its camera, b1 for material tint,
        // and a shader-visible SRV table for the authored RGBA8 YTD texture.
        D3D12_ROOT_PARAMETER sky_params[3]{};
        sky_params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        sky_params[0].Descriptor.ShaderRegister=0;
        sky_params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        sky_params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        sky_params[1].Descriptor.ShaderRegister=1;
        sky_params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors=1;range.BaseShaderRegister=0;
        sky_params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        sky_params[2].DescriptorTable.NumDescriptorRanges=1;
        sky_params[2].DescriptorTable.pDescriptorRanges=&range;
        sky_params[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;
        sampler.MaxAnisotropy=1;
        sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC sky_root{};
        sky_root.NumParameters=3;sky_root.pParameters=sky_params;
        sky_root.NumStaticSamplers=1;sky_root.pStaticSamplers=&sampler;
        sky_root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        binary.Reset();error.Reset();
        check(D3D12SerializeRootSignature(&sky_root,D3D_ROOT_SIGNATURE_VERSION_1,
            &binary,&error),"Serialize skydome root signature");
        check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),
            IID_PPV_ARGS(&sky_signature)),"Create skydome root signature");
        auto sky_vs=compile_sky("sky_vertex_main","vs_5_0");
        auto sky_ps=compile_sky("sky_pixel_main","ps_5_0");
        D3D12_INPUT_ELEMENT_DESC sky_elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(SkyVertex,x),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(SkyVertex,u),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC sky_pso=pso;
        sky_pso.pRootSignature=sky_signature.Get();
        sky_pso.VS={sky_vs->GetBufferPointer(),sky_vs->GetBufferSize()};
        sky_pso.PS={sky_ps->GetBufferPointer(),sky_ps->GetBufferSize()};
        sky_pso.InputLayout={sky_elements,2};
        sky_pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        check(device->CreateGraphicsPipelineState(&sky_pso,IID_PPV_ARGS(&sky_pipeline)),
            "Create native D3D12 skydome pipeline");
        if(hdr_enabled) {
            sky_pso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            check(device->CreateGraphicsPipelineState(&sky_pso,
                IID_PPV_ARGS(&hdr_sky_pipeline)),"Create HDR skydome pipeline");
        }
        D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
        srv_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.NumDescriptors=1;srv_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&srv_desc,IID_PPV_ARGS(&sky_srv_heap)),
            "Create skydome SRV heap");
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        auto vertices=memory(UINT64(capacity)*sizeof(Vertex));
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&vertices,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&vertex_buffer)),
            "Create upload vertices");
        auto constants=memory(256);
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&constants,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&constant_buffer)),
            "Create camera constants");
        check(vertex_buffer->Map(0,nullptr,reinterpret_cast<void**>(&vertex_data)),
            "Map vertex upload");
        check(constant_buffer->Map(0,nullptr,reinterpret_cast<void**>(&camera_data)),
            "Map camera upload");
        auto make_constant=[&](ComPtr<ID3D12Resource>& buffer,unsigned char** bytes) {
            check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&constants,
                D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buffer)),
                "Create D3D12 render constants");
            check(buffer->Map(0,nullptr,reinterpret_cast<void**>(bytes)),
                "Map D3D12 render constants");
        };
        make_constant(sky_camera,&sky_camera_data);
        make_constant(sky_tint,&sky_tint_data);
        make_constant(hud_camera,&hud_camera_data);
        // Deferred CSM constants now occupy >256 bytes (3 light matrices).
        // Shadow VS uses a distinct 256-byte aligned CBV per cascade.
        auto make_large_constant=[&](ComPtr<ID3D12Resource>& buffer,
                                    unsigned char** bytes,UINT64 size) {
            auto desc=memory(size);
            check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
                &desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
                IID_PPV_ARGS(&buffer)),"Create CSM constant buffer");
            check(buffer->Map(0,nullptr,reinterpret_cast<void**>(bytes)),
                "Map CSM constant buffer");
        };
        make_large_constant(deferred_constants,&deferred_constant_data,512);
        if(shadows_enabled)
            make_large_constant(sun_shadow_constants,&sun_shadow_constants_data,
                                cascade_count*256ull);
        if(postfx_enabled())make_constant(fxaa_constants,&fxaa_constant_data);
        using namespace northstarcpp::math;
        float4x4 identity{float4(1.f,0.f,0.f,0.f),float4(0.f,1.f,0.f,0.f),
                          float4(0.f,0.f,1.f,0.f),float4(0.f,0.f,0.f,1.f)};
        std::memcpy(hud_camera_data,&identity,sizeof(identity));
        vertex_view={vertex_buffer->GetGPUVirtualAddress(),capacity*sizeof(Vertex),sizeof(Vertex)};
        DXGI_SWAP_CHAIN_DESC1 chain{};
        chain.Format=DXGI_FORMAT_R8G8B8A8_UNORM;chain.SampleDesc.Count=1;
        chain.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;chain.BufferCount=buffer_count;
        chain.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> created;
        check(factory->CreateSwapChainForHwnd(queue.Get(),hwnd,&chain,nullptr,nullptr,
            &created),"Create D3D12 swapchain");
        check(created.As(&swapchain),"Query D3D12 swapchain");
        factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
        RECT rect{};GetClientRect(hwnd,&rect);
        if(rect.right>0&&rect.bottom>0)resize(rect.right,rect.bottom);
    }
} // namespace northstarcpp::render::d3d12::detail
