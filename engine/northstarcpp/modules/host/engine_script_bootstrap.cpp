// Runs once per V8 context; no per-frame work and no project VFS override
// of the protected engine bootstrap, UI root, or scene helpers.
#include "engine_script_bootstrap.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/embedded_script_api.hpp>
#include <northstarcpp/logger.hpp>
#include <northstarcpp/scripting.hpp>
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace northstarcpp::script_bootstrap {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::array<std::string_view,5> fixed_scripts{{
    "scripts/northstar/bootstrap.js",
    "scripts/input/Keys.js",
    "scripts/input/GamepadButtons.js",
    "scripts/scene/Entity.js",
    "scripts/scene/Prefab.js"
}};
struct DefaultScript {
    std::string_view grouped;
    std::string_view legacy;
    bool sealed;
};
constexpr std::array<DefaultScript,11> defaults{{
    {"scripts/ui/UI.js", "scripts/UI.js", true},
    {"scripts/character/Character.js", "scripts/Character.js", true},
    {"scripts/player/playerConfig.js", "scripts/playerConfig.js", false},
    {"scripts/player/playerActor.js", "scripts/playerActor.js", false},
    {"scripts/player/playerSession.js", "scripts/playerSession.js", false},
    {"scripts/camera/playerCamera.js", "scripts/playerCamera.js", false},
    {"scripts/camera/playerLook.js", "scripts/playerLook.js", false},
    {"scripts/player/playerMovement.js", "scripts/playerMovement.js", false},
    {"scripts/character/footsteps.js", "scripts/playerFootsteps.js", false},
    {"scripts/character/playerCharacter.js", "scripts/playerCharacter.js", false},
    {"scripts/player/playerInteraction.js", "scripts/playerInteraction.js", false}
}};

void write_log(Logger* logger,LogLevel level,std::string const& msg) noexcept {
    if(!logger)return;
    try {logger->write(level,"engine.scripting.bootstrap",msg);}
    catch(...) {} // Log backend failure cannot change JS execution semantics.
}
std::uint64_t elapsed_us(Clock::time_point since) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-since).count());
}
bool file_override(Options const& options,std::string_view path) {
    if(options.project_root.empty())return false;
    auto file=options.project_root/std::filesystem::u8path(std::string(path));
    std::error_code ec;
    if(!std::filesystem::exists(file,ec))return false;
    if(options.validate_project_path)
        options.validate_project_path(std::filesystem::u8path(std::string(path)));
    return std::filesystem::is_regular_file(file);
}
bool archive_override(AssetStore const& archive,std::string_view path) {
    try {
        return archive.size(std::string(path))>0;
    }catch(std::runtime_error const&) {
        return false;
    }
}
} // namespace

Report load(ScriptRuntime& runtime,AssetStore const& project_assets,
            Options const& options,Logger* logger) {
    auto const started=Clock::now();
    Report report;
    write_log(logger,LogLevel::info,
        "phase=begin status=begin provider="+runtime.provider_name()+
        " mode="+(options.archive_index.empty()?"project":"archive")+
        " embedded_count="+std::to_string(embedded_api::count()));
    std::unique_ptr<AssetStore> archive;
    if(!options.archive_index.empty()) {
        archive=std::make_unique<AssetStore>();
        archive->mount_archive("",options.archive_index,
            options.archive_data.empty()?options.archive_index:options.archive_data);
    }
    auto evaluate=[&](std::string_view path,bool embedded,std::string_view role) {
        auto const step=Clock::now();
        auto const name=std::string(path);
        std::size_t bytes=0;
        try {
            if(embedded) {
                auto const* resource=embedded_api::find(path);
                if(!resource || path.size()<3 || path.substr(path.size()-3)!=".js")
                    throw std::runtime_error("Required embedded API is missing: "+name);
                bytes=resource->source.size();
                if(bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                    throw std::length_error("Embedded API exceeds scripting source limit");
                // ScriptRuntime::evaluate takes string_view: do not heap-copy the
                // static source data before the native V8 provider consumes it.
                runtime.evaluate(resource->source,resource->virtual_path);
                ++report.embedded;
            }else{
                bytes=static_cast<std::size_t>(project_assets.size(name));
                if(bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                    throw std::length_error("Project script exceeds scripting source limit: "+name);
                runtime.evaluate_asset(project_assets,name);
                ++report.project_overrides;
            }
            report.source_bytes+=bytes;
            write_log(logger,LogLevel::debug,
                "phase=load status=pass source="+std::string(embedded?"embedded":"project")+
                " role="+std::string(role)+" path="+name+
                " bytes="+std::to_string(bytes)+
                " duration_us="+std::to_string(elapsed_us(step)));
        }catch(std::exception const& error) {
            write_log(logger,LogLevel::error,
                "phase=load status=fail source="+std::string(embedded?"embedded":"project")+
                " role="+std::string(role)+" path="+name+
                " duration_us="+std::to_string(elapsed_us(step))+
                " reason="+error.what());
            throw;
        }
    };
    try {
        // Only the universal engine bootstrap is sealed.
        // Character-specific event schemas are registered by character scripts.
        evaluate(fixed_scripts[0],true,"bootstrap");
        runtime.evaluate(
            "void engine.on('northstar._entityBatch', function(packet) {"
            " for (const entity of packet.items) engine.emit(packet.event, entity);"
            "});void 0;", "<engine-entity-batch-bridge>");
        for(std::size_t i=1;i<fixed_scripts.size();++i)
            evaluate(fixed_scripts[i],true,"sdk");
        for(auto const& entry:defaults) {
            if(entry.sealed) {
                evaluate(entry.grouped,true,"sealed");
                continue;
            }
            // Query only project paths. The ordinary VFS may also contain the
            // obsolete sharedAssets scripts, which must NEVER become overrides.
            auto present=[&](std::string_view path) {
                return archive?archive_override(*archive,path):
                               file_override(options,path);
            };
            auto const selected=present(entry.grouped)?entry.grouped:
                                present(entry.legacy)?entry.legacy:std::string_view{};
            evaluate(selected.empty()?entry.grouped:selected,
                     selected.empty(),"default");
        }
        if(options.load_project_entry) {
            constexpr std::string_view path="scripts/main.js";
            if(options.validate_project_path)
                options.validate_project_path(std::filesystem::path(path));
            evaluate(path,false,"entrypoint");
            ++report.project_entries;
        }
        report.elapsed_us=elapsed_us(started);
        write_log(logger,LogLevel::info,
            "phase=complete status=pass embedded="+std::to_string(report.embedded)+
            " overrides="+std::to_string(report.project_overrides)+
            " entrypoints="+std::to_string(report.project_entries)+
            " source_bytes="+std::to_string(report.source_bytes)+
            " duration_us="+std::to_string(report.elapsed_us));
        return report;
    }catch(std::exception const& error) {
        write_log(logger,LogLevel::error,
            "phase=complete status=fail embedded="+std::to_string(report.embedded)+
            " overrides="+std::to_string(report.project_overrides)+
            " duration_us="+std::to_string(elapsed_us(started))+
            " reason="+error.what());
        throw;
    }
}
} // namespace northstarcpp::script_bootstrap
