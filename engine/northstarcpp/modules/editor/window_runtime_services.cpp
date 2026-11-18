#include "window_runtime_services.hpp"
#include "window_runtime_project_files.hpp"
#include "editor_visual_prefab.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/scripting.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace northstarcpp::runtime_services {
namespace fs=std::filesystem;

std::optional<Scene> RuntimeServices::resolve_visual_prefabs(
    Scene candidate,
    std::function<void(std::string const&)> const& warning) const {
    auto const refs=candidate.version==3 ?
        editor::visual_prefab::links(candidate) :
        std::vector<std::pair<std::uint64_t,std::string>>{};
    if(refs.empty())return {};
    AssetStore store;
    if(options_.archive_path.empty()) {
        auto const base=runtime_project_files::script_project_root(scene_path_);
        store.mount_project_assets(base,
            runtime_options::discover_shared_assets(options_,base));
    } else {
        store.mount_runtime_archives();
        store.mount_archive("",options_.archive_path,
            options_.archive_data.empty() ?
                options_.archive_path : options_.archive_data);
    }
    std::size_t applied=0;
    for(auto const& [id,ref]:refs) {
        try {
            auto bytes=store.read(ref);
            if(bytes.size()>4u*1024u*1024u)
                throw std::length_error("Visual prefab exceeds 4 MiB");
            auto prefab=parse_scene_data(
                std::string(bytes.begin(),bytes.end()));
            // Apply transactionally: a failed prefab must not partially mutate
            // an otherwise valid scene (same semantics as original App).
            auto proposed=candidate;
            editor::visual_prefab::apply(proposed,id,prefab);
            candidate=std::move(proposed);
            ++applied;
        } catch(std::exception const& e) {
            warning("Unable to resolve "+ref+": "+e.what());
        }
    }
    if(!applied)return {};
    return candidate;
}

ScriptMode RuntimeServices::discover_script_mode(Scene const& source) const {
    ScriptMode result;
    if(scene_path_.empty() || options_.smoke || options_.fps_ui_smoke ||
       options_.check_scene)
        return result;
    auto const root=options_.archive_path.empty() ?
        runtime_project_files::script_project_root(scene_path_) : fs::path{};
    if(options_.archive_path.empty()) {
        // assets/scripts is the canonical project source; legacy scripts/
        // remains a read-only compatibility fallback for older projects.
        result.scripted_game=fs::is_regular_file(
            root/"assets"/"scripts"/"main.js") ||
            fs::is_regular_file(root/"scripts"/"main.js");
    } else {
        AssetStore archive_assets;
        archive_assets.mount_archive("",options_.archive_path,
            options_.archive_data.empty() ?
                options_.archive_path : options_.archive_data);
        try {
            auto const bytes=archive_assets.size("scripts/main.js");
            if(bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                throw std::runtime_error("Archived main.js exceeds 1 MiB");
            result.scripted_game=true;
        } catch(std::runtime_error const& e) {
            if(std::string_view(e.what()).find("exceeds 1 MiB")!=std::string_view::npos)
                throw;
        }
    }
    if(!result.scripted_game) {
        result.scripted_game=std::any_of(source.objects.begin(),source.objects.end(),
            [&](SceneObject const& object) {
                return scene_pawn::is_local(source,object) ||
                    !object.node.script.empty();
            });
    }
    result.first_person=result.scripted_game && options_.fps;
    if(result.scripted_game && !result.first_person &&
       options_.archive_path.empty()) {
        auto const manifest=root/"northstarcpp.project.json";
        if(fs::is_regular_file(manifest)) {
            boost::property_tree::ptree metadata;
            boost::property_tree::read_json(manifest.u8string(),metadata);
            auto const mode=metadata.get<std::string>("game_mode","scene");
            result.first_person=(mode=="fps" || mode=="explore");
        }
    }
    return result;
}
} // namespace northstarcpp::runtime_services
