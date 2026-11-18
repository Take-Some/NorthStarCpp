// NorthStarCpp — unified VFS material preview source.
// YMAT and all referenced YTD entries are resolved by engine.assets.
// No independent filesystem read, ad-hoc NEF8 parser or private search path.
#include "editor_asset_material_source.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/editor_asset_catalog.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace northstarcpp::editor {
namespace fs=std::filesystem;

MaterialPreviewSource loadMaterialPreviewSource(
    fs::path const& root,std::string const& reference)
{
    MaterialPreviewSource result;
    auto const at=reference.rfind('@');
    auto const library=reference.substr(0,at);
    auto const selected=at==std::string::npos?std::string{}:
        reference.substr(at+1);
    if(!safe_virtual_path(library) || library.empty() ||
       selected.find_first_of("/\\@:")!=std::string::npos)
        throw std::runtime_error("Unsafe material entry");

    AssetStore assets;
    assets.mount_content_assets(root);
    if(assets.size(library)>64u*1024u*1024u)
        throw std::length_error("Material library exceeds preview size limit");
    auto materials=shared_assets::load_materials(assets,library);
    if(selected.empty() && materials.size()!=1)
        throw std::runtime_error(
            "YMAT is a material library; select a named material entry");
    auto it=selected.empty()?materials.begin():
        std::find_if(materials.begin(),materials.end(),
            [&](auto const& item){return item.name==selected;});
    if(it==materials.end())
        throw std::runtime_error("Material entry not found");
    result.material=std::move(*it);

    std::unordered_map<std::string,shared_assets::Texture> cache;
    std::unordered_set<std::string> libraries;
    for(auto const& binding:result.material->textures) {
        if(result.textures.size()>=16)break;
        auto const separator=binding.reference.rfind('@');
        if(separator==std::string::npos) {
            result.missing.push_back(binding.reference);
            continue;
        }
        auto const container=binding.reference.substr(0,separator);
        if(!safe_virtual_path(container) || container.empty()) {
            result.missing.push_back(binding.reference);
            continue;
        }
        try {
            if(libraries.emplace(container).second && libraries.size()>8)
                throw std::length_error("Texture library limit");
            auto found=cache.find(binding.reference);
            if(found==cache.end()) {
                if(assets.size(container)>64u*1024u*1024u)
                    throw std::length_error("Texture library exceeds preview limit");
                found=cache.emplace(
                    binding.reference,assets.load_texture(binding.reference)).first;
            }
            result.textures.push_back({binding.reference,found->second});
        }catch(std::exception const&) {
            result.missing.push_back(binding.reference);
        }
    }
    return result;
}
} // namespace northstarcpp::editor
