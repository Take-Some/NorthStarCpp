// NorthStarCpp native Project Manager smoke
// Copyright (c) 2026 Kayla's Systems — Kayla Verner
#include <northstarcpp/project_catalog.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using northstarcpp::projects::Catalog;
using northstarcpp::projects::Project;

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try {
        fs::path const root = fs::u8path(argv[1]);
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root);
        Catalog catalog(root / "Projects", root / "catalog.json", "0.2.0");
        if (northstarcpp::projects::templates().empty()) return 3;
        auto created = catalog.create(L"Catalog Smoke", "sandbox");
        if (created.status != Project::Status::ready ||
            !fs::exists(created.directory / "main.nscn"))
            return 4;
        if (catalog.list().size() != 1) return 5;
        catalog.favorite(created.directory, true);
        if (!catalog.list().front().favorite) return 6;
        catalog.update_description(created.directory, L"Playable test level", L"Games");
        catalog.set_provider(created.directory,"engine.render","vulkan");
        catalog.set_provider(created.directory,"engine.physics","jolt");
        auto choices=catalog.inspect(created.directory).provider_choices;
        if(choices.size()!=2 || choices.at("engine.render")!="vulkan" ||
           choices.at("engine.physics")!="jolt")return 23;
        bool invalid_provider=false;
        try {catalog.set_provider(created.directory,"engine.render","../bad.dll");}
        catch(std::invalid_argument const&) {invalid_provider=true;}
        if(!invalid_provider)return 24;
        catalog.rename(created.directory, L"Catalog Renamed");
        auto inspected = catalog.inspect(created.directory);
        if (inspected.name != L"Catalog Renamed" ||
            inspected.description != L"Playable test level")
            return 7;
        catalog.opened(created.directory);
        auto duplicate = catalog.duplicate(created.directory, L"Catalog Copy");
        if (duplicate.status != Project::Status::ready || catalog.list().size() != 2)
            return 8;
        if(duplicate.provider_choices.at("engine.render")!="vulkan" ||
           duplicate.provider_choices.at("engine.physics")!="jolt")
            return 25;
        catalog.set_provider(created.directory,"engine.render","");
        if(catalog.inspect(created.directory).provider_choices.count("engine.render")!=0 ||
           catalog.inspect(created.directory).provider_choices.at("engine.physics")!="jolt")
            return 26;
        auto external_root = root / "External";
        fs::create_directories(external_root);
        auto external = catalog.duplicate(created.directory, L"External Copy", external_root);
        if (external.status != Project::Status::ready || !external.external)
            return 11;
        catalog.forget(external.directory);
        if (!fs::exists(external.directory / "main.nscn"))
            return 12;
        catalog.forget(created.directory);
        if (catalog.list().size() != 1 || !fs::exists(created.directory / "main.nscn"))
            return 9;

        // Automatic listing must discover unregistered nested projects.
        auto const nested = root / "Projects" / "group" / "unregistered";
        fs::create_directories(nested);
        fs::copy_file(created.directory / "northstarcpp.project.json",
                      nested / "northstarcpp.project.json");
        fs::copy_file(created.directory / "main.nscn", nested / "main.nscn");
        auto entries = catalog.list();
        auto has_nested = std::any_of(entries.begin(), entries.end(), [&](auto const& entry) {
            return entry.directory == nested && entry.status == Project::Status::ready;
        });
        if (!has_nested) return 13;
        fs::remove_all(nested);
        entries = catalog.list();
        if (std::any_of(entries.begin(), entries.end(), [&](auto const& entry) {
            return entry.directory == nested;
        })) return 14;

        // A stale registered project must never be considered launchable.
        auto stale = catalog.duplicate(duplicate.directory, L"Stale Copy", external_root);
        fs::remove_all(stale.directory);
        if (catalog.inspect(stale.directory).status != Project::Status::missing)
            return 15;
        bool rejected = false;
        try { catalog.opened(stale.directory); }
        catch (std::exception const&) { rejected = true; }
        if (!rejected) return 16;
        entries = catalog.list();
        if (std::any_of(entries.begin(), entries.end(), [&](auto const& entry) {
            return entry.directory == stale.directory;
        })) return 17;

        // A surviving folder without a manifest is also not a project.
        auto unrecognized = catalog.duplicate(duplicate.directory, L"Unrecognized", external_root);
        fs::remove(unrecognized.directory / "northstarcpp.project.json");
        entries = catalog.list();
        if (std::any_of(entries.begin(), entries.end(), [&](auto const& entry) {
            return entry.directory == unrecognized.directory;
        })) return 20;

        // Existing external projects are still registered, and preferences
        // survive refresh even when a temporarily missing directory returns.
        fs::copy(duplicate.directory, stale.directory, fs::copy_options::recursive);
        entries = catalog.list();
        if (!std::any_of(entries.begin(), entries.end(), [&](auto const& entry) {
            return entry.directory == stale.directory &&
                   entry.status == Project::Status::ready;
        })) return 21;

        auto broken = catalog.duplicate(duplicate.directory, L"Missing Scene", external_root);
        fs::remove(broken.startup_scene);
        if (catalog.inspect(broken.directory).status != Project::Status::missing_scene)
            return 18;
        rejected = false;
        try { catalog.opened(broken.directory); }
        catch (std::exception const&) { rejected = true; }
        if (!rejected) return 19;

        fs::remove_all(root, ignored);
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 10;
    }
}
