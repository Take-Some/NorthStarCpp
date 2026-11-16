#pragma once
#include "window_runtime_options.hpp"
#include <northstarcpp/scene.hpp>
#include <northstarcpp/save_game.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
namespace northstarcpp::runtime_project_files {
void validate_script_virtual_path(std::filesystem::path const&);
std::filesystem::path validated_script_file(std::filesystem::path const&,std::filesystem::path const&);
std::filesystem::path script_project_root(std::filesystem::path const&);
SaveGameStore project_save_store(runtime_options::Options const&,std::filesystem::path const&);
std::string script_json_quote(std::string_view);
std::string script_object_payload(SceneObject const&,std::size_t,float dt=-1.f);
}
