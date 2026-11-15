// Runtime asset graph regression for move-only assembly of decoded
// compressed textures/materials from temporary dictionaries.
#include <northstarcpp/assets.hpp>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::cerr<<"probe: initialize VFS\n";
        northstarcpp::AssetManager assets;
        assets.mount_shared_assets(argv[1]);
        std::cerr<<"probe: resolve material graph\n";
        auto resources=assets.load_drawable_resources(
            "models/skydome.ydd@skydome_high");
        if(resources.drawable.name!="skydome_high" ||
           resources.drawable.meshes.empty() ||
           resources.materials.empty() || resources.textures.empty())
            throw std::runtime_error("Incomplete native drawable dependency graph");
        std::set<std::string> refs;
        std::size_t total_compressed=0;
        for(auto const& reference:resources.textures) {
            if(!refs.insert(reference.reference).second ||
               reference.asset.name.empty() || reference.asset.data.empty() ||
               reference.asset.mips.empty())
                throw std::runtime_error("Missing, duplicated or damaged native texture");
            total_compressed+=reference.asset.data.size();
        }
        for(auto const& ref:resources.materials) {
            if(ref.reference.empty() || ref.asset.name.empty())
                throw std::runtime_error("Material moved-from data leaked to result");
        }
        std::cout<<"DrawableResources: "<<resources.materials.size()
                 <<" materials, "<<resources.textures.size()
                 <<" unique textures, "<<total_compressed
                 <<" compressed bytes; PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Drawable resource assembly: "<<e.what()<<'\n';
        return 1;
    }
}
