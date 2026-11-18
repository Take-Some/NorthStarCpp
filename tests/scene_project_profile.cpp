// Offline 1000+ frame SceneWorld baseline for a real NEF8 project scene.
// This tool does NOT create a renderer or report GPU/VRAM measurements.
#include <northstarcpp/scene_world.hpp>
#include "scene_project_profile_tiled.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    try {
        if(argc<3 || argc>5) {
            std::cerr<<"usage: scene_project_profile <scene.nscn> <frames>=1000 [output.csv]\n";
            return 2;
        }
        auto const frames=std::stoul(argv[2]);
        if(frames<1000 || frames>60000)
            throw std::invalid_argument("frames must be 1000..60000");
        auto scene=northstarcpp::load_scene(std::filesystem::u8path(argv[1]));
        if(argc==5)
            scene=northstarcpp::tests::tiled_stress(scene,std::stoul(argv[4]));
        northstarcpp::SceneWorld world(std::move(scene));
        auto const& authored=world.source();
        northstarcpp::Vector3 begin{0.f,0.f,0.f},end{0.f,0.f,0.f};
        if(!authored.cells.empty()) {
            auto const& a=authored.cells.front();
            auto const& b=authored.cells.back();
            begin={(a.minimum.x+a.maximum.x)*.5f,
                   (a.minimum.y+a.maximum.y)*.5f,
                   (a.minimum.z+a.maximum.z)*.5f};
            end={(b.minimum.x+b.maximum.x)*.5f,
                 (b.minimum.y+b.maximum.y)*.5f,
                 (b.minimum.z+b.maximum.z)*.5f};
        }else if(!authored.objects.empty()) {
            begin=authored.objects.front().position;
            end=authored.objects.back().position;
        }
        for(unsigned frame=0;frame<frames;++frame) {
            float const phase=float(frame)/float(frames-1);
            float const factor=.5f-.5f*std::cos(phase*6.283185307179586f);
            northstarcpp::Vector3 const camera{
                begin.x+(end.x-begin.x)*factor,
                begin.y+(end.y-begin.y)*factor,
                begin.z+(end.z-begin.z)*factor};
            world.update(camera);
        }
        auto const summary=world.cost_profiler().summary();
        std::cout<<"scene="<<authored.name<<"\n"
                 <<"authored_entities="<<authored.objects.size()<<"\n"
                 <<"streaming_cells="<<authored.cells.size()<<"\n"
                 <<"portals="<<authored.portals.size()<<"\n"
                 <<"HLOD_clusters="<<world.hlod_cluster_count()<<"\n"
                 <<"frames_run="<<frames<<"\n"
                 <<"frames_retained="<<summary.cpu_us.samples<<"\n"
                 <<"scene_cpu_p50_us="<<summary.cpu_us.p50<<"\n"
                 <<"scene_cpu_p95_us="<<summary.cpu_us.p95<<"\n"
                 <<"scene_cpu_p99_us="<<summary.cpu_us.p99<<"\n"
                 <<"gpu_samples="<<summary.gpu_us.samples<<" (not measured in offline runner)\n";
        if(argc>=4) {
            std::ofstream csv(std::filesystem::u8path(argv[3]),
                              std::ios::binary|std::ios::trunc);
            if(!csv)throw std::runtime_error("failed to open CSV output");
            world.cost_profiler().write_csv(csv);
            if(!csv)throw std::runtime_error("CSV write failed");
        }
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"scene_project_profile: "<<e.what()<<"\n";
        return 1;
    }
}
