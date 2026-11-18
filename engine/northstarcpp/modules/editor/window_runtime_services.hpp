#pragma once
// Player-owned scene startup services. References remain valid while App lives.
#include "window_runtime_options.hpp"
#include <northstarcpp/scene.hpp>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace northstarcpp::runtime_services {
struct ScriptMode {
    bool scripted_game=false;
    bool first_person=false;
};
class RuntimeServices final {
public:
    RuntimeServices(runtime_options::Options const& options,
                    std::filesystem::path const& scene_path) noexcept
        : options_(options), scene_path_(scene_path) {}
    // No change -> no reload, preserving the editor ECS document.
    std::optional<Scene> resolve_visual_prefabs(
        Scene scene,
        std::function<void(std::string const&)> const& warning) const;
    ScriptMode discover_script_mode(Scene const& scene) const;
private:
    // App owns both objects and declares this service after them.
    runtime_options::Options const& options_;
    std::filesystem::path const& scene_path_;
};
} // namespace northstarcpp::runtime_services
