// NorthStarCpp V-HACD 4.1 provider-independent convex decomposition.
#pragma once
#include <cstdint>
#include <atomic>
#include <vector>
namespace northstarcpp::collision_cooker {
struct Hull {
    std::vector<float> xyz; // local-model-space XYZ triples
    std::vector<std::uint32_t> indices; // cooked hull triangle indices
};
struct Options {
    std::uint32_t max_hulls=4;          // splitsNum / maximum convex hulls
    std::uint32_t resolution=100000;   // bounded voxel budget
    std::uint32_t max_vertices=64;     // maximum vertices per hull
    double volume_error_percent=1.0;
    std::atomic<int>* progress_percent=nullptr;
    std::atomic<bool>* cancel_requested=nullptr;
};
std::vector<Hull> cook(std::vector<float> const& xyz,
                       std::vector<std::uint32_t> const& triangles,
                       Options const& options);
} // namespace northstarcpp::collision_cooker
