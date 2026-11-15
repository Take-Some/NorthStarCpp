// Post-tonemap engine.ui compositor for the D3D12 provider.
// C ABI supplies straight-alpha RGBA8; upload staging is reused after the
// frame fence in RendererState::draw(), never shared with other devices.
#include "renderer_private.hpp"
#include <string>

namespace northstarcpp::render::d3d12::detail {
void RendererState::set_ui_rgba(const std::uint8_t* source,
                               UINT w,UINT h,UINT source_stride) {
    if(!source) {
        if(w||h||source_stride)
            throw std::invalid_argument("Invalid UI overlay clear");
        ui_cpu_rgba.clear(); ui_width=ui_height=0;return;
    }
    if(!w||!h||w>8192||h>8192||
       static_cast<UINT64>(w)*h*4>64ull*1024*1024 ||
       source_stride<static_cast<UINT64>(w)*4)
        throw std::invalid_argument("Invalid UI overlay dimensions/stride");
    std::vector<std::uint8_t> bytes(static_cast<size_t>(w)*h*4);
    for(UINT y=0;y<h;++y)
        std::memcpy(bytes.data()+static_cast<size_t>(y)*w*4,
                    source+static_cast<size_t>(y)*source_stride,
                    static_cast<size_t>(w)*4);
    ui_cpu_rgba.swap(bytes);ui_width=w;ui_height=h;
}
void RendererState::record_ui_pass(FramePassContext& context) {
    if(ui_cpu_rgba.empty()||!ui_width||!ui_height)return;
    if(!ui_pso) {
        // A dedicated 2D pipeline keeps UI independent of 3D camera,
        // exposure, depth, shadows and world material variants.
        static constexpr char source[]=R"HLSL(
Texture2D image : register(t0);
SamplerState samplerLinear : register(s0);
struct Out { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
Out vs(uint id : SV_VertexID) {
    Out o;
    o.uv=float2((id<<1)&2,id&2);
    o.pos=float4(o.uv.x*2-1,1-o.uv.y*2,0,1);
    return o;
}
float4 ps(Out input) : SV_TARGET {
    return image.Sample(samplerLinear,input.uv);
}
)HLSL";
        ComPtr<ID3DBlob> vs,ps,error;
        auto makeShader=[&](const char* entry,const char* profile,
                            ComPtr<ID3DBlob>& result) {
            HRESULT hr=D3DCompile(source,sizeof(source)-1,nullptr,
                nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,
                &result,&error);
            if(FAILED(hr)) {
                std::string detail=error?
                    std::string(static_cast<const char*>(error->GetBufferPointer()),
                                error->GetBufferSize()):"unknown compiler error";
                throw std::runtime_error("UI D3D12 shader compile: "+detail);
            }
            error.Reset();
        };
        makeShader("vs","vs_5_0",vs);
        makeShader("ps","ps_5_0",ps);
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors=1;range.BaseShaderRegister=0;
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges=1;
        param.DescriptorTable.pDescriptorRanges=&range;
        param.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister=0;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;
        sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC sig{};
        sig.NumParameters=1;sig.pParameters=&param;
        sig.NumStaticSamplers=1;sig.pStaticSamplers=&sampler;
        sig.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized;
        check(D3D12SerializeRootSignature(&sig,D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,&error),"Serialize UI root signature");
        check(device->CreateRootSignature(0,serialized->GetBufferPointer(),
            serialized->GetBufferSize(),IID_PPV_ARGS(&ui_signature)),
            "Create UI root signature");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature=ui_signature.Get();
        desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        desc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        desc.SampleMask=UINT_MAX;
        desc.SampleDesc.Count=1;
        desc.NumRenderTargets=1;
        desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable=TRUE;
        desc.DepthStencilState.DepthEnable=FALSE;
        desc.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        auto& blend=desc.BlendState.RenderTarget[0];
        blend.BlendEnable=TRUE;
        blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ONE;
        blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateGraphicsPipelineState(&desc,
              IID_PPV_ARGS(&ui_pso)),"Create UI overlay pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC heapdesc{};
        heapdesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapdesc.NumDescriptors=1;
        heapdesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&heapdesc,IID_PPV_ARGS(&ui_srv_heap)),
              "Create UI shader resource descriptor");
    }
    if(!ui_texture||ui_width!=ui_gpu_width||ui_height!=ui_gpu_height) {
        ui_texture.Reset();ui_upload.Reset();
        D3D12_RESOURCE_DESC description{};
        description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width=ui_width;
        description.Height=ui_height;
        description.DepthOrArraySize=1;
        description.MipLevels=1;
        description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count=1;
        description.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        auto props=heap(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,
              &description,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
              IID_PPV_ARGS(&ui_texture)),"Allocate UI GPU texture");
        ui_gpu_width=ui_width;ui_gpu_height=ui_height;
        ui_texture_state=D3D12_RESOURCE_STATE_COPY_DEST;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=description.Format;
        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels=1;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(ui_texture.Get(),&srv,
                                         ui_srv_heap->GetCPUDescriptorHandleForHeapStart());
    }
    const UINT pitch=(ui_width*4u+255u)&~255u;
    const UINT64 bytes=static_cast<UINT64>(pitch)*ui_height;
    if(!ui_upload||ui_upload->GetDesc().Width<bytes) {
        ui_upload.Reset();
        auto props=heap(D3D12_HEAP_TYPE_UPLOAD);
        auto buffer=memory(bytes);
        check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,
            &buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&ui_upload)),"Allocate UI GPU staging buffer");
    }
    void* mapped=nullptr;
    D3D12_RANGE readRange{0,0};
    check(ui_upload->Map(0,&readRange,&mapped),"Map UI upload buffer");
    for(UINT row=0;row<ui_height;++row)
        std::memcpy(static_cast<std::uint8_t*>(mapped)+static_cast<size_t>(row)*pitch,
                    ui_cpu_rgba.data()+static_cast<size_t>(row)*ui_width*4,
                    static_cast<size_t>(ui_width)*4);
    ui_upload->Unmap(0,nullptr);
    auto transition=[&](D3D12_RESOURCE_STATES next) {
        if(ui_texture_state==next)return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=ui_texture.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=ui_texture_state;
        barrier.Transition.StateAfter=next;
        commands->ResourceBarrier(1,&barrier);
        ui_texture_state=next;
    };
    transition(D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource=ui_texture.Get();
    destination.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION staging{};
    staging.pResource=ui_upload.Get();
    staging.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    staging.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    staging.PlacedFootprint.Footprint.Width=ui_width;
    staging.PlacedFootprint.Footprint.Height=ui_height;
    staging.PlacedFootprint.Footprint.Depth=1;
    staging.PlacedFootprint.Footprint.RowPitch=pitch;
    commands->CopyTextureRegion(&destination,0,0,0,&staging,nullptr);
    transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commands->OMSetRenderTargets(1,&context.rtv,FALSE,nullptr);
    commands->RSSetViewports(1,&context.viewport);
    commands->RSSetScissorRects(1,&context.scissors);
    ID3D12DescriptorHeap* heaps[]{ui_srv_heap.Get()};
    commands->SetDescriptorHeaps(1,heaps);
    commands->SetGraphicsRootSignature(ui_signature.Get());
    commands->SetGraphicsRootDescriptorTable(0,
                                              ui_srv_heap->GetGPUDescriptorHandleForHeapStart());
    commands->SetPipelineState(ui_pso.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3,1,0,0);
}
} // namespace northstarcpp::render::d3d12::detail
