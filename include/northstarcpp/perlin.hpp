// NorthStarCpp engine.perlin C++ SDK.
#pragma once
#include <northstarcpp/perlin_plugin.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace northstarcpp {

enum class PerlinMode : std::uint32_t {
    fbm = NORTHSTAR_PERLIN_FBM,
    turbulence = NORTHSTAR_PERLIN_TURBULENCE,
    ridged = NORTHSTAR_PERLIN_RIDGED
};

struct PerlinOptions {
    std::uint64_t seed{0};
    std::uint32_t period_x{256};
    std::uint32_t period_y{256};
    std::uint32_t period_z{256};
};

struct PerlinFractal {
    std::uint32_t octaves{5};
    PerlinMode mode{PerlinMode::fbm};
    double frequency{1.0};
    double lacunarity{2.0};
    double persistence{0.5};
};

// Provider-neutral, deterministic, and thread-safe for concurrent sampling
// on an initialized context. The loader retains the provider DLL for its lifetime.
class PerlinNoise final {
public:
    explicit PerlinNoise(PerlinOptions options = {});
    ~PerlinNoise();
    PerlinNoise(PerlinNoise const&) = delete;
    PerlinNoise& operator=(PerlinNoise const&) = delete;
    PerlinNoise(PerlinNoise&&) = delete;
    PerlinNoise& operator=(PerlinNoise&&) = delete;

    std::string provider_name() const;
    std::string provider_version() const;
    float sample1(double x) const;
    float sample2(double x, double y) const;
    float sample3(double x, double y, double z) const;
    float fractal1(double x, PerlinFractal const& config = {}) const;
    float fractal2(double x, double y, PerlinFractal const& config = {}) const;
    float fractal3(double x, double y, double z, PerlinFractal const& config = {}) const;
    std::vector<float> fill2(double x, double y, double step_x, double step_y,
                             std::uint32_t width, std::uint32_t height,
                             PerlinFractal const& config = {}) const;
    // Fill an existing row-strided buffer without allocating.
    void fill2(double x, double y, double step_x, double step_y,
               std::uint32_t width, std::uint32_t height, std::uint32_t row_stride,
               PerlinFractal const& config, float* output) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace northstarcpp
