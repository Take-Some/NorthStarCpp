// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/project_catalog.hpp>
#include <northstarcpp/scene.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <climits>
#include <cwctype>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <string_view>
#include <stdexcept>
#include <system_error>

namespace northstarcpp::projects {
namespace {
namespace fs = std::filesystem;
namespace pt = boost::property_tree;

constexpr char manifest_name[] = "northstarcpp.project.json";
constexpr char startup_name[] = "main.nscn";
constexpr std::uintmax_t manifest_limit = 128 * 1024;

fs::path normalized_absolute(fs::path const& path) { return fs::absolute(path).lexically_normal(); }

bool redirected(fs::path const& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::wstring canonical_key(fs::path const& path)
{
    auto result = normalized_absolute(path).wstring();
    std::transform(result.begin(), result.end(), result.begin(),
        [](wchar_t c) { return std::towlower(c); });
    return result;
}

std::string timestamp()
{
    auto const now = std::chrono::system_clock::now();
    auto const raw = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &raw);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

void atomic_write(fs::path const& target, std::string const& bytes)
{
    fs::create_directories(target.parent_path());
    if (redirected(target))
        throw std::runtime_error("Refusing to overwrite a redirected file");
    auto temporary = target;
    temporary += L".pending-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64());
    try {
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Cannot write project file");
            out.flush();
            if (!out)
                throw std::runtime_error("Cannot flush project file");
        }
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Cannot replace project file");
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

pt::ptree read_json(fs::path const& path, std::uintmax_t limit = manifest_limit)
{
    if (redirected(path) || !fs::is_regular_file(path) || fs::file_size(path) > limit)
        throw std::runtime_error("Project JSON is absent, redirected or too large");
    pt::ptree result;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read project JSON");
    try {
        pt::read_json(file, result);
    } catch (std::exception const& error) {
        throw std::runtime_error(std::string("Invalid JSON: ") + error.what());
    }
    return result;
}

void save_json(fs::path const& path, pt::ptree const& tree)
{
    std::ostringstream buffer;
    pt::write_json(buffer, tree, false);
    atomic_write(path, buffer.str());
}

bool safe_scene(std::string const& path)
{
    if (path.empty() || path.size() > 512 || path.front() == '/' ||
        path.find(':') != std::string::npos || path.find('\\') != std::string::npos ||
        path.find('\0') != std::string::npos)
        return false;
    fs::path const p = fs::u8path(path);
    if ((p.extension() != ".nscn" && p.extension() != ".nscene") || p.is_absolute())
        return false;
    for (auto const& part : p)
        if (part == "." || part == ".." || part.empty())
            return false;
    return true;
}

bool compatible(std::string const& requested, std::string const& current)
{
    if (requested.empty())
        return true; // legacy project
    unsigned a=0,b=0,c=0,x=0,y=0,z=0;
    if (std::sscanf(requested.c_str(), "%u.%u.%u", &a, &b, &c) != 3 ||
        std::sscanf(current.c_str(), "%u.%u.%u", &x, &y, &z) != 3)
        return false;
    return a == x && std::tie(a,b,c) <= std::tie(x,y,z);
}

std::wstring validated_name(std::wstring value)
{
    auto begin = value.find_first_not_of(L" \t");
    if (begin == std::wstring::npos)
        throw std::runtime_error("Empty project name");
    auto end = value.find_last_not_of(L" \t");
    value = value.substr(begin, end - begin + 1);
    if (value.size() > 64 || value.back() == L'.')
        throw std::runtime_error("Project name must be 1-64 characters without trailing dot");
    for (auto c : value)
        if (c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos)
            throw std::runtime_error("Project name contains invalid path characters");

    auto stem = value.substr(0, value.find(L'.'));
    std::transform(stem.begin(), stem.end(), stem.begin(),
        [](wchar_t c) { return std::towupper(c); });
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        (stem.size() == 4 &&
         (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") &&
         stem[3] >= L'1' && stem[3] <= L'9'))
        throw std::runtime_error("Reserved Windows project name");
    return value;
}

std::wstring folder_name(std::wstring name)
{
    name = validated_name(std::move(name));
    for (wchar_t& c : name)
        if (std::iswspace(c))
            c = L'_';
    return name;
}

struct Entry {
    fs::path path;
    bool favorite = false;
    bool hidden = false;
    std::string last_opened;
};

using Entries = std::map<std::wstring, Entry>;

Entries read_catalog(fs::path const& path)
{
    Entries records;
    if (!fs::exists(path))
        return records;
    auto tree = read_json(path, 2 * 1024 * 1024);
    if (tree.get<int>("schema", 0) != 1)
        throw std::runtime_error("Incompatible project library schema");
    auto children = tree.get_child_optional("projects");
    if (!children)
        return records;
    for (auto const& child : *children) {
        auto const name = child.second.get<std::string>("path", "");
        if (name.empty())
            continue;
        fs::path p = fs::u8path(name);
        if (!p.is_absolute())
            continue;
        Entry entry;
        entry.path = normalized_absolute(p);
        entry.favorite = child.second.get<bool>("favorite", false);
        entry.hidden = child.second.get<bool>("hidden", false);
        entry.last_opened = child.second.get<std::string>("last_opened", "");
        records[canonical_key(entry.path)] = std::move(entry);
    }
    return records;
}

void save_catalog(fs::path const& path, Entries const& records)
{
    pt::ptree root, entries;
    root.put("schema", 1);
    for (auto const& [key, entry] : records) {
        pt::ptree row;
        row.put("path", entry.path.u8string());
        row.put("favorite", entry.favorite);
        row.put("hidden", entry.hidden);
        row.put("last_opened", entry.last_opened);
        entries.push_back({"", row});
    }
    root.add_child("projects", entries);
    save_json(path, root);
}

std::string starter_scene(std::string const& id)
{
    static std::string const header = "NorthStarCpp.Scene 2\n";
    if (id == "blank")
        return header;
    if (id == "sandbox")
        return header +
            "cube \"Player\" 0 0 0 1.5 46b9d8 1 1.5\n"
            "cube \"Landmark A\" -3 0 3 1 f2a65a 0 0\n"
            "cube \"Landmark B\" 3 0 3 1 9bd783 0 0\n";
    if (id == "physics")
        return header +
            "cube \"Platform\" 0 0 0 8 64748b 0 0\n"
            "cube \"Falling A\" -1 8 0 1 ffcc66 0 2\n"
            "cube \"Falling B\" 1 11 0 1 f28c91 0 3\n"
            "cube \"Player\" 3 0 0 1 46b9d8 1 1\n";
    if (id == "city")
        return header +
            "cube \"Road Marker\" 0 0 0 6 29313b 0 0\n"
            "cube \"Building A\" -5 0 1 3 8294a4 0 0\n"
            "cube \"Building B\" 5 0 1 3 8294a4 0 0\n"
            "cube \"Player\" 0 0 -5 1 46b9d8 1 1\n";
    if (id == "architecture")
        return header +
            "cube \"Floor\" 0 0 0 9 7e8897 0 0\n"
            "cube \"Volume A\" -2 3 0 2 a7b8ca 0 0\n"
            "cube \"Volume B\" 2 3 0 2 e0caa6 0 0\n";
    throw std::runtime_error("Unknown project template");
}

void reject_redirects(fs::path const& base)
{
    if (redirected(base))
        throw std::runtime_error("Project directory cannot be a symlink or junction");
}

fs::path checked_directory(fs::path const& path)
{
    if (!fs::is_directory(path))
        throw std::runtime_error("Project directory does not exist");
    reject_redirects(path);
    return normalized_absolute(path);
}

// Read the actual filesystem on each listing. No registry mutation, symlink
// traversal or assumption that projects are only immediate children.
std::vector<fs::path> discover_projects(fs::path const& root, unsigned max_depth)
{
    std::vector<fs::path> projects;
    std::error_code error;
    if (!fs::is_directory(root, error) || redirected(root))
        return projects;

    std::deque<std::pair<fs::path, unsigned>> pending{{root, 0}};
    std::size_t visited = 0;
    while (!pending.empty() && visited < 5000 && projects.size() < 500) {
        auto [directory, depth] = std::move(pending.front());
        pending.pop_front();
        ++visited;

        error.clear();
        auto const manifest = directory / manifest_name;
        if (!redirected(manifest) && fs::is_regular_file(manifest, error)) {
            projects.push_back(std::move(directory));
            continue; // Never interpret asset subdirectories as other projects.
        }
        if (depth >= std::min(max_depth, 6u))
            continue;

        error.clear();
        fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, error);
        fs::directory_iterator const end;
        while (!error && it != end) {
            std::error_code node_error;
            auto const& node = *it;
            if (node.is_directory(node_error) && !node_error && !redirected(node.path())) {
                auto const name = node.path().filename().wstring();
                if (name != L".git" && name != L".northstarcpp" &&
                    name != L"node_modules" && name != L"build" &&
                    name != L"out" && name != L"dist" && pending.size() < 5000)
                    pending.emplace_back(node.path(), depth + 1);
            }
            it.increment(error);
        }
    }
    return projects;
}
} // namespace

std::wstring to_wide(std::string const& source)
{
    if (source.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                                    static_cast<int>(source.size()), nullptr, 0);
    if (!size)
        throw std::runtime_error("Invalid UTF-8 project metadata");
    std::wstring text(static_cast<size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                             static_cast<int>(source.size()), text.data(), size))
        throw std::runtime_error("UTF-8 conversion failed");
    return text;
}

std::string to_utf8(std::wstring const& source)
{
    if (source.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                                    static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
    if (!size)
        throw std::runtime_error("Invalid UTF-16 project metadata");
    std::string text(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                             static_cast<int>(source.size()), text.data(), size, nullptr, nullptr))
        throw std::runtime_error("UTF-16 conversion failed");
    return text;
}

std::wstring status_text(Project::Status status)
{
    switch (status) {
    case Project::Status::ready: return L"Ready";
    case Project::Status::missing: return L"Directory unavailable";
    case Project::Status::invalid: return L"Invalid project";
    case Project::Status::missing_scene: return L"Startup scene missing";
    case Project::Status::incompatible: return L"Engine version mismatch";
    }
    return L"Unknown";
}

std::vector<TemplateInfo> templates()
{
    return {
        {"blank", L"Blank", L"General", L"Empty scene"},
        {"sandbox", L"Sandbox", L"Games", L"Playable starter scene"},
        {"physics", L"Physics Playground", L"Simulation", L"Rigid bodies and collisions"},
        {"city", L"City Block", L"Worlds", L"Basic city-scene prototype"},
        {"architecture", L"Architecture", L"Visualization", L"Geometry exploration"},
    };
}

fs::path executable_directory()
{
    std::array<wchar_t, 32768> buffer{};
    auto n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size())
        throw std::runtime_error("Cannot locate executable directory");
    return fs::path(buffer.data()).parent_path();
}

fs::path default_projects_directory()
{
    auto const exe = executable_directory();
    // During development the EXE may live under out/<config> or
    // dist/<version>/bin. Preserve the workspace Projects directory without
    // assuming the working directory is the repository root.
    auto directory = exe;
    for (int level = 0; level != 6; ++level) {
        if (fs::is_regular_file(directory / "CMakeLists.txt") &&
            fs::is_directory(directory / "Projects")) {
            std::ifstream cmake(directory / "CMakeLists.txt");
            std::string beginning(2048, '\0');
            cmake.read(beginning.data(), static_cast<std::streamsize>(beginning.size()));
            beginning.resize(static_cast<size_t>(cmake.gcount()));
            if (beginning.find("project(NorthStarCpp") != std::string::npos)
                return directory / "Projects";
        }
        if (directory == directory.parent_path())
            break;
        directory = directory.parent_path();
    }
    // Portable installation: keep writable user projects local to the browser.
    return exe / "Projects";
}

fs::path default_catalog_file()
{
    std::array<wchar_t, 32768> buffer{};
    auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
                                        static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size())
        return fs::path(buffer.data()) / L"KaylasSystems" / L"NorthStarCpp" / L"project_catalog.json";
    return executable_directory() / ".northstarcpp" / "project_catalog.json";
}

Catalog::Catalog(fs::path projects_directory, fs::path catalog_file, std::string engine_version)
    : projects_directory_(normalized_absolute(projects_directory)), catalog_file_(normalized_absolute(catalog_file)),
      engine_version_(std::move(engine_version))
{
}

Project Catalog::inspect(fs::path const& path) const
{
    Project project{};
    project.directory = normalized_absolute(path);
    project.name = project.directory.filename().wstring();
    project.external = canonical_key(project.directory.parent_path()) != canonical_key(projects_directory_);
    std::error_code directory_error;
    if (!fs::is_directory(project.directory, directory_error)) {
        project.status = Project::Status::missing;
        project.diagnostic = L"Project directory is missing";
        return project;
    }
    try {
        reject_redirects(project.directory);
        auto data = read_json(project.directory / manifest_name);
        if (data.get<std::string>("engine", "") != "NorthStarCpp")
            throw std::runtime_error("Not a NorthStarCpp project");
        auto const schema = data.get<int>("schema_version", 1);
        if (schema != 1 && schema != 2)
            throw std::runtime_error("Unsupported project schema");
        project.name = to_wide(data.get<std::string>("name", to_utf8(project.name)));
        if (project.name.empty())
            throw std::runtime_error("Project name is empty");
        project.category = to_wide(data.get<std::string>("category", "General"));
        project.description = to_wide(data.get<std::string>("description", ""));
        project.template_id = data.get<std::string>("template", "sandbox");
        project.engine_version = data.get<std::string>("engine_version", "");
        auto scene_name = data.get<std::string>("startup_scene", startup_name);
        if (!safe_scene(scene_name))
            throw std::runtime_error("Unsafe startup scene path");
        project.startup_scene = project.directory / fs::u8path(scene_name);
        for (auto const& parent : project.startup_scene.parent_path()) {
            (void)parent;
        }
        auto partial = project.directory;
        for (auto const& piece : fs::u8path(scene_name).parent_path()) {
            partial /= piece;
            if (redirected(partial))
                throw std::runtime_error("Redirected startup scene directory");
        }
        if (!fs::is_regular_file(project.startup_scene) || redirected(project.startup_scene)) {
            project.status = Project::Status::missing_scene;
            project.diagnostic = L"Create or restore the startup .nscn scene file";
        } else if (!compatible(project.engine_version, engine_version_)) {
            project.status = Project::Status::incompatible;
            project.diagnostic = L"Project targets a newer or different engine major version";
        } else {
            project.status = Project::Status::ready;
        }
        if (auto const tags = data.get_child_optional("tags")) {
            for (auto const& tag : *tags)
                if (project.tags.size() < 12)
                    project.tags.push_back(to_wide(tag.second.get_value<std::string>()));
        }
    } catch (std::exception const& ex) {
        project.status = Project::Status::invalid;
        try { project.diagnostic = to_wide(ex.what()); }
        catch (...) { project.diagnostic = L"Invalid project metadata"; }
    }
    return project;
}

std::vector<Project> Catalog::list() const
{
    Entries const data = read_catalog(catalog_file_);
    std::map<std::wstring, Project> found;
    for (auto const& directory : discover_projects(projects_directory_, 4)) {
        auto const key = canonical_key(directory);
        if (auto it = data.find(key); it != data.end() && it->second.hidden)
            continue;
        found[key] = inspect(directory);
    }
    // Registry entries carry user preferences, not proof of project existence.
    // Only show registered external projects if their directory and manifest
    // still exist. Missing paths (including obsolete legacy workspace paths)
    // are omitted without erasing their saved preferences or user files.
    for (auto const& [key, entry] : data) {
        if (entry.hidden || found.count(key))
            continue;
        std::error_code ec;
        if (!fs::is_directory(entry.path, ec) || ec ||
            redirected(entry.path) ||
            !fs::is_regular_file(entry.path / manifest_name, ec) || ec ||
            redirected(entry.path / manifest_name))
            continue;
        auto project = inspect(entry.path);
        if (project.status != Project::Status::missing)
            found[key] = std::move(project);
    }
    std::vector<Project> result;
    result.reserve(found.size());
    for (auto& [key, project] : found) {
        auto it = data.find(key);
        if (it != data.end()) {
            project.favorite = it->second.favorite;
            project.last_opened = it->second.last_opened;
        }
        result.push_back(std::move(project));
    }
    std::sort(result.begin(), result.end(), [](Project const& a, Project const& b) {
        if (a.favorite != b.favorite) return a.favorite > b.favorite;
        if (a.last_opened != b.last_opened) return a.last_opened > b.last_opened;
        return a.name < b.name;
    });
    return result;
}

Project Catalog::import(fs::path const& value)
{
    fs::path folder = value;
    if (folder.filename() == manifest_name)
        folder = folder.parent_path();
    auto project = inspect(checked_directory(folder));
    if (project.status == Project::Status::invalid)
        throw std::runtime_error("Cannot import invalid NorthStarCpp project");
    auto records = read_catalog(catalog_file_);
    auto& entry = records[canonical_key(project.directory)];
    entry.path = project.directory;
    entry.hidden = false;
    save_catalog(catalog_file_, records);
    return project;
}

Project Catalog::create(std::wstring const& name, std::string const& template_id,
                        fs::path const& parent)
{
    auto const& types = templates();
    auto type = std::find_if(types.begin(), types.end(), [&](TemplateInfo const& t) {
        return t.id == template_id;
    });
    if (type == types.end())
        throw std::runtime_error("Unknown project template");
    auto container = parent.empty() ? projects_directory_ : normalized_absolute(parent);
    if (fs::exists(container) && redirected(container))
        throw std::runtime_error("Cannot create projects under redirected directories");
    fs::create_directories(container);
    auto target = container / folder_name(name);
    if (fs::exists(target))
        throw std::runtime_error("Project already exists");
    if (!fs::create_directory(target))
        throw std::runtime_error("Cannot create project directory");
    try {
        for (auto const* sub : {"assets", "scripts", "config"})
            fs::create_directory(target / sub);
        save_scene(parse_scene_data(starter_scene(template_id)), target / startup_name);

        pt::ptree meta;
        meta.put("schema_version", 2);
        meta.put("engine", "NorthStarCpp");
        meta.put("engine_version", engine_version_);
        meta.put("name", to_utf8(name));
        meta.put("description", "");
        meta.put("template", template_id);
        meta.put("category", to_utf8(type->category));
        meta.put("startup_scene", startup_name);
        meta.put("created_at", timestamp());
        meta.put("state", "scaffold");
        save_json(target / manifest_name, meta);
        atomic_write(target / "README.md", "# " + to_utf8(name) + "\n\n"
            "Created with NorthStarCpp Project Manager (native C++).\n");
        return import(target);
    } catch (...) {
        std::error_code ec;
        fs::remove_all(target, ec);
        throw;
    }
}

std::size_t Catalog::scan(fs::path const& root, unsigned max_depth)
{
    auto const start = checked_directory(root);
    std::size_t count = 0;
    for (auto const& directory : discover_projects(start, max_depth)) {
        try { import(directory); ++count; }
        catch (std::exception const&) { /* Ignore malformed individual projects. */ }
    }
    return count;
}

Project Catalog::duplicate(fs::path const& path, std::wstring const& new_name,
                           fs::path const& destination_parent)
{
    auto original = inspect(path);
    if (original.status != Project::Status::ready)
        throw std::runtime_error("Source project is unavailable");
    auto const parent = destination_parent.empty()
        ? projects_directory_ : normalized_absolute(destination_parent);
    if (redirected(parent))
        throw std::runtime_error("Duplicate location is a redirected directory");
    auto const target = parent / folder_name(new_name);
    auto const original_key = canonical_key(original.directory);
    auto const target_key = canonical_key(target);
    if (target_key == original_key || target_key.substr(0,original_key.size()+1) ==
            original_key + L"\\")
        throw std::runtime_error("Cannot duplicate a project inside itself");
    if (fs::exists(target))
        throw std::runtime_error("Duplicate project already exists");
    for (auto const& entry : fs::recursive_directory_iterator(original.directory))
        if (redirected(entry.path()))
            throw std::runtime_error("Project contains redirected files");
    fs::create_directories(parent);
    fs::copy(original.directory, target, fs::copy_options::recursive);
    try {
        auto metadata = read_json(target / manifest_name);
        metadata.put("name", to_utf8(new_name));
        metadata.put("created_at", timestamp());
        save_json(target / manifest_name, metadata);
        return import(target);
    } catch (...) {
        std::error_code ec;
        fs::remove_all(target, ec);
        throw;
    }
}

void Catalog::rename(fs::path const& path, std::wstring const& new_name)
{
    auto const name = validated_name(new_name);
    auto project = inspect(path);
    if (project.status != Project::Status::ready)
        throw std::runtime_error("Project is unavailable");
    auto const manifest = project.directory / manifest_name;
    auto metadata = read_json(manifest);
    metadata.put("name", to_utf8(name));
    save_json(manifest, metadata);
}

void Catalog::update_description(fs::path const& path, std::wstring const& description,
                                 std::wstring const& category)
{
    auto project = inspect(path);
    if (project.status != Project::Status::ready)
        throw std::runtime_error("Project is unavailable");
    if (description.size() > 1024 || category.empty() || category.size() > 80)
        throw std::runtime_error("Project metadata length is invalid");
    auto const manifest = project.directory / manifest_name;
    auto metadata = read_json(manifest);
    metadata.put("description", to_utf8(description));
    metadata.put("category", to_utf8(category));
    save_json(manifest, metadata);
}

void Catalog::favorite(fs::path const& path, bool flag)
{
    auto const project = inspect(path);
    if (project.status == Project::Status::missing || project.status == Project::Status::invalid)
        throw std::runtime_error("Cannot favorite missing or invalid project");
    auto entries = read_catalog(catalog_file_);
    auto& entry = entries[canonical_key(project.directory)];
    entry.path = project.directory;
    entry.hidden = false;
    entry.favorite = flag;
    save_catalog(catalog_file_, entries);
}

void Catalog::opened(fs::path const& path)
{
    auto const project = inspect(path);
    if (project.status != Project::Status::ready)
        throw std::runtime_error("Cannot mark unavailable project as opened");
    auto entries = read_catalog(catalog_file_);
    auto& entry = entries[canonical_key(project.directory)];
    entry.path = project.directory;
    entry.hidden = false;
    entry.last_opened = timestamp();
    save_catalog(catalog_file_, entries);
}

void Catalog::forget(fs::path const& path)
{
    auto entries = read_catalog(catalog_file_);
    auto const directory = normalized_absolute(path);
    auto& entry = entries[canonical_key(directory)];
    entry.path = directory;
    entry.hidden = true; // Only hides library entry; NEVER deletes project files.
    save_catalog(catalog_file_, entries);
}

} // namespace northstarcpp::projects
