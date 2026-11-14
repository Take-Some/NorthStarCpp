// Real archive-to-V8 integration: standard engine.assets FAT/DB VFS.
#include <northstarcpp/assets.hpp>
#include <northstarcpp/scripting.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        northstarcpp::AssetStore store;
        store.mount_archive("", argv[1], argv[1]);
        auto const scene=store.read("level/main.nscene");
        if (std::string(scene.begin(),scene.end()).find("NorthStarCpp.Scene 2")!=0)
            throw std::runtime_error("Cannot read archived game scene");
        bool started=false;
        northstarcpp::ScriptRuntime runtime;
        runtime.bind("engine.logger","info",[&started](std::string_view json) {
            if (json != "[\"archive loaded\"]")
                throw std::runtime_error("Bad archived JS logger callback");
            started=true;
            return std::string("null");
        });
        auto const loaded=runtime.evaluate_asset(store,"scripts/main.js");
        if (loaded != "7") throw std::runtime_error("Archived main.js returned " + loaded);
        runtime.emit_event("start");
        if(!started) throw std::runtime_error("Archived startup event wasn't dispatched");
        std::cout << "archive engine.assets + V8 PASS\n";
    } catch (std::exception const& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
