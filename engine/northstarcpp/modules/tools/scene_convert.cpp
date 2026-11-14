// NorthStarCpp legacy/NSCN scene -> NEF8 ListFile migration utility.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        namespace fs=std::filesystem;
        if (argc!=3) {
            std::cerr<<"Usage: northstarcpp_scene_convert INPUT.nscene OUTPUT.nscn\n";
            return 2;
        }
        fs::path from=fs::u8path(argv[1]);
        fs::path to=fs::u8path(argv[2]);
        if (to.extension()!=".nscn" ||
            fs::weakly_canonical(from)==fs::weakly_canonical(to))
            throw std::invalid_argument("Output must be a different .nscn file");
        if (fs::exists(to))
            throw std::invalid_argument("Refusing to overwrite existing binary scene");
        auto const source=northstarcpp::load_scene(from);
        northstarcpp::save_scene(source,to);
        auto const result=northstarcpp::load_scene(to);
        if (result.version!=3 || result.objects.size()!=source.objects.size())
            throw std::runtime_error("Scene migration verification failed");
        std::cout<<"Converted "<<result.objects.size()<<" objects into NEF8 Scene ListFile v2\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"Scene conversion error: "<<e.what()<<"\n";
        return 1;
    }
}
