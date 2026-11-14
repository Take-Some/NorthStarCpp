// Coarse navigation cell lifecycle follows verified NEF8 and WorldPartition.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_navigation_cells.hpp>
#include <northstarcpp/scene_world.hpp>
#include <stdexcept>
int main() {
    using namespace northstarcpp;
    Scene scene;
    scene.version=3;
    scene.streaming={25.f,40.f,0.f,4,4};
    scene.cells={
        {"A",{0.f,-2.f,0.f},{10.f,2.f,10.f},false},
        {"B",{10.f,-2.f,0.f},{20.f,2.f,10.f},false},
        {"C",{20.f,-2.f,0.f},{30.f,2.f,10.f},false},
        {"D",{100.f,-2.f,100.f},{110.f,2.f,110.f},false}};
    SceneObject player;
    player.name="Driver";player.node.id=1;player.player=true;
    player.mass=1.f;
    scene.objects.push_back(player);
    SceneWorld world(scene);
    SceneNavigationCells navigation(scene);
    world.enable_payload_gate(true);
    world.update({10.f,0.f,0.f});
    navigation.sync(world);
    if(navigation.resident_count()!=0 || !navigation.route("A","B").empty())
        return 1;
    for(auto const& name:{"A","B","C"})
        world.set_cell_payload_ready(name,true);
    world.update({10.f,0.f,0.f});
    navigation.sync(world);
    if(navigation.resident_count()!=3)return 2;
    if(navigation.route("A","C")!=std::vector<std::string>{"A","B","C"})
        return 3;
    if(!navigation.route("A","D").empty())return 4;
    world.update({105.f,0.f,105.f});
    world.set_cell_payload_ready("D",true);
    world.update({105.f,0.f,105.f});
    navigation.sync(world);
    if(navigation.resident_count()!=1 || !navigation.resident("D") ||
       !navigation.route("A","B").empty())
        return 5;
    world.update({10.f,0.f,0.f});
    navigation.sync(world);
    if(navigation.resident_count()!=3 ||
       navigation.route("C","A")!=std::vector<std::string>{"C","B","A"})
        return 6;
    return 0;
}
