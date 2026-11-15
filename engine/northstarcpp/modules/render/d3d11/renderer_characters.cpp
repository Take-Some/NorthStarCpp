// D3D11 characters methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
void RendererState::ensure_vertex_capacity(std::size_t required){
        // HLOD and streaming reduce this transient GPU vertex allocation.
        // Shrink only after 120 consecutive underutilized frames to avoid
        // buffer thrash when cells load/unload at an LOD boundary.
        if (required>max_vertex_capacity)
            throw std::runtime_error("Render frame exceeds vertex capacity");
        std::size_t next=vertex_buffer_capacity;
        if (required>next) {
            do {
                next=std::min<std::size_t>(max_vertex_capacity,next*2);
            } while (next<required);
            underutilized_frames=0;
        } else if (next>min_vertex_capacity && required*4<next) {
            if (++underutilized_frames>=120) {
                next=min_vertex_capacity;
                while (next<required*2 && next<max_vertex_capacity)
                    next=std::min<std::size_t>(max_vertex_capacity,next*2);
                underutilized_frames=0;
            }
        } else {
            underutilized_frames=0;
        }
        if (next==vertex_buffer_capacity)
            return;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=static_cast<UINT>(next*sizeof(Vertex));
        desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        // Allocate first, then replace atomically. On allocation failure the
        // previous frame buffer remains valid and its COM ownership intact.
        Com<ID3D11Buffer> replacement;
        check(device->CreateBuffer(&desc,nullptr,replacement.put()),
              "Reallocate residency-aware D3D11 vertex buffer");
        vertex_buffer.swap(replacement);
        vertex_buffer_capacity=static_cast<unsigned>(next);
        ++vertex_buffer_reallocations;
    }

void RendererState::set_character(NorthStarCharacterPart const* parts, std::uint32_t count){
        if (!count) {
            character_parts.clear();
            character_indices.reset();
            character_vertices.reset();
            character_vertex_count = 0;
            return;
        }
        if (!parts || count > 128)
            throw std::invalid_argument("Invalid character mesh parts");

        std::vector<CharacterBatch> batches(count);
        std::vector<std::uint32_t> indices;
        std::vector<NorthStarCharacterVertex> model;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto const& part = parts[i];
            if (!part.vertices || !part.indices || !part.vertex_count || !part.index_count ||
                part.index_count % 3 || part.vertex_count > 200000 - model.size() ||
                part.index_count > 1000000 - indices.size() ||
                !(part.alpha_cutoff >= 0.f && part.alpha_cutoff <= 1.f))
                throw std::invalid_argument("Invalid character mesh part");
            auto& batch = batches[i];
            batch.start = static_cast<UINT>(indices.size());
            batch.count = part.index_count;
            batch.cutoff[0] = part.alpha_cutoff;
            for (std::uint32_t j = 0; j < part.index_count; ++j) {
                if (part.indices[j] >= part.vertex_count)
                    throw std::invalid_argument("Character index out of range");
                indices.push_back(static_cast<std::uint32_t>(model.size()) + part.indices[j]);
            }
            model.insert(model.end(), part.vertices, part.vertices + part.vertex_count);
            auto const& image = part.texture;
            DXGI_FORMAT format;
            unsigned block = 4;
            switch (image.format_id) {
            case 1: case 2:
                format = image.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
                break;
            case 103:
                format = image.srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
                block = 16;
                break;
            case 104:
                format = image.srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
                block = 16;
                break;
            default:
                throw std::invalid_argument("Unsupported character texture format");
            }
            if (!image.data || !image.mips || !image.mip_count || image.mip_count > 15)
                throw std::invalid_argument("Invalid character texture");
            auto w = image.mips[0].width;
            auto h = image.mips[0].height;
            if (!w || !h || w > 16384 || h > 16384)
                throw std::invalid_argument("Invalid character texture dimensions");
            std::vector<D3D11_SUBRESOURCE_DATA> data;
            for (unsigned j = 0; j < image.mip_count; ++j) {
                auto const& mip = image.mips[j];
                auto const pitch = (block == 4 ? w : (w + 3) / 4) * block;
                auto const rows = block == 4 ? h : (h + 3) / 4;
                if (mip.width != w || mip.height != h || mip.offset > image.data_bytes ||
                    mip.bytes > image.data_bytes - mip.offset ||
                    std::uint64_t(pitch) * rows != mip.bytes)
                    throw std::invalid_argument("Invalid character texture mip");
                data.push_back({image.data + mip.offset, pitch, mip.bytes});
                w = std::max(1u, w / 2);
                h = std::max(1u, h / 2);
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = image.mips[0].width;
            desc.Height = image.mips[0].height;
            desc.MipLevels = image.mip_count;
            desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Com<ID3D11Texture2D> texture;
            check(device->CreateTexture2D(&desc, data.data(), texture.put()),
                  "Create character albedo texture");
            check(device->CreateShaderResourceView(texture.get(), nullptr, batch.texture.put()),
                  "Create character albedo view");
        }

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(indices[0]));
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{indices.data(),0,0};
        Com<ID3D11Buffer> new_indices, new_vertices;
        check(device->CreateBuffer(&desc, &initial, new_indices.put()), "Create character index buffer");
        desc.ByteWidth = static_cast<UINT>(model.size() * sizeof(model[0]));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        initial.pSysMem = model.data();
        check(device->CreateBuffer(&desc, &initial, new_vertices.put()), "Create character vertex buffer");
        character_indices.swap(new_indices);
        character_vertices.swap(new_vertices);
        character_parts.swap(batches);
        character_vertex_count = static_cast<std::uint32_t>(model.size());
        character_visible = false;
    }

void RendererState::set_character_pose(NorthStarCharacterVertex const* data, std::uint32_t count){
        if (!count) {
            character_visible = false;
            return;
        }
        if (!data || count != character_vertex_count)
            throw std::invalid_argument("Character pose vertex count mismatch");
        D3D11_MAPPED_SUBRESOURCE output{};
        check(context->Map(character_vertices.get(),0,D3D11_MAP_WRITE_DISCARD,0,&output),
              "Upload character pose");
        std::memcpy(output.pData,data,std::size_t(count)*sizeof(*data));
        context->Unmap(character_vertices.get(),0);
        character_visible = true;
    }

void RendererState::draw_character(){
        if (!character_visible || character_parts.empty()) return;
        UINT const stride = sizeof(NorthStarCharacterVertex), offset = 0;
        auto* vertices = character_vertices.get();
        auto* material = character_material.get();
        context->IASetInputLayout(character_layout.get());
        context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        context->IASetIndexBuffer(character_indices.get(),DXGI_FORMAT_R32_UINT,0);
        context->VSSetShader(character_vs.get(),nullptr,0);
        context->PSSetShader(character_ps.get(),nullptr,0);
        context->PSSetConstantBuffers(1,1,&material);
        for (auto const& part : character_parts) {
            auto* albedo = part.texture.get();
            context->PSSetShaderResources(0,1,&albedo);
            context->UpdateSubresource(material,0,nullptr,part.cutoff,0,0);
            context->DrawIndexed(part.count,part.start,0);
        }
    }

}
