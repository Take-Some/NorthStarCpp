// One-way scene authoring conversion: text Scene V3 -> canonical NEF8 NSCN.
#include <northstarcpp/scene.hpp>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument(
            "Usage: northstarcpp_scene_convert authored.scene.v3 output.nscn");
        std::ifstream input(std::filesystem::u8path(argv[1]),std::ios::binary);
        if(!input)throw std::runtime_error("Cannot open source V3 scene");
        std::string const source((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
        auto const scene=northstarcpp::parse_scene_v3(source);
        if(scene.version!=3)throw std::runtime_error("Not a Scene V3");
        northstarcpp::save_scene(scene,std::filesystem::u8path(argv[2]));
        auto const check=northstarcpp::load_scene(std::filesystem::u8path(argv[2]));
        if(check.objects.size()!=scene.objects.size() || check.version!=3)
            throw std::runtime_error("NEF8 conversion round-trip mismatch");
        std::cout<<"NorthStarCpp Scene V3 -> NEF8 PASS: "<<scene.objects.size()
                 <<" entities"<<std::endl;
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Scene conversion failed: "<<e.what()<<std::endl;
        return 2;
    }
}
