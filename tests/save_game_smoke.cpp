// Native C++ smoke/regression for project save slots.
#include <northstarcpp/save_game.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    namespace fs = std::filesystem;
    auto const root = fs::absolute(fs::u8path(argv[1]));
    std::error_code ec;
    fs::remove_all(root, ec);
    northstarcpp::SaveGameStore store(root);
    assert(!store.exists("manual_1"));
    assert(!store.read("manual_1").has_value());
    assert(!store.erase("manual_1"));
    store.write("manual_1", R"({"level":1,"player":{"health":70},"items":["a","b"]})");
    assert(store.exists("manual_1"));
    assert(store.read("manual_1") ==
           R"({"level":1,"player":{"health":70},"items":["a","b"]})");
    store.write("manual_1", R"({"level":2,"message":"Привет"})");
    assert(store.read("manual_1") == R"({"level":2,"message":"Привет"})");
    // A failed overwrite must preserve a previously valid save.
    try {
        store.write("manual_1", "{not json}");
        assert(false);
    } catch (std::invalid_argument const&) {}
    assert(store.read("manual_1") == R"({"level":2,"message":"Привет"})");
    for (auto const& slot : {"../escape","..","a/b","a\\b",".hidden","x y",""})
        try {
            store.write(slot, "{}");
            assert(false);
        } catch (std::invalid_argument const&) {}
    try {
        store.write("oversize", std::string(northstarcpp::SaveGameStore::max_payload_bytes+1,'x'));
        assert(false);
    } catch (std::invalid_argument const&) {}
    {
        std::ofstream file(root/"manual_1.nsave", std::ios::binary|std::ios::trunc);
        file << "Corrupted";
    }
    try {
        (void)store.read("manual_1");
        assert(false);
    } catch (std::runtime_error const&) {}
    assert(store.erase("manual_1"));
    assert(!store.exists("manual_1"));
    fs::remove_all(root,ec);
    return 0;
}
