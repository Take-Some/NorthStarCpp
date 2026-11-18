// NorthStarCpp physics residency optimization microbenchmark.
// Synthetic CPU-only case, not an FPS benchmark. Compares identical decisions.
#include "../../engine/northstarcpp/modules/host/physics_residency_guards.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <stdexcept>

struct Object { float x,y,z,size; };

int main() {
    using Clock=std::chrono::steady_clock;
    using northstarcpp::physics_detail::VehicleResidencyGuard;
    constexpr std::size_t vehicle_count=32;
    constexpr std::size_t object_count=4000;
    constexpr int repetitions=55;
    std::vector<NorthStarPhysicsVehiclePose> vehicles(vehicle_count);
    for(std::size_t i=0;i<vehicles.size();++i) {
        auto& v=vehicles[i];
        v.x=static_cast<float>(i*31);
        v.y=0.f;
        v.z=static_cast<float>((i*97)%800);
        v.linear_velocity[0]=static_cast<float>(i%19)-9.f;
        v.linear_velocity[2]=static_cast<float>(i%17)-8.f;
    }
    std::vector<Object> objects(object_count);
    for(std::size_t i=0;i<objects.size();++i)
        objects[i]={float((i*331)%2400)-600.f, float((i*43)%70),
                    float((i*131)%1500)-300.f, float(1+i%8)};
    std::vector<VehicleResidencyGuard> scratch;
    auto original=[&]() {
        // Historical implementation: constructs a vector and computes hypot
        // inside the object/vehicle loop.
        std::uint64_t count=0;
        for(int rep=0;rep<repetitions;++rep) {
            std::vector<NorthStarPhysicsVehiclePose> guards;
            for(auto const& v:vehicles) guards.push_back(v);
            for(auto const& object:objects) {
                bool nearby=false;
                for(auto const& v:guards) {
                    float const ahead_x=v.x+v.linear_velocity[0]*2.f;
                    float const ahead_z=v.z+v.linear_velocity[2]*2.f;
                    float const radius=std::clamp(48.f+2.f*std::hypot(
                        v.linear_velocity[0],v.linear_velocity[2]),48.f,180.f)+object.size;
                    float const dx=object.x-ahead_x;
                    float const dz=object.z-ahead_z;
                    float const dy=object.y-v.y;
                    if(dx*dx+dy*dy+dz*dz<=radius*radius){nearby=true;break;}
                }
                count+=nearby;
            }
        }
        return count;
    };
    auto optimized=[&]() {
        std::uint64_t count=0;
        for(int rep=0;rep<repetitions;++rep) {
            scratch.clear();
            scratch.reserve(vehicles.size());
            for(auto const& vehicle:vehicles)
                scratch.push_back(VehicleResidencyGuard::from_pose(vehicle));
            for(auto const& object:objects) {
                bool nearby=false;
                for(auto const& guard:scratch) {
                    if(guard.contains(object.x,object.y,object.z,object.size)) {
                        nearby=true;break;
                    }
                }
                count+=nearby;
            }
        }
        return count;
    };
    // Warm instruction/data caches before recording.
    if(original()!=optimized())throw std::runtime_error("Residency decision mismatch");
    auto measured=[&](auto const& fn) {
        auto const before=Clock::now();
        auto const count=fn();
        auto const after=Clock::now();
        return std::pair<double,std::uint64_t>{
            std::chrono::duration<double,std::milli>(after-before).count(),count};
    };
    auto const base=measured(original);
    auto const fast=measured(optimized);
    if(base.second!=fast.second)throw std::runtime_error("Results changed");
    std::printf("Synthetic streaming: objects=%zu vehicles=%zu passes=%d\n",
        object_count,vehicle_count,repetitions);
    std::printf("Old: %.3f ms | Optimized: %.3f ms | speedup: %.2fx\n",
        base.first,fast.first,base.first/fast.first);
    std::printf("Same decisions: %llu | Retained guard capacity: %zu\n",
        static_cast<unsigned long long>(base.second),scratch.capacity());
    return 0;
}
