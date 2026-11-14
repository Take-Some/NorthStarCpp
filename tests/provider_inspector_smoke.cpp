// Verify that the Editor's provider diagnostics use the same ABI resolver
// as runtime services, including an author-supplied DLL override.
#include <northstarcpp/provider_inspector.hpp>
#include <northstarcpp/module.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const bool external=std::string_view(argv[1])=="external";
    try {
        const auto providers=northstarcpp::inspect_engine_providers();
        auto find=[&](const char* capability) {
            return std::find_if(providers.begin(),providers.end(),[&](auto const& entry) {
                return entry.capability==capability && entry.requested_provider.empty();
            });
        };
        if(providers.size()<11)
            throw std::runtime_error("Engine API registry is incomplete");
        for(const char* capability:{
            NORTHSTARCPP_CAPABILITY_GEOMETRY,
            NORTHSTARCPP_CAPABILITY_COLLISION,
            NORTHSTARCPP_CAPABILITY_PERLIN,
            NORTHSTARCPP_CAPABILITY_TIME
        }) {
            const auto match=find(capability);
            if(match==providers.end()||!match->available||match->api_abi==0)
                throw std::runtime_error(std::string("Unavailable builtin ABI: ")+capability);
        }
        const auto geometry=find(NORTHSTARCPP_CAPABILITY_GEOMETRY);
        if(external) {
            if(!geometry->external||geometry->provider!="Fixture Geometry"||
               geometry->source.empty()||geometry->source=="builtin")
                throw std::runtime_error("Explicit geometry DLL was not selected");
        } else {
            if(geometry->external||geometry->source!="builtin"||
               geometry->provider!="NorthStar Geometry")
                throw std::runtime_error("Builtin geometry resolution failed");
        }
        std::printf("Provider inspector %s: %zu entries; geometry=%s, ABI=%u\n",
            external?"external":"builtin",providers.size(),
            geometry->provider.c_str(),geometry->api_abi);
        return 0;
    } catch(std::exception const& error) {
        std::fprintf(stderr,"Provider inspector regression: %s\n",error.what());
        return 1;
    }
}
