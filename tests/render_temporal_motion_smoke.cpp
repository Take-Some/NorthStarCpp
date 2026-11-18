#include "../engine/northstarcpp/modules/render/shared/temporal_motion_vectors.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
int main() {
    using namespace northstarcpp::render;
    TemporalCamera camera{};camera.width=640;camera.height=360;
    TemporalHistoryTracker tracker;
    TemporalJitterSequence jitter;
    auto first=tracker.begin(camera);
    auto first_jitter=jitter.next(first);
    assert(!first.history_valid);
    assert(first_jitter.current==first_jitter.previous);
    auto stable=tracker.begin(camera);
    auto active_jitter=jitter.next(stable);
    assert(active_jitter.current!=active_jitter.previous);
    for(auto value:active_jitter.current)
        assert(value>=-.5f&&value<=.5f);
    std::array<float,2> v{};
    const TemporalJitter zero{};
    assert(temporal_vertex_pixel_velocity(
        stable,{0.f,0.f,10.f},{0.f,0.f,10.f},zero,v));
    assert(std::abs(v[0])<.001f&&std::abs(v[1])<.001f);
    assert(temporal_vertex_pixel_velocity(
        stable,{1.f,0.f,10.f},{0.f,0.f,10.f},zero,v));
    assert(v[0]<-20.f&&std::abs(v[1])<.001f);
    assert(temporal_vertex_pixel_velocity(
        stable,{0.f,0.f,10.f},{0.f,0.f,10.f},active_jitter,v));
    assert(std::abs(v[0])<1.f&&std::abs(v[1])<1.f);
    std::array<float,16> previous{};
    previous[0]=previous[5]=previous[10]=previous[15]=1.f;
    auto current=previous;current[12]=1.f;
    std::array<std::uint16_t,8> ids{};
    std::array<float,8> weights{};weights[0]=1.f;
    std::array<float,3> world{};
    assert(temporal_skin_world_position(
        {0.f,0.f,10.f},ids,weights,current.data(),1u,world));
    assert(std::abs(world[0]-1.f)<.001f);
    assert(temporal_skinned_pixel_velocity(stable,
        {0.f,0.f,10.f},ids,weights,current.data(),
        previous.data(),1u,zero,v));
    assert(v[0]<-20.f);
    assert(!temporal_skinned_pixel_velocity(stable,
        {0.f,0.f,10.f},ids,weights,current.data(),nullptr,1u,zero,v));
    weights[0]=-1.f;
    assert(!temporal_skin_world_position(
        {0.f,0.f,10.f},ids,weights,current.data(),1u,world));
    weights[0]=1.f;ids[0]=8u;
    assert(!temporal_skin_world_position(
        {0.f,0.f,10.f},ids,weights,current.data(),1u,world));
    ids[0]=0u;
    auto teleport=camera;teleport.position[0]=1000.f;
    auto cut=tracker.begin(teleport);
    auto cut_jitter=jitter.next(cut);
    assert(!cut.history_valid);
    assert(cut_jitter.current==cut_jitter.previous);
    assert(!temporal_skinned_pixel_velocity(cut,
        {0.f,0.f,10.f},ids,weights,current.data(),
        previous.data(),1u,cut_jitter,v));
    return 0;
}
