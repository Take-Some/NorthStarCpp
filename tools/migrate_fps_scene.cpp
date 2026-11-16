// NorthStar scene conversion; preserve original for rollback.
#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/player_controller.hpp>
#include <iostream>
#include <filesystem>
#include <cmath>
int main(int argc, char** argv) {
    if(argc!=3 && argc!=4)return 2;
    try {
        auto old=std::filesystem::u8path(argv[1]);
        auto output=std::filesystem::u8path(argv[2]);
        if(old==output)return 3;
        auto scene=northstarcpp::load_scene(old);
        auto pawn=northstarcpp::scene_pawn::local_index(scene);
        if(!pawn)return 4;
        if(argc==4) {
            float const y=std::stof(argv[3]);
            if(!std::isfinite(y) || y<0.f || y>1000.f)return 7;
            scene.objects[*pawn].position.y=y;
        }
        northstarcpp::gameplay::PlayerController controller;
        if(!controller.auto_possess(scene))return 5;
        northstarcpp::save_scene(scene,output);
        auto after=northstarcpp::load_scene(output);
        auto restored=northstarcpp::scene_pawn::local_index(after);
        if(!restored ||
           after.objects[*restored].node.id!=controller.controlled_entity() ||
           after.objects.size()!=scene.objects.size())return 6;
        std::cout<<"Migrated "<<after.objects.size()<<" entities; Pawn id="
                 <<controller.controlled_entity()<<std::endl;
        return 0;
    }catch(std::exception const& e){
        std::cerr<<e.what()<<std::endl;
        return 1;
    }
}
