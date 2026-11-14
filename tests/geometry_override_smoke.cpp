#include <northstarcpp/geometry_service.hpp>
#include <iostream>
#include <string_view>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::string_view mode=argv[1];
    try {
        northstarcpp::GeometryService geometry;
        auto const mesh=geometry.solid(0);
        if(mode=="builtin")return
            geometry.provider_name()=="NorthStar Geometry"&&
            mesh.xyz.size()==24&&mesh.indices.size()==36?0:1;
        if(mode=="external")return
            geometry.provider_name()=="Fixture Geometry"&&
            mesh.xyz.size()==9&&mesh.xyz[0]==42.f&&mesh.indices.size()==3?0:1;
    }catch(std::exception const& ex) {
        std::cerr<<ex.what()<<'\n';
        if(mode=="reject" && std::string_view(ex.what()).find("engine.geometry")!=std::string_view::npos)
            return 0;
    }
    return 1;
}
