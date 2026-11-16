#include "viewport_skin_cache.hpp"
#include "../shared/derived_data_file.hpp"
#include "../shared/frame_memory_counters.hpp"
#include <blake3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace northstarcpp::viewport_detail {
namespace {
// Bump for changes to the importer, section layout, or arm selection rules.
constexpr std::string_view recipe="NorthStarCpp/YDD/GpuSkin/1";
constexpr std::uint64_t max_cache_payload=128ull*1024*1024;

std::string cache_key(SkinSectionCacheRequest const& request)
{
    static_assert(sizeof(MeshVertex)==8*sizeof(float) &&
        std::is_trivially_copyable_v<MeshVertex>);
    static_assert(sizeof(SkinWeights)==8*(sizeof(std::uint16_t)+sizeof(float)) &&
        std::is_trivially_copyable_v<SkinWeights>);
    static_assert(sizeof(NorthStarSkinMaterialVertex)==80);
    blake3_hasher state{};
    blake3_hasher_init(&state);
    auto add=[&](auto const* data,std::size_t count) {
        const std::uint64_t length=count;
        blake3_hasher_update(&state,&length,sizeof(length));
        if(count)blake3_hasher_update(&state,data,count*sizeof(*data));
    };
    add(recipe.data(),recipe.size());
    add(request.provider.data(),request.provider.size());
    add(request.provider_version.data(),request.provider_version.size());
    const std::uint32_t abi=NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI;
    add(&abi,1);
    add(request.mesh.vertices.data(),request.mesh.vertices.size());
    add(request.mesh.indices.data(),request.mesh.indices.size());
    add(request.weights.data(),request.weights.size());
    const bool arms=request.arm_mask!=nullptr;
    add(&arms,1);
    if(arms)add(request.arm_mask->data(),request.arm_mask->size());
    const std::uint64_t section_count=request.mesh.sections.size();
    add(&section_count,1);
    for(auto const& section:request.mesh.sections) {
        const std::uint64_t range[]{section.first_index,section.index_count};
        add(range,2);
    }
    std::array<std::uint8_t,32> digest{};
    blake3_hasher_finalize(&state,digest.data(),digest.size());
    std::string key(2*digest.size(),'0');
    const char alphabet[]="0123456789abcdef";
    for(std::size_t i=0;i<digest.size();++i) {
        key[i*2]=alphabet[digest[i]>>4];
        key[i*2+1]=alphabet[digest[i]&15];
    }
    return key;
}

std::optional<PreparedSkinSections> decode(
    std::vector<std::uint8_t> const& bytes,SkinSectionCacheRequest const& request)
{
    PreparedSkinSections output(request.mesh.sections.size());
    std::size_t offset=0;
    for(std::size_t i=0;i<output.size();++i) {
        std::uint32_t count=0;
        if(bytes.size()-offset<sizeof(count))return {};
        std::memcpy(&count,bytes.data()+offset,sizeof(count));
        offset+=sizeof(count);
        const std::uint64_t length=std::uint64_t(count)*sizeof(NorthStarSkinMaterialVertex);
        const auto expected=request.mesh.sections[i].index_count;
        if(count%3 || count>expected || (!request.arm_mask && count!=expected) ||
           length>bytes.size()-offset)
            return {};
        diagnostics::frame_memory::resize(output[i],count);
        if(length)std::memcpy(output[i].data(),bytes.data()+offset,static_cast<std::size_t>(length));
        offset+=static_cast<std::size_t>(length);
    }
    if(offset!=bytes.size())return {};
    return output;
}

PreparedSkinSections build(SkinSectionCacheRequest const& request)
{
    PreparedSkinSections output;
    diagnostics::frame_memory::reserve(output,request.mesh.sections.size());
    for(auto const& section:request.mesh.sections) {
        auto& vertices=output.emplace_back();
        if(!request.arm_mask)diagnostics::frame_memory::reserve(vertices,section.index_count);
        const auto end=section.first_index+section.index_count;
        for(auto index=section.first_index;index<end;index+=3) {
            if(request.arm_mask &&
               (!request.arm_mask->at(request.mesh.indices[index]) ||
                !request.arm_mask->at(request.mesh.indices[index+1]) ||
                !request.arm_mask->at(request.mesh.indices[index+2])))
                continue;
            for(std::size_t corner=0;corner<3;++corner) {
                const auto source=request.mesh.indices[index+corner];
                const auto& vertex=request.mesh.vertices.at(source);
                const auto& skin=request.weights.at(source);
                NorthStarSkinMaterialVertex packed{vertex.position.x,vertex.position.y,
                    vertex.position.z,vertex.normal.x,vertex.normal.y,vertex.normal.z,
                    vertex.u,vertex.v,{},{}};
                std::copy(skin.joints.begin(),skin.joints.end(),packed.joints);
                std::copy(skin.weights.begin(),skin.weights.end(),packed.weights);
                diagnostics::frame_memory::track_capacity(vertices,[&] { vertices.push_back(packed); });
            }
        }
    }
    return output;
}

void store(std::filesystem::path const& path,std::string const& key,
           PreparedSkinSections const& sections)
{
    std::uint64_t bytes_required=0;
    for(auto const& section:sections) {
        if(section.size()>std::numeric_limits<std::uint32_t>::max())return;
        bytes_required+=sizeof(std::uint32_t)+std::uint64_t(section.size())*sizeof(NorthStarSkinMaterialVertex);
        if(bytes_required>max_cache_payload)return;
    }
    std::vector<std::uint8_t> bytes;
    diagnostics::frame_memory::resize(bytes,static_cast<std::size_t>(bytes_required));
    std::size_t offset=0;
    for(auto const& section:sections) {
        const auto count=static_cast<std::uint32_t>(section.size());
        const auto size=section.size()*sizeof(NorthStarSkinMaterialVertex);
        std::memcpy(bytes.data()+offset,&count,sizeof(count));
        offset+=sizeof(count);
        if(size)std::memcpy(bytes.data()+offset,section.data(),size);
        offset+=size;
    }
    derived_data_file::write(path,key,bytes.data(),bytes.size(),max_cache_payload);
}
}

std::filesystem::path skin_cache_directory()
{
    wchar_t disabled[2]{};
    if(GetEnvironmentVariableW(L"NORTHSTARCPP_DERIVED_DATA_DISABLE",disabled,2)==1 &&
       disabled[0]==L'1')return {};
    if(auto path=derived_data_file::absolute_environment_path(L"NORTHSTARCPP_DERIVED_DATA_DIR"))
        return *path/"YddGpuSkin"/"v1";
    if(auto path=derived_data_file::absolute_environment_path(L"LOCALAPPDATA"))
        return *path/"NorthStarCpp"/"DerivedData"/"YddGpuSkin"/"v1";
    return {};
}

PreparedSkinResult prepare_skin_sections(SkinSectionCacheRequest const& request,
                                         std::filesystem::path const& cache_dir)
{
    if(request.weights.size()!=request.mesh.vertices.size() ||
       (request.arm_mask && request.arm_mask->size()!=request.mesh.vertices.size()))
        throw std::invalid_argument("Invalid native GPU skin source");
    for(auto const& section:request.mesh.sections)
        if(section.first_index>request.mesh.indices.size() ||
           section.index_count>request.mesh.indices.size()-section.first_index ||
           section.index_count%3)
            throw std::invalid_argument("Invalid scene mesh section range");
    std::string key;
    std::filesystem::path path;
    if(!cache_dir.empty()) {
        try {
            key=cache_key(request);
            path=cache_dir/(key+".bin");
            if(auto bytes=derived_data_file::read(path,key,max_cache_payload))
                if(auto decoded=decode(*bytes,request))return {std::move(*decoded),true};
        }catch(std::exception const&) {
            // Optional cache. The native source stays authoritative.
        }
    }
    auto sections=build(request);
    if(!path.empty()) {
        try { store(path,key,sections); }
        catch(std::exception const&) {}
    }
    return {std::move(sections),false};
}

} // namespace northstarcpp::viewport_detail
