#include "../engine/northstarcpp/modules/viewport/viewport_model_preloader.hpp"
#include <northstarcpp/assets.hpp>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <thread>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        northstarcpp::AssetStore store;
        store.mount_project_assets(argv[1],{});
        using northstarcpp::viewport_detail::NativeModelPreloader;
        NativeModelPreloader preloader([&](std::string const& key) {
            return northstarcpp::load_native_model_resource(store,key,true);
        });
        constexpr char source[]=
            "models/mahir_mt28/v_mahir_mt28_coach__ext01_body_01.ydr";
        if(!preloader.enqueue(source,true))return 3;
        std::optional<northstarcpp::NativeModelResource> decoded;
        std::exception_ptr error;
        auto const start=std::chrono::steady_clock::now();
        for(int attempt=0;attempt<3000;++attempt) {
            auto const status=preloader.take(source,decoded,error);
            if(status==NativeModelPreloader::Status::failed) {
                std::rethrow_exception(error);
            }
            if(status==NativeModelPreloader::Status::ready) {
                if(!decoded || decoded->mesh.indices.empty() ||
                   decoded->mesh.vertices.empty())return 4;
                auto const millis=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count();
                std::printf("Original MT28 body prefetch: %zu indices, %zu "
                    "vertices, %zu material definitions, %.1f ms PASS\n",
                    decoded->mesh.indices.size(),decoded->mesh.vertices.size(),
                    decoded->materials.size(),millis);
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::fprintf(stderr,"MT28 model async decode exceeded 30s test deadline\n");
        return 5;
    } catch(std::exception const& error) {
        std::fprintf(stderr,"MT28 preload: %s\n",error.what());
        return 6;
    }
}
