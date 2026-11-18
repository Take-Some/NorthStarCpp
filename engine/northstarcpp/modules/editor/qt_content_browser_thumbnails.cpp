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
// One provider-backed asset session per thumbnail worker and content root.
// This avoids redundant DLL contexts without allowing source-specific
// shortcuts to bypass NSP mounts, project overlays or virtual references.
AssetStore& thumbnailAssets(fs::path const& root)
{
    struct Session {
        fs::path root;
        std::unique_ptr<AssetStore> store;
    };
    static thread_local Session session;
    if(!session.store || session.root!=root) {
        auto next=std::make_unique<AssetStore>();
        next->mount_content_assets(root);
        session.store=std::move(next);
        session.root=root;
    }
    return *session.store;
}
QImage directListFileThumbnail(fs::path const& root,std::string const& key,
                               AssetKind kind)
{
    if(key.find('@')==std::string::npos ||
       (kind!=AssetKind::texture && kind!=AssetKind::mesh &&
        kind!=AssetKind::material))return {};
    if(kind==AssetKind::material) {
        // Material graph previews remain a distinct rendering technique,
        // but texture/material resolution uses the SAME provider VFS.
        static std::mutex materialPreviewMutex;
        std::lock_guard<std::mutex> guard(materialPreviewMutex);
        auto source=loadMaterialPreviewSource(root,key);
        if(!source.material)return {};
        auto rendered=editor_tools::preview_material(
            *source.material,source.textures,256);
        if(!rendered.valid())return {};
        QImage image(reinterpret_cast<uchar const*>(rendered.argb.data()),
             rendered.width,rendered.height,rendered.width*4,
             QImage::Format_ARGB32);
        return image.copy();
    }
    auto& assets=thumbnailAssets(root);
    auto to_image=[](auto const& preview)->QImage {
        if(!preview.valid())return {};
        QImage image(reinterpret_cast<uchar const*>(preview.argb.data()),
            preview.width,preview.height,preview.width*4,
            QImage::Format_ARGB32);
        return image.copy();
    };
    if(kind==AssetKind::texture)
        return to_image(editor_tools::preview_texture(
            assets.load_texture(key),256));
    return to_image(editor_tools::preview_drawable(
        assets.load_drawable(key),256,256,.62f,.27f,1.f,false));
}
} // namespace
QImage engineThumbnail(fs::path const& root,std::string const& key, AssetKind kind) {
    try {
        if(kind==AssetKind::prefab) {
            auto prefab=prefab_preview::load(root,key);
            try {
                // Render the entire native composition on this existing
                // async thumbnail worker; one camera and shared Z buffer.
                auto composition=prefab_preview::load_composition(
                    prefab,thumbnailAssets(root));
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
        auto& assets=thumbnailAssets(root);
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
