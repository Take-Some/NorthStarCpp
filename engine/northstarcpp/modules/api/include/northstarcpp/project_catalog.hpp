// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace northstarcpp::projects {

struct TemplateInfo {
    std::string id;
    std::wstring label;
    std::wstring category;
    std::wstring description;
};

struct Project {
    std::filesystem::path directory;
    std::wstring name;
    std::wstring category;
    std::wstring description;
    std::string template_id;
    std::string engine_version;
    std::filesystem::path startup_scene;
    std::vector<std::wstring> tags;
    std::string last_opened;
    // Per-project explicit engine.<capability> -> provider ID.
    // Empty map preserves automatic/default provider selection.
    std::map<std::string,std::string> provider_choices;
    // Optional strict rendering capabilities required by this project.
    // Empty means no declared project-specific GPU feature requirements.
    std::vector<std::string> required_renderer_features;
    bool favorite = false;
    bool external = false;
    enum class Status { ready, missing, invalid, missing_scene, incompatible } status = Status::invalid;
    std::wstring diagnostic;
};

std::wstring to_wide(std::string const& utf8);
std::string to_utf8(std::wstring const& text);
std::wstring status_text(Project::Status status);
std::vector<TemplateInfo> templates();
std::filesystem::path executable_directory();
std::filesystem::path default_projects_directory();
std::filesystem::path default_catalog_file();

class Catalog {
public:
    Catalog(std::filesystem::path projects_directory,
            std::filesystem::path catalog_file,
            std::string engine_version = "0.4.0");

    std::vector<Project> list() const;
    Project inspect(std::filesystem::path const& path) const;
    Project create(std::wstring const& name, std::string const& template_id = "sandbox",
                   std::filesystem::path const& parent = {});
    Project import(std::filesystem::path const& path);
    std::size_t scan(std::filesystem::path const& path, unsigned max_depth = 3);
    Project duplicate(std::filesystem::path const& path, std::wstring const& name,
                      std::filesystem::path const& destination_parent = {});
    void rename(std::filesystem::path const& path, std::wstring const& name);
    void update_description(std::filesystem::path const& path, std::wstring const& description,
                            std::wstring const& category);
    void favorite(std::filesystem::path const& path, bool flag);
    void opened(std::filesystem::path const& path);
    // Saves selection in northstarcpp.project.json; empty provider = automatic.
    // Unknown providers are rejected by the Project Browser's live inventory.
    void set_provider(std::filesystem::path const& path,
                      std::string const& capability,std::string const& provider);
    void set_renderer_requirements(std::filesystem::path const& path,
                                   std::vector<std::string> const& requirements);
    void forget(std::filesystem::path const& path);
    std::filesystem::path const& projects_directory() const { return projects_directory_; }
private:
    std::filesystem::path projects_directory_;
    std::filesystem::path catalog_file_;
    std::string engine_version_;
};

} // namespace northstarcpp::projects
