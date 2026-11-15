#include "editor_asset_material_source.hpp"
#include <northstarcpp/editor_asset_catalog.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
std::vector<std::uint8_t> readFile(fs::path const& root,std::string const& name) {
    if(!safe_virtual_path(name) || name.empty() ||
       name.find('@')!=std::string::npos)
        throw std::runtime_error("Unsafe asset container reference");
    std::error_code ec;
    auto base=fs::weakly_canonical(root,ec);
    if(ec)throw std::runtime_error("Invalid Content root");
    auto physical=root/fs::u8path(name);
    auto normalized=fs::weakly_canonical(physical,ec);
    if(ec)throw std::runtime_error("Invalid material texture path");
    auto a=base.begin(),b=normalized.begin();
    for(;a!=base.end();++a,++b)
        if(b==normalized.end() || *a!=*b)
            throw std::runtime_error("Asset escapes Content root");
    if(fs::is_symlink(fs::symlink_status(physical,ec)) || ec)
        throw std::runtime_error("Symlink material resource rejected");
    auto bytes=fs::file_size(physical,ec);
    if(ec || bytes<128 || bytes>64u*1024u*1024u)
        throw std::runtime_error("Material resource missing or too large");
    std::ifstream input(physical,std::ios::binary);
    if(!input)throw std::runtime_error("Material resource not readable");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes));
    if(!input.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("Truncated material resource");
    return data;
}
}
MaterialPreviewSource loadMaterialPreviewSource(
    fs::path const& root,std::string const& reference) {
    MaterialPreviewSource result;
    auto const at=reference.rfind('@');
    auto const library=reference.substr(0,at);
    auto const selected=at==std::string::npos?std::string{}:
                          reference.substr(at+1);
    if(selected.find_first_of("/\\@:")!=std::string::npos)
        throw std::runtime_error("Unsafe material entry");
    auto materials=shared_assets::load_materials_from_bytes(
        readFile(root,library),library);
    if(selected.empty() && materials.size()!=1)
        throw std::runtime_error(
            "YMAT is a material library; select a named material entry");
    auto it=selected.empty()?materials.begin():
        std::find_if(materials.begin(),materials.end(),
            [&](auto const& item){return item.name==selected;});
    if(it==materials.end())
        throw std::runtime_error("Material entry not found");
    result.material=std::move(*it);
    std::unordered_map<std::string,std::vector<shared_assets::Texture>> libraries;
    std::size_t read_count=0;
    for(auto const& binding:result.material->textures) {
        if(result.textures.size()>=16)break;
        auto const sep=binding.reference.rfind('@');
        if(sep==std::string::npos) {
            result.missing.push_back(binding.reference);
            continue;
        }
        auto const container=binding.reference.substr(0,sep);
        auto const name=binding.reference.substr(sep+1);
        try {
            auto found=libraries.find(container);
            if(found==libraries.end()) {
                if(++read_count>8)
                    throw std::runtime_error("Texture library limit");
                found=libraries.emplace(
                    container,shared_assets::load_textures_from_bytes(
                        readFile(root,container),container)).first;
            }
            auto& textures=found->second;
            auto match=std::find_if(textures.begin(),textures.end(),
                [&](auto const& t){return t.name==name;});
            if(match==textures.end()) {
                result.missing.push_back(binding.reference);
            } else {
                result.textures.push_back({binding.reference,std::move(*match)});
            }
        } catch(std::exception const&) {
            result.missing.push_back(binding.reference);
        }
    }
    return result;
}
} // namespace northstarcpp::editor
