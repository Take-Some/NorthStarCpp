// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/scene.hpp>

#include <fstream>
#include <limits>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    using namespace northstarcpp;
    namespace fs = std::filesystem;
    fs::path const folder(argv[1]);
    fs::create_directories(folder);
    auto const path = folder / fs::path(u8"NorthStar \u0421\u0446\u0435\u043d\u0430.nscn");

    auto scene = make_startup_scene();
    scene.objects[0].name = u8"Player \"\u041a\u0430\u0439\u043b\u0430\"";
    save_scene(scene, path);
    auto reopened = load_scene(path);
    if (reopened.objects.size() != scene.objects.size() ||
        reopened.objects[0].name != scene.objects[0].name || !reopened.objects[0].player ||
        reopened.objects[0].mass != 2.f || reopened.objects[1].mass != 0.f || reopened.objects[2].mass != 1.f)
        return 2;

    reopened.objects[0].position.x = 2.f;
    auto const& position = reopened.objects[0].position;

    reopened.objects[1].player = true;
    bool rejected = false;
    try {
        save_scene(reopened, path);
    } catch (std::runtime_error const&) {
        rejected = true;
    }
    if (!rejected || load_scene(path).objects[0].position.x != 0.f)
        return 4;

    reopened.objects[1].player = false;
    reopened.objects[1].size = std::numeric_limits<float>::infinity();
    try {
        validate_scene(reopened);
        return 5;
    } catch (std::runtime_error const&) {
    }
    reopened.objects[1].size = 1.f;
    save_scene(reopened, path);
    if (load_scene(path).objects[0].position.x != position.x)
        return 6;

    {
        std::ofstream legacy(path, std::ios::binary);
        legacy << "NorthStarCpp.Scene 1\r\ncube \"Player\" 0 0 0 1 46b9d8 1\r\n"
               << "cube \"Obstacle\" 0 0 2 1 ffffff 0\r\n";
    }
    auto const legacy = load_scene(path);
    if (legacy.objects[0].mass != 1.f || legacy.objects[1].mass != 0.f)
        return 3;
    {
        std::ofstream malformed(path);
        malformed << "NorthStarCpp.Scene 1\ncube \"Incomplete\" 1 2\n";
    }
    try {
        load_scene(path);
        return 7;
    } catch (std::runtime_error const&) {
    }
    fs::remove(path);
    return 0;
}
