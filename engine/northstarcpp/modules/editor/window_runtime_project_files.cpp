#include "window_runtime_project_files.hpp"
#include <northstarcpp/scripting.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace northstarcpp::runtime_project_files {
namespace fs = std::filesystem;
using runtime_options::Options;
// Projects may place their startup scene in a nested level/ directory.
// The package exporter flattens the startup scene into game/main.nscn (legacy releases may use .nscene).
// Validate VFS paths even when scripts are inside a FAT/DB archive.
void validate_script_virtual_path(fs::path const& virtual_path) {
    auto const name=virtual_path.generic_u8string();
    if (name.rfind("scripts/",0)!=0 || name.find(':')!=std::string::npos ||
        name.find('\\')!=std::string::npos ||
        (virtual_path.extension()!=".js" && virtual_path.extension()!=".mjs"))
        throw std::runtime_error("Invalid project script virtual path");
    for (auto const& part:virtual_path)
        if (part.empty() || part=="." || part==".." || part.is_absolute())
            throw std::runtime_error("Script path must remain in project scripts/");
}

fs::path validated_script_file(fs::path const& root,fs::path const& virtual_path) {
    validate_script_virtual_path(virtual_path);
    auto cursor=fs::absolute(root);
    for (auto const& part:virtual_path) {
        if (part.empty() || part=="." || part==".." || part.is_absolute())
            throw std::runtime_error("Script path must remain in project scripts/");
        cursor/=part;
        if (fs::is_symlink(cursor))
            throw std::runtime_error("Script path cannot contain redirected components");
    }
    auto canonical_root=fs::weakly_canonical(root);
    auto canonical_file=fs::weakly_canonical(cursor);
    auto inside=canonical_file.lexically_relative(canonical_root);
    if (inside.empty() || inside.is_absolute() ||
        *inside.begin()==fs::path("..") || !fs::is_regular_file(cursor))
        throw std::runtime_error("Script path escapes project root or file is missing");
    if (fs::file_size(cursor)>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
        throw std::runtime_error("Script source exceeds 1 MiB");
    return cursor;
}

fs::path script_project_root(fs::path const& scene_path) {
    auto const fallback=fs::absolute(scene_path).parent_path();
    auto cursor=fallback;
    for (unsigned depth=0;depth<8;++depth) {
        if (fs::is_regular_file(cursor/"northstarcpp.project.json"))
            return cursor;
        if (cursor==cursor.root_path() || cursor==cursor.parent_path())
            break;
        cursor=cursor.parent_path();
    }
    return fallback;
}

// Game saves belong to the current Windows user, never to an exported
// read-only game package or to the source tree. Source/release names match.
SaveGameStore project_save_store(Options const& config, fs::path const& scene_path) {
    auto const source = config.packaged_game
        ? fs::absolute(scene_path).parent_path().parent_path()/"manifest.json"
        : script_project_root(scene_path)/"northstarcpp.project.json";
    std::string game_name = scene_path.stem().u8string();
    if (fs::is_regular_file(source) && !fs::is_symlink(source) &&
        fs::file_size(source) <= 128u*1024u) {
        boost::property_tree::ptree meta;
        std::ifstream in(source, std::ios::binary);
        if (in) {
            try {
                boost::property_tree::read_json(in,meta);
                auto const key = config.packaged_game ? "game_name" : "name";
                auto name = meta.get<std::string>(key,"");
                if (!name.empty() && name.size() <= 128)
                    game_name = std::move(name);
            } catch (boost::property_tree::json_parser_error const&) {
                // Old projects without metadata retain the scene name.
            }
        }
    }
    std::uint64_t hash=14695981039346656037ull;
    std::string safe;
    for (unsigned char ch : game_name) {
        hash=(hash^ch)*1099511628211ull;
        if (safe.size()<40) {
            if ((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') ||
                (ch>='0' && ch<='9') || ch=='-' || ch=='_')
                safe.push_back(static_cast<char>(ch));
            else if (ch==' ' && !safe.empty() && safe.back()!='-')
                safe.push_back('-');
        }
    }
    if (safe.empty()) safe="project";
    std::ostringstream namespace_name;
    namespace_name << safe << '-' << std::hex << std::setw(16)
                   << std::setfill('0') << hash;
    // Deliberately fail save APIs instead of silently writing into CWD.
    std::wstring appdata(32768,L'\0');
    DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",appdata.data(),
                                         static_cast<DWORD>(appdata.size()));
    if (!count || count>=appdata.size())
        throw std::runtime_error("LOCALAPPDATA is required for game saves");
    appdata.resize(count);
    return SaveGameStore(fs::path(appdata)/L"Kaylas Systems"/L"NorthStarCpp"/
                         L"Saves"/fs::u8path(namespace_name.str()));
}

std::string script_json_quote(std::string_view input) {
    static constexpr char hex[]="0123456789abcdef";
    std::string result(1,'"');
    for (unsigned char c:input) {
        if (c=='"' || c=='\\') {result+='\\';result+=static_cast<char>(c);}
        else if (c<32) {result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=static_cast<char>(c);
    }
    result+='"';
    return result;
}
std::string script_object_payload(SceneObject const& obj,std::size_t index,float dt) {
    std::string payload="{\"id\":" + script_json_quote(std::to_string(obj.node.id)) +
        ",\"index\":" + std::to_string(index) + ",\"script\":" +
        script_json_quote(obj.node.script);
    if (dt>=0.f) payload+=",\"deltaTime\":" + std::to_string(dt);
    return payload+"}";
}

 } // namespace northstarcpp::runtime_project_files
