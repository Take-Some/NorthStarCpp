#include <northstarcpp/audio_plugin.h>
#include <northstarcpp/physics_plugin.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {

char const* error() { return "Capability fixture rejected the operation"; }

struct Physics {
    std::vector<NorthStarRigidBody> bodies;
};

void* create_physics(NorthStarRigidBody const* bodies, uint32_t count)
{
    try {
        auto state = std::make_unique<Physics>();
        if (count)
            state->bodies.assign(bodies, bodies + count);
        return state.release();
    } catch (...) {
        return nullptr;
    }
}

void destroy_physics(void* state) { delete static_cast<Physics*>(state); }

int step_physics(void* state, float seconds, float input_x, float input_z)
{
    for (auto& body : static_cast<Physics*>(state)->bodies) {
        if (body.mass > 0)
            body.y = std::max(0.f, body.y - seconds * 3.f);
        if (body.player) {
            body.x += input_x * seconds * 2.f;
            body.z += input_z * seconds * 2.f;
        }
    }
    return 1;
}

int read_physics(void* state, NorthStarBodyPose* poses, uint32_t count)
{
    auto const& bodies = static_cast<Physics*>(state)->bodies;
    if (bodies.size() != count)
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        auto const& body = bodies[i];
        poses[i] = {body.x, body.y, body.z, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    }
    return 1;
}

void* create_audio() { return new (std::nothrow) bool(false); }
void destroy_audio(void* state) { delete static_cast<bool*>(state); }
void stop_audio(void* state) { *static_cast<bool*>(state) = false; }
int play_audio(void* state, NorthStarPcm16 const* pcm)
{
    if (!pcm || !pcm->samples || pcm->frames == 0 || pcm->sample_rate != 24000 ||
        pcm->channels != 1 || pcm->samples[0] != 73)
        return 0;
    *static_cast<bool*>(state) = true;
    return 1;
}

int jump_physics(void*) { return 1; }
int move_static_physics(void* state, uint32_t index, float x, float y, float z)
{
    auto& objects = static_cast<Physics*>(state)->bodies;
    if (index >= objects.size() || objects[index].mass != 0.f) return 0;
    objects[index].x = x;
    objects[index].y = y;
    objects[index].z = z;
    return 1;
}
int fixture_sweep(void* state, float, float, float, float, float, float,
                  float, uint32_t, NorthStarPhysicsCastHit* out)
{
    if (!state || !out) return 0;
    *out = {};
    return 1; // No triangles or fixture collision geometry.
}
int fixture_mesh(void* state, const float* v, uint32_t count,
                 const uint32_t* triangles, uint32_t indices)
{
    return state && v && triangles && count >= 3 && indices>=3 ? 1 : 0;
}
int fixture_impulse(void* state,uint32_t index,float x,float y,float z)
{
    if(!state) return 0;
    auto& objects=static_cast<Physics*>(state)->bodies;
    if(index>=objects.size() || objects[index].mass<=0.f || objects[index].player)
        return 0;
    objects[index].x+=x*0.05f; objects[index].y+=y*0.05f;
    objects[index].z+=z*0.05f;
    return 1;
}
NorthStarPhysicsApi const physics_api{create_physics, destroy_physics, step_physics,
    read_physics, jump_physics, move_static_physics, fixture_sweep, fixture_mesh, fixture_impulse};
NorthStarAudioApi const audio_api{create_audio, destroy_audio, play_audio, stop_audio};
NorthStarModule const modules[]{
    {sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI, NORTHSTARCPP_CAPABILITY_PHYSICS,
        "Fixture Physics", "1.0", NORTHSTARCPP_PHYSICS_ABI, sizeof(physics_api), &physics_api, error},
    {sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI, NORTHSTARCPP_CAPABILITY_AUDIO,
        "Fixture Audio", "1.0", NORTHSTARCPP_AUDIO_ABI, sizeof(audio_api), &audio_api, error}
};

} // namespace

extern "C" NORTHSTARCPP_MODULE_EXPORT
const NorthStarModule* northstar_module_query(uint32_t host, const char* capability, uint32_t api)
{
    if (host != NORTHSTARCPP_MODULE_ABI || !capability)
        return nullptr;
    for (auto const& module : modules)
        if (api == module.api_version && std::strcmp(capability, module.capability) == 0)
            return &module;
    return nullptr;
}
