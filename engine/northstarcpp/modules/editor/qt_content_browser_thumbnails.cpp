// NEF8 thumbnail backend: decode/cache on thumbnail workers only.
// Separated from Qt navigation and file-mutation paths.
#include "qt_content_browser_thumbnails.hpp"
#include "editor_asset_thumbnails.hpp"
#include "qt_prefab_preview.hpp"
#include "editor_prefab_composition.hpp"
#include "editor_asset_material_source.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

namespace northstarcpp::editor::browser_internal {
namespace fs=std::filesystem;
namespace {
QImage directListFileThumbnail(fs::path const& root,std::string const& key,
                               AssetKind kind) {
    auto const separator=key.find('@');
    if(separator==std::string::npos ||
       (kind!=AssetKind::texture && kind!=AssetKind::mesh &&
        kind!=AssetKind::material))return {};
    auto const relative=key.substr(0,separator);
    auto const name=key.substr(separator+1);
    if(kind==AssetKind::material) {
        // NEF8 material previews share a codec/Qt worker backend. Protect
        // the material-decoding section pending a full sanitizer audit,
        // while leaving texture and model thumbnails parallel.
        static std::mutex materialPreviewMutex;
        std::lock_guard<std::mutex> guard(materialPreviewMutex);
        auto resources=loadMaterialPreviewSource(root,key);
        if(!resources.material)return {};
        auto sphere=northstarcpp::editor_tools::preview_material(
            *resources.material,resources.textures,256);
        if(!sphere.valid())return {};
        QImage bitmap(reinterpret_cast<uchar const*>(sphere.argb.data()),
             sphere.width,sphere.height,sphere.width*4,QImage::Format_ARGB32);
        return bitmap.copy();
    }
    if(!safe_virtual_path(relative) || name.empty() ||
       name.find_first_of("@/\\\\:")!=std::string::npos)return {};
    auto const full=root/fs::u8path(relative);
    std::error_code ec;
    auto const canonical_root=fs::weakly_canonical(root,ec);
    if(ec)return {};
    auto const canonical_file=fs::weakly_canonical(full,ec);
    if(ec)return {};
    auto base=canonical_root.begin();
    auto entry=canonical_file.begin();
    for(;base!=canonical_root.end();++base,++entry)
        if(entry==canonical_file.end() || *entry!=*base)return {};
    if(fs::is_symlink(fs::symlink_status(full,ec)) || ec)return {};
    auto const bytes_count=fs::file_size(full,ec);
    if(ec || bytes_count<128 || bytes_count>64u*1024u*1024u)return {};
    auto const modified=fs::last_write_time(full,ec);
    if(ec)return {};
    struct Cache {
        fs::path file;
        fs::file_time_type modified{};
        std::uintmax_t bytes=0;
        std::vector<northstarcpp::shared_assets::Texture> textures;
        std::vector<northstarcpp::shared_assets::Drawable> drawables;
    };
    static thread_local Cache cache;
    if(cache.file!=full || cache.modified!=modified || cache.bytes!=bytes_count) {
        std::ifstream input(full,std::ios::binary);
        if(!input)return {};
        std::vector<std::uint8_t> encoded(static_cast<std::size_t>(bytes_count));
        if(!input.read(reinterpret_cast<char*>(encoded.data()),
                       static_cast<std::streamsize>(encoded.size())))return {};
        Cache updated;
        updated.file=full;updated.modified=modified;updated.bytes=bytes_count;
        if(kind==AssetKind::texture)
            updated.textures=northstarcpp::shared_assets::load_textures_from_bytes(encoded,relative);
        else
            updated.drawables=northstarcpp::shared_assets::load_drawables_from_bytes(encoded,relative);
        cache=std::move(updated);
    }
    northstarcpp::editor_tools::AssetPreviewImage preview;
    if(kind==AssetKind::texture) {
        auto found=std::find_if(cache.textures.begin(),cache.textures.end(),
            [&](auto const& item){return item.name==name;});
        if(found==cache.textures.end())return {};
        preview=northstarcpp::editor_tools::preview_texture(*found,256);
    } else {
        auto found=std::find_if(cache.drawables.begin(),cache.drawables.end(),
            [&](auto const& item){return item.name==name;});
        if(found==cache.drawables.end())return {};
        preview=northstarcpp::editor_tools::preview_drawable(
            *found,256,256,.62f,.27f,1.f,false);
    }
    if(!preview.valid())return {};
    QImage image(reinterpret_cast<uchar const*>(preview.argb.data()),
                 preview.width,preview.height,preview.width*4,
                 QImage::Format_ARGB32);
    return image.copy();
}
} // namespace
QImage engineThumbnail(fs::path const& root,std::string const& key, AssetKind kind) {
    try {
        if(kind==AssetKind::prefab) {
            auto prefab=prefab_preview::load(root,key);
            try {
                // Render the entire native composition on this existing
                // async thumbnail worker; one camera and shared Z buffer.
                AssetManager assets;
                assets.mount_directory("",root,100);
                std::error_code ec;
                for(auto current=root.parent_path();!current.empty();) {
                    auto shared=current/"sharedAssets";
                    if(fs::is_directory(shared,ec)) {
                        assets.mount_shared_assets(shared);
                        break;
                    }
                    auto upper=current.parent_path();
                    if(upper==current)break;
                    current=upper;
                }
                auto composition=prefab_preview::load_composition(
                    prefab,assets);
                if(!composition.drawable.meshes.empty()) {
                    auto rendered=editor_tools::preview_drawable(
                        composition.drawable,320,320,.62f,.27f,1.f,
                        false,&composition.materials);
                    if(rendered.valid()) {
                        QImage image(reinterpret_cast<uchar const*>(
                            rendered.argb.data()),rendered.width,
                            rendered.height,rendered.width*4,
                            QImage::Format_ARGB32);
                        return image.copy();
                    }
                }
            }catch(std::exception const&) {
                // Preserve an accurate scene schematic if native assets
                // cannot be resolved by an external engine.assets provider.
            }
            return prefab_preview::thumbnail(prefab);
        }
        if(key.find('@')!=std::string::npos &&
           (kind==AssetKind::texture || kind==AssetKind::mesh ||
            kind==AssetKind::material)) {
            // Embedded items have their own identity; never substitute
            // the first entry or a generic icon on successful decode.
            return directListFileThumbnail(root,key,kind);
        }
        AssetManager assets;
        assets.mount_directory("",root,0);
        // Authored resources can refer to the engine's shared asset package.
        std::error_code ec;
        for(auto current=root.parent_path(); !current.empty(); ) {
            auto shared=current/"sharedAssets";
            if(fs::is_directory(shared,ec)) {
                assets.mount_shared_assets(shared);break;
            }
            auto upper=current.parent_path();
            if(upper==current)break;
            current=upper;
        }
        northstarcpp::editor_tools::Thumbnail t;
        if(kind==AssetKind::texture) {
            if(key.find('@')!=std::string::npos)
                t=northstarcpp::editor_tools::thumbnail_texture(
                    assets.load_texture(key));
            else {
                auto list=northstarcpp::shared_assets::load_textures(assets,key);
                if(!list.empty())t=northstarcpp::editor_tools::thumbnail_texture(list.front());
            }
        } else if(kind==AssetKind::mesh) {
            if(key.find('@')!=std::string::npos)
                t=northstarcpp::editor_tools::thumbnail_drawable(
                    assets.load_drawable(key));
            else {
                auto list=northstarcpp::shared_assets::load_drawables(assets,key);
                if(!list.empty())t=northstarcpp::editor_tools::thumbnail_drawable(list.front());
            }
        } else if(kind==AssetKind::material) {
            auto list=northstarcpp::shared_assets::load_materials(assets,key);
            if(!list.empty())t=northstarcpp::editor_tools::thumbnail_material(list.front());
        }
        if(t.valid()) {
            QImage image(reinterpret_cast<uchar const*>(t.bgra.data()),
                t.size,t.size,QImage::Format_ARGB32);
            return image.copy();
        }
    } catch(std::exception const&) {
        // Unavailable/invalid providers keep a category icon.
    }
    return {};
}
} // namespace northstarcpp::editor::browser_internal
