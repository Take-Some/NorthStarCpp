// Provider-neutral engine.geometry service. C++ objects do not cross DLL ABI.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/geometry_plugin.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace northstarcpp {

struct GeometryData {
    std::vector<float> xyz;          // packed x,y,z
    std::vector<std::uint16_t> indices;
    std::uint32_t vertex_stride=12;  // bytes
};

class GeometryService final {
public:
    GeometryService();
    ~GeometryService();
    GeometryService(GeometryService const&) = delete;
    GeometryService& operator=(GeometryService const&) = delete;
    std::string provider_name() const;
    std::string provider_version() const;

    GeometryData generate(NorthStarGeometryRequest const& request) const;
    GeometryData solid(std::uint32_t shape) const;
    GeometryData wire(std::uint32_t shape) const;
    GeometryData ring(float inner_radius,float width,std::uint32_t segments) const;
    GeometryData torus(float major_radius,float minor_radius,
                       std::uint32_t major_segments,std::uint32_t minor_segments) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace northstarcpp
