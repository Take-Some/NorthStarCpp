// Project Browser -> engine Module loader provider-selection contract.
#include "../engine/northstarcpp/modules/host/module_loader.hpp"
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/provider_identity.hpp>
#include <northstarcpp/physics_plugin.h>
#include <northstarcpp/assets_plugin.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const std::string capability=argv[1],expected=argv[2];
    try {
        northstarcpp::Module module(capability);
        if(capability==NORTHSTARCPP_CAPABILITY_RENDER)
            (void)module.query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
        else if(capability==NORTHSTARCPP_CAPABILITY_PHYSICS)
            (void)module.query<NorthStarPhysicsApi>(NORTHSTARCPP_PHYSICS_ABI);
        else if(capability==NORTHSTARCPP_CAPABILITY_ASSETS)
            (void)module.query<NorthStarAssetsApi>(NORTHSTARCPP_ASSETS_ABI);
        else return 3;
        if(expected=="!unavailable") {
            std::cerr<<"Unexpectedly loaded provider "<<module.provider()<<"\n";
            return 4;
        }
        auto provider=northstarcpp::canonical_provider_id(module.provider(),capability);
        if(provider!=expected)return 5;
        std::cout<<"Module "<<capability<<" selected "<<provider
                 <<" via "<<module.source()<<"\n";
        return 0;
    }catch(std::exception const& error) {
        if(expected=="!unavailable") {
            std::cout<<"Expected explicit selection rejected: "<<error.what()<<"\n";
            return 0;
        }
        std::cerr<<"Provider loader failed: "<<error.what()<<"\n";
        return 6;
    }
}
