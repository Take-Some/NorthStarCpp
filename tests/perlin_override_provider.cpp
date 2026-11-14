// Test-only distinct provider; the same fixture is compiled as a DLL or SO.
#include <northstarcpp/perlin_plugin.h>
#include <cstring>

namespace {
char const* last_error() { return "Fixture Perlin error"; }
void* create(NorthStarPerlinConfig const*) { static int context; return &context; }
void destroy(void*) {}
int sample1(void*, double, float* out) { *out = 0.375f; return 1; }
int sample2(void* context, double x, double, float* out) { return sample1(context, x, out); }
int sample3(void* context, double x, double, double, float* out) { return sample1(context, x, out); }
int fractal1(void* context, double x, NorthStarPerlinFractal const*, float* out)
{ return sample1(context, x, out); }
int fractal2(void* context, double x, double, NorthStarPerlinFractal const*, float* out)
{ return sample1(context, x, out); }
int fractal3(void* context, double x, double, double, NorthStarPerlinFractal const*, float* out)
{ return sample1(context, x, out); }
int fill2(void*, double, double, double, double, uint32_t width, uint32_t height,
          uint32_t stride, NorthStarPerlinFractal const*, float* out)
{
    for (uint32_t row = 0; row < height; ++row)
        for (uint32_t column = 0; column < width; ++column)
            out[row * stride + column] = 0.375f;
    return 1;
}

#ifdef NORTHSTARCPP_INVALID_PERLIN_FIXTURE
NorthStarPerlinApi const api{create, destroy, sample1, sample2, sample3, fractal1, fractal2, fractal3, nullptr};
#else
NorthStarPerlinApi const api{create, destroy, sample1, sample2, sample3, fractal1, fractal2, fractal3, fill2};
#endif
NorthStarModule const module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_PERLIN, "Fixture Perlin", "1.0", NORTHSTARCPP_PERLIN_ABI,
    sizeof(api), &api, last_error};
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule* northstar_module_query(
    uint32_t host, char const* capability, uint32_t version)
{
    return host == module.host_abi && version == module.api_version &&
        capability && std::strcmp(capability, module.capability) == 0 ? &module : nullptr;
}
