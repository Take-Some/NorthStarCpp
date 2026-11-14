// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <unordered_set>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace northstarcpp {
namespace {

constexpr float legacy_world_limit = 1000.f;
constexpr float large_world_limit = 1000000.f;
constexpr std::streamsize legacy_file_limit = 512 * 1024;
constexpr std::streamsize large_file_limit = 16 * 1024 * 1024;
constexpr char legacy_scene_header[] = "NorthStarCpp.Scene 1";
constexpr char scene_header[] = "NorthStarCpp.Scene 2";
constexpr char large_scene_header[] = "NorthStarCpp.Scene 3";

bool valid_coordinate(float value, float limit)
{
    return std::isfinite(value) && std::abs(value) <= limit;
}

void fail_windows(char const* operation)
{
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

void write_scene_file(std::filesystem::path const& path, std::string const& bytes)
{
    static std::atomic<unsigned long> sequence{0};
    auto temp = path;
    temp += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(++sequence);
    HANDLE const file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        fail_windows("Cannot create temporary scene file");

    DWORD written = 0;
    auto const size = static_cast<DWORD>(bytes.size());
    BOOL const ok = WriteFile(file, bytes.data(), size, &written, nullptr);
    DWORD failure = ok ? ERROR_SUCCESS : GetLastError();
    if (ok && written != size)
        failure = ERROR_WRITE_FAULT;
    if (failure == ERROR_SUCCESS && !FlushFileBuffers(file))
        failure = GetLastError();
    if (!CloseHandle(file) && failure == ERROR_SUCCESS)
        failure = GetLastError();

    if (failure == ERROR_SUCCESS &&
        !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        failure = GetLastError();
    if (failure != ERROR_SUCCESS) {
        DeleteFileW(temp.c_str());
        throw std::system_error(static_cast<int>(failure), std::system_category(), "Cannot save scene");
    }
}

 } // namespace

void validate_scene_v3(Scene const& scene);

Scene make_startup_scene()
{
    return {{
        {"Player", {0.f, 0.f, 0.f}, 1.5f, 0x46b9d8, true, 2.f},
        {"Static Block", {0.f, 0.f, 3.f}, 1.5f, 0xf2a65a, false, 0.f},
        {"Pushable Cube", {2.4f, 0.f, 0.f}, 1.f, 0x9bd783, false, 1.f},
        {"Falling Cube", {-2.4f, 4.f, 2.f}, 1.f, 0xb486f0, false, 1.f},
    }};
}

void validate_scene(Scene const& scene)
{
    if (scene.version < 1 || scene.version > 3)
        throw std::runtime_error("Unsupported scene document version");
    auto const legacy = scene.version < 3;
    auto const limit = legacy ? max_legacy_scene_objects : max_scene_objects;
    auto const coordinate_limit = legacy ? legacy_world_limit : large_world_limit;
    if (scene.objects.size() > limit)
        throw std::runtime_error("Scene has too many objects");
    bool has_player = false;
    for (auto const& object : scene.objects) {
        if (object.name.empty() || object.name.size() > 96 ||
            std::any_of(object.name.begin(), object.name.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, object.name.data(),
                static_cast<int>(object.name.size()), nullptr, 0))
            throw std::runtime_error("Object name must contain 1 to 96 bytes of valid UTF-8 text");
        if (!valid_coordinate(object.position.x, coordinate_limit) ||
            !valid_coordinate(object.position.y, coordinate_limit) ||
            !valid_coordinate(object.position.z, coordinate_limit))
            throw std::runtime_error("Object coordinates are invalid or exceed scene world bounds");
        if (!std::isfinite(object.size) || object.size < 0.05f || object.size > 100.f)
            throw std::runtime_error("Object size must be between 0.05 and 100");
        if (object.color > 0xffffff)
            throw std::runtime_error("Object color must be RGB");
        if (!std::isfinite(object.mass) || object.mass < 0.f || object.mass > 10000.f ||
            (object.mass > 0.f && object.mass < 0.001f))
            throw std::runtime_error("Mass must be zero or between 0.001 and 10000 kg");
        if (object.player && (has_player || object.mass == 0.f))
            throw std::runtime_error("The scene can have one player-controlled cube with positive mass");
        has_player |= object.player;
    }
    if (!legacy) {
        validate_scene_v3(scene);
        if(scene.components.size()+scene.opaque_records.size()>65536)
            throw std::runtime_error("Too many ECS component records");
        std::unordered_set<std::uint64_t> entity_ids;
        for(auto const& object:scene.objects)entity_ids.insert(object.node.id);
        for(auto const& component:scene.components) {
            if(component.entity_id && !entity_ids.count(component.entity_id))
                throw std::runtime_error("ECS component refers to missing entity");
            if(!validate_scene_component_blob(component))
                throw std::runtime_error("ECS component provider rejected data");
        }
        for(auto const& opaque:scene.opaque_records) {
            if(opaque.kind==0 || opaque.version==0 ||
               (opaque.kind<=7 && opaque.version==1) ||
               opaque.payload.empty() || opaque.payload.size()>1048576)
                throw std::runtime_error("Invalid opaque Scene record");
            if(opaque.entity_id && !entity_ids.count(opaque.entity_id))
                throw std::runtime_error("Opaque component refers to missing entity");
        }
    }
}

Scene load_scene(std::filesystem::path const& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open scene file");
    // Bound file size before reading even the header. Malformed documents with
    // no newline must not cause unbounded allocations via std::getline.
    std::error_code error;
    auto const size = std::filesystem::file_size(path, error);
    if (error || size > static_cast<std::uintmax_t>(large_file_limit))
        throw std::runtime_error("Scene file exceeds 16 MiB or cannot be measured");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!bytes.empty()) {
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (file.bad() || file.gcount() != static_cast<std::streamsize>(bytes.size()))
            throw std::runtime_error("Cannot read complete scene file");
    }
    return parse_scene_data(bytes);
}

Scene parse_scene_data(std::string const& bytes)
{
    if (bytes.size() > static_cast<std::size_t>(large_file_limit))
        throw std::runtime_error("Scene exceeds 16 MiB");
    if (bytes.size()>=4 &&
        (bytes.compare(0,4,"NSCN")==0 || bytes.compare(0,4,"NEF8")==0))
        return parse_scene_binary(bytes);
    if (bytes.rfind(large_scene_header, 0) == 0)
        return parse_scene_v3(bytes);
    if (bytes.size() > static_cast<std::size_t>(legacy_file_limit))
        throw std::runtime_error("Legacy scene exceeds 512 KiB");
    std::istringstream input(bytes);
    input.imbue(std::locale::classic());
    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    bool const legacy = line == legacy_scene_header;
    if (!legacy && line != scene_header)
        throw std::runtime_error("Unsupported scene format");

    Scene scene;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        std::istringstream record(line);
        record.imbue(std::locale::classic());
        record >> std::ws;
        if (record.eof())
            continue;

        SceneObject object;
        std::string kind;
        int player = 0;
        record >> kind >> std::ws;
        if (kind != "cube" || record.peek() != '"' ||
            !(record >> std::quoted(object.name) >> object.position.x >> object.position.y >>
                object.position.z >> object.size >> std::hex >> object.color >> std::dec >> player) ||
            (player != 0 && player != 1) || (!legacy && !(record >> object.mass)) ||
            !(record >> std::ws).eof())
            throw std::runtime_error("Invalid cube record at line " + std::to_string(line_number));

        if (scene.objects.size() == max_legacy_scene_objects)
            throw std::runtime_error("Scene has too many objects");
        object.player = player == 1;
        if (legacy && object.player)
            object.mass = 1.f;
        scene.objects.push_back(std::move(object));
    }
    validate_scene(scene);
    return scene;
}

void save_scene(Scene const& scene, std::filesystem::path const& path)
{
    validate_scene(scene);
    Scene binary = scene;
    if (binary.version < 3) {
        binary.version = 3;
        std::uint64_t next = 1;
        for (auto& object : binary.objects) {
            object.node = SceneNode{};
            object.node.id = next++;
        }
    }
    write_scene_file(path, serialize_scene_binary(binary));
}

} // namespace northstarcpp
