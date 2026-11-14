// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Optional, backward-compatible physics residency ABI regression.
// Loads the provider directly to exercise the actual broadphase behavior.

#include <northstarcpp/physics_plugin.h>
#include <northstarcpp/module.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, char const* problem)
{
    if (!condition)
        throw std::runtime_error(problem);
}
struct Library {
    explicit Library(std::filesystem::path const& path)
        : module(LoadLibraryW(path.wstring().c_str()))
    {
        if (!module)
            throw std::runtime_error("Cannot load provider under test");
    }
    ~Library() { FreeLibrary(module); }
    Library(Library const&)=delete;
    Library& operator=(Library const&)=delete;

    template<typename Func>
    Func symbol(char const* name) const
    {
        return reinterpret_cast<Func>(GetProcAddress(module,name));
    }
    HMODULE module;
};
struct World {
    World(NorthStarPhysicsApi const* source,
          NorthStarRigidBody const* bodies,std::uint32_t count)
        : api(source), world(api->create(bodies,count))
    {
        require(world!=nullptr,"Cannot create physics world");
    }
    ~World() { api->destroy(world); }
    NorthStarPhysicsApi const* api;
    void* world;
};
}

int main(int argc,char** argv)
{
    if (argc!=2) return 2;
    try {
        Library provider(std::filesystem::u8path(argv[1]));
        auto const query=provider.symbol<NorthStarModuleQuery>(
            "northstar_module_query");
        require(query!=nullptr,"Provider lacks required module descriptor");
        auto const module=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS,NORTHSTARCPP_PHYSICS_ABI);
        require(module!=nullptr && module->api_size>=sizeof(NorthStarPhysicsApi) &&
                module->api!=nullptr,"Physics API is incompatible");
        auto const* api=static_cast<NorthStarPhysicsApi const*>(module->api);
        require(api->create && api->destroy && api->step && api->read_poses &&
                api->sweep_sphere,"Physics provider has incomplete API");

        auto const extension=provider.symbol<NorthStarPhysicsGetResidencyApi>(
            "northstar_physics_get_residency_api");
        require(extension!=nullptr,"Physics residency extension is missing");
        require(extension(2)==nullptr,"Unknown extension ABI was accepted");
        auto const* residency=extension(NORTHSTARCPP_PHYSICS_RESIDENCY_ABI);
        require(residency &&
                residency->abi_version==NORTHSTARCPP_PHYSICS_RESIDENCY_ABI &&
                residency->set_static_resident && residency->is_static_resident,
                "Physics residency ABI table is invalid");

        // Static cube top is y=3. Swept sphere never approaches the world
        // ground plane at y=0; an absent cube must be an actual miss.
        NorthStarRigidBody bodies[]{
            {0.f,1.f,0.f,2.f,0.f,0u},
            {10.f,1.f,0.f,1.f,2.f,0u},
        };
        World world(api,bodies,2);
        auto cast=[&]() {
            NorthStarPhysicsCastHit result{};
            require(api->sweep_sphere(world.world,
                0.f,5.f,0.f,0.f,2.f,0.f,
                0.2f,UINT32_MAX,&result)!=0,
                "Physics cast failed");
            return result;
        };
        require(residency->is_static_resident(world.world,0)==1,
                "Static body does not initially collide");
        auto hit=cast();
        require(hit.hit && hit.object_index==0,
                "Initial static cube sweep did not hit");
        require(residency->set_static_resident(world.world,0,0)==1,
                "Cannot suspend static body in provider");
        require(residency->is_static_resident(world.world,0)==0,
                "Provider retained collision participation after suspension");
        hit=cast();
        require(!hit.hit,"Suspended static cube still collides");
        require(residency->set_static_resident(world.world,0,0)==1,
                "Duplicate suspension was not idempotent");
        require(residency->set_static_resident(world.world,0,1)==1,
                "Cannot restore static body to physics world");
        require(residency->is_static_resident(world.world,0)==1,
                "Reactivated body did not regain collision participation");
        hit=cast();
        require(hit.hit && hit.object_index==0,
                "Reactivated scene object failed to restore sweep hit");
        require(residency->set_static_resident(world.world,1,0)==0 &&
                residency->is_static_resident(world.world,1)==-1,
                "Provider erroneously suspended dynamic body");
        NorthStarBodyPose poses[2]{};
        require(api->read_poses(world.world,poses,2)!=0,
                "Pose readback broke after static residency changes");
        std::cout<<"Physics residency extension passed: "<<module->provider<<'\n';
        return 0;
    } catch (std::exception const& ex) {
        std::cerr<<"Physics residency smoke failed: "<<ex.what()<<'\n';
        return 1;
    }
}
