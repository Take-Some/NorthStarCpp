// NorthStarCpp engine.perlin host bridge. Noise generation lives in the provider.
#include <northstarcpp/perlin.hpp>
#include "module_loader.hpp"
#include <limits>
#include <stdexcept>

namespace northstarcpp {
namespace {
NorthStarPerlinFractal native(PerlinFractal const& value) {
    return {sizeof(NorthStarPerlinFractal), value.octaves,
        static_cast<std::uint32_t>(value.mode), 0,
        value.frequency, value.lacunarity, value.persistence};
}
}
struct PerlinNoise::State {
    Module module{NORTHSTARCPP_CAPABILITY_PERLIN};
    NorthStarPerlinApi const& api = *module.query<NorthStarPerlinApi>(NORTHSTARCPP_PERLIN_ABI);
    void* context = nullptr;
    explicit State(PerlinOptions const& options) {
        NorthStarPerlinConfig cfg{sizeof(NorthStarPerlinConfig),
            options.period_x, options.period_y, options.period_z, options.seed};
        context = api.create(&cfg);
        if (!context)
            throw module.error("Cannot create Perlin noise context");
    }
    ~State() { api.destroy(context); }
};
PerlinNoise::PerlinNoise(PerlinOptions options) : state_(std::make_unique<State>(options)) {}
PerlinNoise::~PerlinNoise() = default;
std::string PerlinNoise::provider_name() const { return state_->module.provider(); }
std::string PerlinNoise::provider_version() const { return state_->module.version(); }
float PerlinNoise::sample1(double x) const {
    float v=0.f;
    if (!state_->api.sample1(state_->context,x,&v))
        throw state_->module.error("Cannot sample Perlin 1D");
    return v;
}
float PerlinNoise::sample2(double x, double y) const {
    float v=0.f;
    if (!state_->api.sample2(state_->context,x,y,&v))
        throw state_->module.error("Cannot sample Perlin 2D");
    return v;
}
float PerlinNoise::sample3(double x, double y, double z) const {
    float v=0.f;
    if (!state_->api.sample3(state_->context,x,y,z,&v))
        throw state_->module.error("Cannot sample Perlin 3D");
    return v;
}
float PerlinNoise::fractal1(double x, PerlinFractal const& cfg) const {
    float v=0.f; auto const params=native(cfg);
    if (!state_->api.fractal1(state_->context,x,&params,&v))
        throw state_->module.error("Cannot sample Perlin fractal 1D");
    return v;
}
float PerlinNoise::fractal2(double x, double y, PerlinFractal const& cfg) const {
    float v=0.f; auto const params=native(cfg);
    if (!state_->api.fractal2(state_->context,x,y,&params,&v))
        throw state_->module.error("Cannot sample Perlin fractal 2D");
    return v;
}
float PerlinNoise::fractal3(double x, double y, double z, PerlinFractal const& cfg) const {
    float v=0.f; auto const params=native(cfg);
    if (!state_->api.fractal3(state_->context,x,y,z,&params,&v))
        throw state_->module.error("Cannot sample Perlin fractal 3D");
    return v;
}
std::vector<float> PerlinNoise::fill2(double x,double y,double dx,double dy,
    std::uint32_t width,std::uint32_t height,PerlinFractal const& cfg) const {
    if (!width || !height || static_cast<std::uint64_t>(width)*height > 4194304u)
        throw std::invalid_argument("Invalid Perlin grid dimensions");
    std::vector<float> result(static_cast<std::size_t>(width)*height);
    fill2(x,y,dx,dy,width,height,width,cfg,result.data());
    return result;
}
void PerlinNoise::fill2(double x,double y,double dx,double dy,
    std::uint32_t width,std::uint32_t height,std::uint32_t stride,
    PerlinFractal const& cfg,float* output) const {
    auto const params=native(cfg);
    if (!state_->api.fill2(state_->context,x,y,dx,dy,width,height,stride,&params,output))
        throw state_->module.error("Cannot fill Perlin 2D grid");
}
} // namespace northstarcpp
