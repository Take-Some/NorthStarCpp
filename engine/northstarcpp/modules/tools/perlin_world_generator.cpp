// Native NorthStarCpp procedural world authoring CLI, using engine.perlin.
// Usage: perlin_world_generator.exe <destination.nscn> [seed]
#include <northstarcpp/procedural_environment.hpp>
#include <northstarcpp/scene.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    if (argc<2 || argc>3) {
        std::cerr<<"Usage: perlin_world_generator.exe FILE.nscn [unsigned-seed]\n";
        return 2;
    }
    try {
        std::filesystem::path path=std::filesystem::u8path(argv[1]);
        if (path.extension()!=".nscn")
            throw std::invalid_argument("Output must be an .nscn file");
        northstarcpp::PerlinWorldSettings config;
        if(argc==3) {
            std::string raw=argv[2];
            if(raw.empty() || raw.find_first_not_of("0123456789")!=std::string::npos)
                throw std::invalid_argument("Seed must be a decimal unsigned integer");
            std::size_t end=0;
            config.seed=std::stoull(raw,&end);
            if(end!=raw.size()) throw std::invalid_argument("Invalid seed");
        }
        auto scene=northstarcpp::make_perlin_world(config);
        northstarcpp::save_scene(scene,path);
        std::cout<<"Wrote "<<path.u8string()<<": "<<scene.objects.size()
                 <<" entities, "<<scene.cells.size()<<" streaming cells, seed "
                 <<config.seed<<"\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"Perlin world generation failed: "<<e.what()<<'\n';
        return 1;
    }
}
