// Real Jolt simulation with typed Pawn ownership; no GPU or UI required.
#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene.hpp>
#include <iostream>
#include <stdexcept>
#include <cmath>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto scene=northstarcpp::load_scene(std::filesystem::u8path(argv[1]));
        northstarcpp::PhysicsScene runtime(scene);
        if(runtime.provider_name()!="Jolt")
            throw std::runtime_error("Expected Jolt provider for Player Framework");
        auto const id=runtime.controlled_entity();
        auto const index=runtime.controlled_index();
        if(!id || !index || *index>=runtime.scene().objects.size())
            throw std::runtime_error("No active stable Pawn entity");
        auto const start=runtime.scene().objects[*index].position;
        runtime.set_sprinting(true);
        for(int frame=0;frame<120;++frame)runtime.step(1.f/60.f,1.f,0.f);
        auto const after=runtime.scene().objects[*index].position;
        if(!std::isfinite(after.x) || !std::isfinite(after.y) ||
           after.x <= start.x+.1f)
            throw std::runtime_error("Jolt Character Motor failed to move Pawn");
        runtime.unpossess();
        if(runtime.controlled_entity() || runtime.controlled_index())
            throw std::runtime_error("Unpossess left stale ownership");
        runtime.possess(id);
        if(runtime.controlled_entity()!=id || runtime.controlled_index()!=index)
            throw std::runtime_error("Repossess failed to retain stable Entity ID");
        runtime.set_sprinting(false);
        runtime.step(1.f/60.f,0.f,0.f);
        std::cout<<"Player Framework Jolt: PASS pawn="<<id
                 <<" movedX="<<(after.x-start.x)<<"\n";
        return 0;
    } catch(std::exception const& error) {
        std::cerr<<"Player Framework Jolt: FAIL: "<<error.what()<<"\n";
        return 1;
    }
}
