// NorthStarCpp engine.perlin: improved seeded gradient noise provider.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/perlin_plugin.h>
#include "module_provider.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>

namespace {
using namespace northstarcpp::module_provider;
constexpr double maximum_coordinate = 1000000000.0;
constexpr std::uint64_t maximum_cells = 4194304u;

void require(bool value, char const* message) {
    if (!value) throw std::invalid_argument(message);
}
void coordinate(double value) {
    require(std::isfinite(value) && std::abs(value) <= maximum_coordinate,
            "Perlin coordinate must be finite and within +/-1e9");
}
double fade(double t) { return t*t*t*(t*(t*6.0-15.0)+10.0); }
double lerp(double a, double b, double t) { return a + t*(b-a); }
double gradient1(unsigned hash, double x) {
    return (hash & 1u) ? -x : x;
}
double gradient2(unsigned hash, double x, double y) {
    constexpr double diag = 0.7071067811865475244;
    switch (hash & 7u) {
    case 0: return x;
    case 1: return -x;
    case 2: return y;
    case 3: return -y;
    case 4: return (x+y)*diag;
    case 5: return (x-y)*diag;
    case 6: return (-x+y)*diag;
    default:return (-x-y)*diag;
    }
}
double gradient3(unsigned hash, double x, double y, double z) {
    // Classic improved Perlin gradients; each gradient has length sqrt(2).
    unsigned h = hash & 15u;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}
struct Noise {
    std::array<unsigned char,512> permutation{};
    int px=256,py=256,pz=256;
    explicit Noise(NorthStarPerlinConfig const& config) {
        require(config.size >= sizeof(config), "Invalid Perlin configuration size");
        auto period = [](std::uint32_t p) {
            require(p <= 256, "Perlin periods must be 0..256");
            return static_cast<int>(p ? p : 256);
        };
        px=period(config.period_x);
        py=period(config.period_y);
        pz=period(config.period_z);
        for (unsigned i=0;i<256;++i) permutation[i]=static_cast<unsigned char>(i);
        std::uint64_t state=config.seed;
        auto next = [&]() {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z=state;
            z=(z^(z>>30))*0xBF58476D1CE4E5B9ull;
            z=(z^(z>>27))*0x94D049BB133111EBull;
            return z^(z>>31);
        };
        for (unsigned i=255;i>0;--i) {
            unsigned j=static_cast<unsigned>(next() % (i+1u));
            std::swap(permutation[i],permutation[j]);
        }
        for (unsigned i=0;i<256;++i) permutation[256+i]=permutation[i];
    }
    static int periodic(std::int64_t coord, int p) {
        auto value=coord%p;
        return static_cast<int>(value<0 ? value+p : value);
    }
    unsigned hash1(std::int64_t x) const {
        return permutation[periodic(x,px)];
    }
    unsigned hash2(std::int64_t x, std::int64_t y) const {
        return permutation[permutation[periodic(x,px)]+periodic(y,py)];
    }
    unsigned hash3(std::int64_t x, std::int64_t y, std::int64_t z) const {
        return permutation[permutation[permutation[periodic(x,px)]+periodic(y,py)]
                           +periodic(z,pz)];
    }
    float sample1(double x) const {
        coordinate(x);
        double const fx=std::floor(x),tx=x-fx;
        auto ix=static_cast<std::int64_t>(fx);
        double a=gradient1(hash1(ix),tx);
        double b=gradient1(hash1(ix+1),tx-1);
        return static_cast<float>(
            std::clamp(2.0*lerp(a,b,fade(tx)),-1.0,1.0));
    }
    float sample2(double x, double y) const {
        coordinate(x); coordinate(y);
        double const fx=std::floor(x),fy=std::floor(y);
        auto ix=static_cast<std::int64_t>(fx),iy=static_cast<std::int64_t>(fy);
        double tx=x-fx,ty=y-fy;
        double a=gradient2(hash2(ix,iy),tx,ty);
        double b=gradient2(hash2(ix+1,iy),tx-1,ty);
        double c=gradient2(hash2(ix,iy+1),tx,ty-1);
        double d=gradient2(hash2(ix+1,iy+1),tx-1,ty-1);
        double value=lerp(lerp(a,b,fade(tx)),lerp(c,d,fade(tx)),fade(ty));
        return static_cast<float>(std::clamp(value*1.4142135623730951,-1.0,1.0));
    }
    float sample3(double x,double y,double z) const {
        coordinate(x); coordinate(y); coordinate(z);
        double fx=std::floor(x),fy=std::floor(y),fz=std::floor(z);
        auto ix=static_cast<std::int64_t>(fx),iy=static_cast<std::int64_t>(fy),
             iz=static_cast<std::int64_t>(fz);
        double tx=x-fx,ty=y-fy,tz=z-fz;
        double const sx=fade(tx),sy=fade(ty),sz=fade(tz);
        auto corner=[&](int dx,int dy,int dz) {
            return gradient3(hash3(ix+dx,iy+dy,iz+dz),
                             tx-dx,ty-dy,tz-dz);
        };
        double a=lerp(corner(0,0,0),corner(1,0,0),sx);
        double b=lerp(corner(0,1,0),corner(1,1,0),sx);
        double c=lerp(corner(0,0,1),corner(1,0,1),sx);
        double d=lerp(corner(0,1,1),corner(1,1,1),sx);
        return static_cast<float>(std::clamp(lerp(lerp(a,b,sy),lerp(c,d,sy),sz),-1.0,1.0));
    }
};
void validate(NorthStarPerlinFractal const* cfg) {
    require(cfg && cfg->size >= sizeof(*cfg) &&
            cfg->octaves >= 1 && cfg->octaves <= NORTHSTAR_PERLIN_MAX_OCTAVES &&
            cfg->mode <= NORTHSTAR_PERLIN_RIDGED && cfg->reserved==0,
            "Invalid Perlin fractal configuration");
    require(std::isfinite(cfg->frequency) && cfg->frequency > 0 &&
            std::isfinite(cfg->lacunarity) && cfg->lacunarity >= 1 &&
            std::isfinite(cfg->persistence) && cfg->persistence >= 0 &&
            cfg->persistence <= 1, "Invalid Perlin fractal parameters");
    double multiplier=cfg->frequency;
    for (unsigned i=1;i<cfg->octaves;++i) {
        multiplier*=cfg->lacunarity;
        require(std::isfinite(multiplier) && multiplier<=maximum_coordinate,
                "Perlin fractal frequency overflow");
    }
}
double max_frequency(NorthStarPerlinFractal const& cfg) {
    double result=cfg.frequency;
    for (unsigned i=1;i<cfg.octaves;++i) result*=cfg.lacunarity;
    return result;
}
void scaled_coordinates(double max_scale,double x,double y,double z=0) {
    require(std::abs(x)*max_scale<=maximum_coordinate &&
            std::abs(y)*max_scale<=maximum_coordinate &&
            std::abs(z)*max_scale<=maximum_coordinate,
            "Perlin fractal input exceeds coordinate limits");
}
double mode_value(float sample, std::uint32_t mode) {
    switch(mode) {
    case NORTHSTAR_PERLIN_TURBULENCE: return std::abs(static_cast<double>(sample));
    case NORTHSTAR_PERLIN_RIDGED: {
        double ridge=1.0-std::abs(static_cast<double>(sample));
        return ridge*ridge;
    }
    default: return sample;
    }
}
template<typename Sampler>
float fractal(NorthStarPerlinFractal const& cfg, Sampler&& sample) {
    double frequency=cfg.frequency,amplitude=1,sum=0,normalizer=0;
    for (unsigned i=0;i<cfg.octaves;++i) {
        sum+=amplitude*mode_value(sample(frequency),cfg.mode);
        normalizer+=amplitude;
        amplitude*=cfg.persistence;
        frequency*=cfg.lacunarity;
    }
    double result=sum/normalizer;
    return static_cast<float>(std::clamp(result,
        cfg.mode==NORTHSTAR_PERLIN_FBM ? -1.0 : 0.0,1.0));
}
float fractal1(Noise const& noise, double x,
               NorthStarPerlinFractal const& cfg) {
    return fractal(cfg,[&](double frequency){
        return noise.sample1(x*frequency);
    });
}
float fractal2(Noise const& noise, double x,double y,
               NorthStarPerlinFractal const& cfg) {
    return fractal(cfg,[&](double frequency){
        return noise.sample2(x*frequency,y*frequency);
    });
}
float fractal3(Noise const& noise, double x,double y,double z,
               NorthStarPerlinFractal const& cfg) {
    return fractal(cfg,[&](double frequency){
        return noise.sample3(x*frequency,y*frequency,z*frequency);
    });
}
void* create(NorthStarPerlinConfig const* config) {
    Noise* noise=nullptr;
    guarded([&] {
        require(config!=nullptr,"Missing Perlin configuration");
        // Validate the versioned struct before dereferencing its other fields.
        require(config->size>=sizeof(NorthStarPerlinConfig),"Invalid Perlin configuration size");
        noise=new Noise(*config);
    });
    return noise;
}
void destroy(void* context) { delete static_cast<Noise*>(context); }
int sample1(void* ctx,double x,float* output) {
    return guarded([&] {
        require(ctx&&output,"Missing Perlin context or output");
        *output=static_cast<Noise const*>(ctx)->sample1(x);
    });
}
int sample2(void* ctx,double x,double y,float* output) {
    return guarded([&] {
        require(ctx&&output,"Missing Perlin context or output");
        *output=static_cast<Noise const*>(ctx)->sample2(x,y);
    });
}
int sample3(void* ctx,double x,double y,double z,float* output) {
    return guarded([&] {
        require(ctx&&output,"Missing Perlin context or output");
        *output=static_cast<Noise const*>(ctx)->sample3(x,y,z);
    });
}
int fractal1_api(void* ctx,double x,
                 NorthStarPerlinFractal const* cfg,float* output) {
    return guarded([&] {
        require(ctx&&output,"Missing Perlin context or output");
        validate(cfg);coordinate(x);
        scaled_coordinates(max_frequency(*cfg),x,0);
        *output=fractal1(*static_cast<Noise const*>(ctx),x,*cfg);
    });
}
int fractal2_api(void* ctx,double x,double y,
                 NorthStarPerlinFractal const* cfg,float* output) {
    return guarded([&] {
        require(ctx&&output,"Missing Perlin context or output");
        validate(cfg);coordinate(x);coordinate(y);
        scaled_coordinates(max_frequency(*cfg),x,y);
        *output=fractal2(*static_cast<Noise const*>(ctx),x,y,*cfg);
    });
}
int fractal3_api(void* ctx,double x,double y,double z,
                 NorthStarPerlinFractal const* cfg,float* output) {
    return guarded([&] {
        require(ctx&&output,"Missing Perlin context or output");
        validate(cfg);coordinate(x);coordinate(y);coordinate(z);
        scaled_coordinates(max_frequency(*cfg),x,y,z);
        *output=fractal3(*static_cast<Noise const*>(ctx),x,y,z,*cfg);
    });
}
int fill2(void* ctx,double x,double y,double dx,double dy,
          std::uint32_t width,std::uint32_t height,std::uint32_t stride,
          NorthStarPerlinFractal const* cfg,float* output) {
    return guarded([&] {
        require(ctx&&output,"Missing Perlin context or output");
        validate(cfg);
        require(width&&height&&stride>=width&&
                static_cast<std::uint64_t>(width)*height<=maximum_cells &&
                static_cast<std::uint64_t>(height-1)*stride+width<=
                    static_cast<std::uint64_t>(SIZE_MAX)/sizeof(float),
                "Invalid Perlin grid size/stride");
        coordinate(x);coordinate(y);coordinate(dx);coordinate(dy);
        double xend=x+dx*static_cast<double>(width-1);
        double yend=y+dy*static_cast<double>(height-1);
        coordinate(xend);coordinate(yend);
        double mf=max_frequency(*cfg);
        scaled_coordinates(mf,x,y);
        scaled_coordinates(mf,xend,yend);
        Noise const& noise=*static_cast<Noise const*>(ctx);
        for (std::uint32_t row=0;row<height;++row)
            for (std::uint32_t col=0;col<width;++col)
                output[static_cast<std::size_t>(row)*stride+col]=
                    fractal2(noise,x+col*dx,y+row*dy,*cfg);
    });
}
NorthStarPerlinApi const api{create,destroy,sample1,sample2,sample3,
    fractal1_api,fractal2_api,fractal3_api,fill2};
NorthStarModule const module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_PERLIN,"Improved Perlin","1.0.0",
    NORTHSTARCPP_PERLIN_ABI,sizeof(api),&api,last_error};
} // namespace

extern "C" const NorthStarModule* northstar_module_query(
    std::uint32_t host,char const* capability,std::uint32_t version) {
    return northstarcpp::module_provider::match(module,host,capability,version);
}
