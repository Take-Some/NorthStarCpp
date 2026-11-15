#include <northstarcpp/assets.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
void write(fs::path const& p, std::string const& value) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary); out << value;
    if (!out) throw std::runtime_error("Cannot initialize asset test fixture");
}
std::string text(std::vector<std::uint8_t> const& d) { return {d.begin(), d.end()}; }

using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t fat_header = 24, root_node = 8, folder_node = 64;
constexpr std::size_t scene_node = 136, inline_node = 232, empty_node = 312;
constexpr std::size_t inline_offset = 280, fat_body = 368;

void put(Bytes& bytes, std::size_t at, std::uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i)
        bytes.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
}

void write_bytes(fs::path const& path, Bytes const& bytes)
{
    std::ofstream file(path, std::ios::binary);
    if (!file || !file.write(reinterpret_cast<char const*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot write archive fixture");
}

// PC FAT record offsets mirror the serialized MSVC layout in compat.
Bytes fat_fixture(std::string const& scene, bool combined)
{
    Bytes bytes(fat_header + fat_body);
    auto field = [&](std::size_t at, std::uint64_t value, unsigned width) {
        put(bytes, fat_header + at, value, width);
    };
    auto node = [&](std::size_t at, std::size_t parent, std::size_t prev,
        std::size_t next, unsigned flags, std::string const& name) {
        field(at + 8, next, 8);
        field(at + 16, prev, 8);
        field(at + 24, parent, 8);
        field(at + 32, 0x2000 | flags, 2);
        std::copy(name.begin(), name.end(), bytes.begin() + fat_header + at + 34);
    };

    std::copy_n("little-endian", 14, bytes.begin());
    put(bytes, 16, 5, 4);
    put(bytes, 20, fat_body, 4);
    field(0, folder_node, 8);
    node(root_node, 0, 0, 0, 0x8000, "");
    field(folder_node - 8, scene_node, 8);
    node(folder_node, root_node, 0, 0, 0x8000, "level");

    auto const data_start = combined ? 2048 : 0;
    field(scene_node - 16, data_start, 8);
    field(scene_node - 8, scene.size(), 4);
    node(scene_node, folder_node, 0, inline_node, 0, "main.nscene");

    field(inline_node - 24, 3, 4);
    field(inline_node - 20, 0x0b19a653, 4); // "abc", legacy CRC configuration.
    field(inline_node - 16, inline_offset, 8);
    field(inline_node - 8, 3, 4);
    node(inline_node, folder_node, scene_node, empty_node, 0x0400, u8"\u0442.bin");
    std::copy_n("abc", 3, bytes.begin() + fat_header + inline_offset);

    field(empty_node - 16, data_start + scene.size(), 8);
    node(empty_node, folder_node, inline_node, 0, 0, "zero.bin");
    if (combined) {
        bytes.resize(data_start);
        bytes.insert(bytes.end(), scene.begin(), scene.end());
    }
    return bytes;
}

void require(bool condition, char const* detail)
{
    if (!condition)
        throw std::runtime_error(detail);
}

template<class F>
void rejects(F action, char const* error)
{
    try {
        action();
    } catch (std::exception const& e) {
        if (std::string(e.what()).find(error) != std::string::npos)
            return;
        throw;
    }
    throw std::runtime_error(std::string("Expected failure: ") + error);
}

void test_archives(northstarcpp::AssetManager& assets, fs::path const& root, std::string const& scene)
{
    auto const combined = root / "game.db";
    auto const fat = root / "game.fat";
    auto const db = root / "payload.db";
    auto const bad = root / "invalid.db";
    auto const archived_scene = "game/level/main.nscene";
    auto const loose = root / "patch";
    write_bytes(combined, fat_fixture(scene, true));
    write_bytes(fat, fat_fixture(scene, false));
    write(db, scene);
    write(loose / "level/main.nscene", "patch");

    assets.mount_archive("game", combined, combined, 10);
    require(text(assets.read(archived_scene)) == scene, "Cannot read combined FAT/DB");
    require(text(assets.read_range(archived_scene,3,8))==scene.substr(3,8),
            "FAT/DB combined archive range read failed");
    require(assets.read_range(archived_scene,scene.size(),0).empty(),
            "FAT/DB empty EOF range failed");
    rejects([&] {assets.read_range(archived_scene,scene.size()-1,2);},
            "range exceeds");
    require(assets.size(archived_scene) == scene.size(), "Archive size mismatch");
    require(text(assets.read(u8"game/level/\u0442.bin")) == "abc", "Cannot read inline UTF-8 asset");
    require(assets.read("game/level/zero.bin").empty(), "Cannot read empty archive entry");
    rejects([&] { assets.mount_archive("game", combined, combined); }, "already mounted");
    assets.mount_directory("game", loose, 10);
    require(text(assets.read(archived_scene)) == "patch", "Latest mount did not shadow archive");
    assets.mount_archive("game", fat, db, 20);
    require(text(assets.read(archived_scene)) == scene, "Cannot read split FAT/DB");
    require(text(assets.read_range(archived_scene,4,9))==scene.substr(4,9),
            "FAT/DB split archive range read failed");
    require(text(assets.read(u8"game/level/\u0442.bin")) == "abc", "Cannot read split inline asset");
    assets.unmount_archive("game", fat, db);
    require(text(assets.read(archived_scene)) == "patch", "Unmount did not reveal the directory");
    assets.unmount_directory("game", loose);
    require(text(assets.read(archived_scene)) == scene, "Unmount did not reveal the base archive");
    assets.unmount_archive("game", combined, combined);

    auto data = fat_fixture(scene, true);
    put(data, fat_header + scene_node + 8, scene_node, 8);
    write_bytes(bad, data);
    rejects([&] { assets.mount_archive("game", bad, bad); }, "broken node links");
    data = fat_fixture(scene, true);
    put(data, fat_header + scene_node - 16, data.size() + 1, 8);
    write_bytes(bad, data);
    rejects([&] { assets.mount_archive("game", bad, bad); }, "file payload outside DB");
    data = fat_fixture(scene, true);
    data[fat_header + scene_node + 34] = ':';
    write_bytes(bad, data);
    rejects([&] { assets.mount_archive("game", bad, bad); }, "invalid virtual file name");
    data = fat_fixture(scene, true);
    data[fat_header + inline_offset] = 'x';
    write_bytes(bad, data);
    assets.mount_archive("game", bad, bad);
    rejects([&] { assets.read(u8"game/level/\u0442.bin"); }, "checksum mismatch");
    assets.unmount_archive("game", bad, bad);
    rejects([&] { assets.read(archived_scene); }, "Asset not found");

    data = fat_fixture(scene, true);
    // Reinterpret the inline test record as a compressed one. Keep the
    // payload at the same offset and move its file metadata one slot back.
    std::copy_n(data.begin() + fat_header + inline_node - 40, 40,
        data.begin() + fat_header + inline_node - 48);
    put(data, fat_header + inline_node - 8, 12, 4);
    put(data, fat_header + inline_node + 32, 0x2c00, 2);
    write_bytes(bad, data);
    assets.mount_archive("game", bad, bad);
    require(text(assets.read(archived_scene)) == scene, "Unsupported compression hid other archive entries");
    rejects([&] { assets.read(u8"game/level/\u0442.bin"); }, "PPMd-compressed");
    assets.unmount_archive("game", bad, bad);
}

void test_chunked_archive(northstarcpp::AssetStore& assets, fs::path const& root)
{
    using namespace northstarcpp;
    Scene world;
    world.version=3;
    world.name="Archived Streaming Sector";
    world.cells.push_back({"Sector",{-10.f,-5.f,-10.f},{10.f,20.f,10.f},false});
    SceneObject object;
    object.name="Archived Entity";
    object.node.id=1;
    object.node.cell="Sector";
    world.objects.push_back(object);
    auto const binary=serialize_scene_binary(world);
    require(binary.substr(0,4)=="NEF8","Test fixture did not encode NEF8");
    auto const archive=root/"nef8_chunked.db";
    write_bytes(archive,fat_fixture(binary,true));
    assets.mount_archive("nef8",archive,archive);
    SceneChunkReader index(assets,"nef8/level/main.nscene");
    require(index.native_range_reads(),"Archive lacks native ABI 3 positioned reads");
    auto const chunks=index.chunks();
    require(chunks.size()==2 && chunks[1].cell=="Sector",
            "Archive NEF8 cell index mismatch");
    auto const records=index.load("Sector");
    require(records.size()==1 && records[0].entity_id==1,
            "Cannot selectively decode archived NEF8 streaming cell");
    assets.unmount_archive("nef8",archive,archive);
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    try {
        fs::path root(argv[1]);
        fs::remove_all(root);
        fs::create_directories(root);
        fs::path low = root / "low";
        fs::path high = root / "high";
        fs::path hidden = root / "hidden";
        write(low / "level" / "test.txt", "base");
        write(high / "level" / "test.txt", "override");
        write(hidden / "private.txt", "secret");
        northstarcpp::AssetStore assets;
        if (assets.provider_name() != "Directory VFS") return 3;
        assets.mount_directory("game", low, 10);
        if (text(assets.read("game/level/test.txt")) != "base") return 4;
        require(assets.native_range_reads(),"Directory VFS ABI 3 range provider missing");
        require(text(assets.read_range("game/level/test.txt",1,2))=="as",
                "Directory ranged read failed");
        rejects([&] {assets.read_range("game/level/test.txt",3,2);},"range exceeds");
        assets.mount_directory("game", high, 20);
        if (text(assets.read("game/level/test.txt")) != "override") return 5;
        assets.unmount_directory("game", high);
        if (text(assets.read("game/level/test.txt")) != "base") return 6;
        for (auto const* invalid : {"../hidden/private.txt", "game/../../hidden/private.txt",
                                    "C:/Windows/system.ini", "/hidden/private.txt",
                                    "game//level/test.txt", "game/./level/test.txt"}) {
            try {
                assets.read(invalid);
                std::cerr << "Unsafe asset path accepted: " << invalid << '\n';
                return 7;
            } catch (std::runtime_error const&) {}
        }
        try { assets.read("game/nonexistent.txt"); return 8; }
        catch (std::runtime_error const&) {}
        std::ifstream input(argv[2], std::ios::binary);
        std::string scene((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        require(!scene.empty(), "Cannot read scene fixture");
        test_archives(assets, root, scene);
        test_chunked_archive(assets,root);
        // One virtual namespace: project/assets shadows sharedAssets, while
        // scripts/main.js is read from the project root and bootstrap from shared.
        auto const common=root/"sharedAssets";
        auto const project=root/"MyGame";
        write(common/"textures/ui.txt","engine-texture");
        write(common/"models/fallback.txt","engine-model");
        write(common/"scripts/northstar/bootstrap.js","engine-bootstrap");
        write(project/"assets/textures/ui.txt","project-texture");
        write(project/"scripts/main.js","project-entry");
        northstarcpp::AssetStore layered;
        layered.mount_project_assets(project,common);
        require(text(layered.read("textures/ui.txt"))=="project-texture",
            "Project assets failed to override sharedAssets");
        require(text(layered.read("models/fallback.txt"))=="engine-model",
            "Missing engine asset fallback");
        require(text(layered.read("scripts/northstar/bootstrap.js"))=="engine-bootstrap",
            "Missing shared root script");
        require(text(layered.read("scripts/main.js"))=="project-entry",
            "Missing project script");
        write(common/"models/player.ydd", "engine-player-model");
        require(text(layered.read("models/player.ydd"))=="engine-player-model",
            "Project missing model did not fall back to sharedAssets");
        write(project/"assets/models/player.ydd", "project-player-model");
        // New content must be visible after VFS remount (mounts are invalidated
        // by design, while file creation without remount may use provider caches).
        layered.unmount_directory("",project/"assets");
        layered.mount_directory("",project/"assets",100);
        require(text(layered.read("models/player.ydd"))=="project-player-model",
            "Project models/player.ydd did not shadow sharedAssets");
        northstarcpp::AssetStore alias_mount;
        alias_mount.mount_project_assets(project,project/"assets");
        require(text(alias_mount.read("models/player.ydd"))=="project-player-model",
            "Shared-assets alias of project assets cannot be mounted");
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 9;
    }
}
