#include <northstarcpp/player_character_components.hpp>
#include "editor_character_marker.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace northstarcpp;
int main() {
    Scene scene;
    scene.version=3;
    SceneObject actor;
    actor.name="Player";
    // Pawn is represented by its scene component, authored below.
    actor.mass=80.f;
    actor.size=1.8f;
    actor.node.id=101;
    scene.objects.push_back(actor);

    player_character::add_character_components(scene,101,true);
    assert(scene.components.size()==4);
    assert(player_character::has_pawn(scene,101));
    auto collision=collision_authoring::lookup(scene,101);
    assert(collision.has_value());
    assert(collision->shape==collision_authoring::Shape::Capsule);
    assert(collision->object_type==collision_authoring::ObjectType::Pawn);
    assert(collision->dimensions.x==.65f);
    assert(collision->dimensions.y==1.8f);

    for(auto const& component:scene.components) {
        if(component.type==player_character::pawn_type)
            assert(player_character::detail::pawn_valid(nullptr,
                component.version,component.payload.data(),component.payload.size()));
        if(component.type==player_character::motor_type)
            assert(player_character::detail::motor_valid(nullptr,
                component.version,component.payload.data(),component.payload.size()));
        if(component.type==player_character::camera_type)
            assert(player_character::detail::camera_valid(nullptr,
                component.version,component.payload.data(),component.payload.size()));
    }
    auto before=scene.components;
    player_character::add_character_components(scene,101,true);
    assert(scene.components.size()==before.size());
    for(std::size_t i=0;i<before.size();++i) {
        assert(scene.components[i].type==before[i].type);
        assert(scene.components[i].payload==before[i].payload);
    }
    auto motor=scene.components[1];
    assert(motor.type==player_character::motor_type);
    // Corrupt run speed below walk speed: reject, never propagate invalid tuning.
    player_character::detail::pushfloat(motor.payload,1.f);
    motor.payload.resize(24);
    std::string invalid;
    player_character::detail::pushfloat(invalid,1.f);
    motor.payload.replace(4,4,invalid);
    assert(!player_character::detail::motor_valid(nullptr,1,
        motor.payload.data(),motor.payload.size()));

    bool threw=false;
    try{player_character::add_character_components(scene,999,true);}
    catch(std::invalid_argument const&){threw=true;}
    assert(threw);
    std::vector<NorthStarRenderTriangleVertex> vertices;
    assert(editor_character_marker::append(vertices,scene.objects[0],true));
    assert(vertices.size()==editor_character_marker::vertices_per_marker);
    for(auto const& v:vertices)
        assert(v.y>=scene.objects[0].position.y-.01f &&
               v.y<=scene.objects[0].position.y+3.f);
    assert(!editor_character_marker::append(vertices,scene.objects[0],
       false,vertices.size()+5));
    std::cout<<"Character components + capsule marker: PASS\n";
}
