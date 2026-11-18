// Deterministic engine.sky QA: sun motion, twilight haze, cloud advection,
// authored cloud flow, scattering approximation and CPU texture update time.
// This tests actual SkyEnvironment, not a reimplementation of its formulas.
#include <northstarcpp/sky_environment.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace northstarcpp;
namespace {
void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
using Image=std::vector<std::uint8_t>;
std::uint64_t pixel_diff(Image const& a,Image const& b,int threshold=3) {
    require(a.size()==b.size(),"Image size differs");
    std::uint64_t changed=0;
    for(std::size_t i=0;i<a.size();i+=4) {
        const int delta=std::abs(int(a[i])-int(b[i]))+
                        std::abs(int(a[i+1])-int(b[i+1]))+
                        std::abs(int(a[i+2])-int(b[i+2]));
        changed+=(delta>threshold);
    }
    return changed;
}
double mean_brightness(Image const& a) {
    double total=0.;
    for(std::size_t i=0;i<a.size();i+=4)
        total+=a[i]+a[i+1]+a[i+2];
    return total/double(a.size()/4)/3.;
}
void make_layers(SkyTextureLayers& maps) {
    auto fill=[](SkyLayerImage& dst,unsigned seed){
        dst.width=64;dst.height=32;dst.rgba8.resize(64u*32u*4);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x) {
            auto const index=(y*64+x)*4;
            // Deterministic high-frequency maps exercise bilinear flow sampling.
            dst.rgba8[index+0]=std::uint8_t((x*19+y*11+seed)%256);
            dst.rgba8[index+1]=std::uint8_t((x*7+y*29+seed)%256);
            dst.rgba8[index+2]=std::uint8_t((x*37+y*13+seed)%256);
            dst.rgba8[index+3]=255;
        }
    };
    fill(maps.atmosphere,7);
    fill(maps.cloud_projection,113);
    fill(maps.flow_phase,61);
    fill(maps.cloud_breakup,191);
}
}
int main() {
    try {
        constexpr unsigned w=256,h=128;
        SkyEnvironment sky;
        SkySettings cfg{};
        cfg.enabled=true;cfg.day_length_seconds=240.;
        cfg.latitude_degrees=0.;cfg.day_of_year=81;
        cfg.clouds=false;cfg.stars=false;cfg.moon=false;
        cfg.haze=0.;cfg.time_of_day=6.;
        sky.configure(cfg);
        const auto dawn=sky.snapshot();
        require(dawn.sun_elevation_degrees>-1. &&
                dawn.sun_elevation_degrees<1.,"Equinox sunrise solar altitude");
        const auto dawn_img=sky.render_rgba(w,h);
        for(int i=0;i<60;++i)sky.advance(1.);
        const auto noon=sky.snapshot();
        require(std::abs(noon.settings.time_of_day-12.)<1e-8,
                "Sun clock must advance 6 h over 60 simulated seconds");
        require(noon.sun_elevation_degrees>89.,
                "Equinox noon sun must reach zenith");
        const auto noon_img=sky.render_rgba(w,h);
        require(pixel_diff(dawn_img,noon_img)>w*h/2,
                "Sun/daylight transition must affect the sky pixels");
        for(int i=0;i<60;++i)sky.advance(1.);
        const auto dusk=sky.snapshot();
        require(std::abs(dusk.settings.time_of_day-18.)<1e-8,
                "Sun clock must reach 18:00");
        require(std::abs(dusk.sun_elevation_degrees)<1.,
                "Equinox sunset must reach horizon");
        const auto dusk_img=sky.render_rgba(w,h);
        require(pixel_diff(noon_img,dusk_img)>w*h/2,
                "Sunset must change the framebuffer texture");
        // At solar sunset the west-facing horizon gets warm red/orange.
        // At equinox and 18:00, the sun lies at -X -> azimuth u=.75.
        const auto sun_pixel=(std::size_t(h/2)*w+std::size_t(.75*w))*4;
        require(dusk_img[sun_pixel]>dusk_img[sun_pixel+2],
                "Sunset horizon should exhibit warm atmospheric color");

        cfg.time_of_day=12.;cfg.haze=0.;cfg.clouds=false;
        sky.configure(cfg);
        const auto clear=sky.render_rgba(w,h);
        cfg.haze=1.;sky.configure(cfg);
        const auto hazy=sky.render_rgba(w,h);
        const auto haze_delta=pixel_diff(clear,hazy);
        require(haze_delta>w*h/10,
                "Atmospheric haze should change a substantial sky region");

        cfg.haze=.25;cfg.clouds=true;cfg.cloud_coverage=0.;
        cfg.cloud_speed=.10;cfg.day_length_seconds=86400.;
        sky.reset();sky.configure(cfg);
        const auto cloudless=sky.render_rgba(w,h);
        cfg.cloud_coverage=.95;sky.configure(cfg);
        const auto cloudy=sky.render_rgba(w,h);
        const auto coverage_delta=pixel_diff(cloudless,cloudy);
        require(coverage_delta>w*h/15,
                "High cloud coverage must change sky pixels");
        const auto first_flow=sky.snapshot().flow_phase;
        const auto first_img=sky.render_rgba(w,h);
        for(int i=0;i<6;++i)sky.advance(1.);
        const auto new_flow=sky.snapshot().flow_phase;
        const auto moved=sky.render_rgba(w,h);
        const auto movement_delta=pixel_diff(first_img,moved);
        require(new_flow>first_flow && movement_delta>w*h/100,
                "Cloud advection must change texture pixels");
        // Freeze cloud animation on pause, rather than just hiding clouds.
        cfg.paused=true;sky.configure(cfg);
        const auto frozen=sky.render_rgba(w,h);
        for(int i=0;i<6;++i)sky.advance(1.);
        require(pixel_diff(frozen,sky.render_rgba(w,h))==0,
                "Paused cloud/sun animation must be pixel-identical");

        // The 4 original-layer texture inputs should influence the output
        // separately from synthesized noise and continue to advect with dt.
        cfg.paused=false;cfg.cloud_speed=.13;
        sky.configure(cfg);
        SkyTextureLayers layers{};
        make_layers(layers);
        require(layers.valid(),"Authored sky layers invalid");
        const auto base=sky.render_rgba(w,h);
        const auto textured=sky.render_rgba(w,h,nullptr,&layers);
        const auto layer_delta=pixel_diff(base,textured);
        require(layer_delta>w*h/5,
                "Authored sky and cloud maps should change the picture");
        for(int i=0;i<4;++i)sky.advance(1.);
        const auto flow_img=sky.render_rgba(w,h,nullptr,&layers);
        const auto flow_delta=pixel_diff(textured,flow_img);
        require(flow_delta>w*h/100,
                "Authored flow maps should visibly move");

        // Benchmark the current CPU composition hotspot at its actual
        // production sky fallback resolution, without pretending GPU FPS.
        constexpr unsigned bw=512,bh=256;
        constexpr int iterations=16;
        std::array<double,iterations> ms{};
        for(int i=0;i<iterations;++i) {
            const auto t0=std::chrono::steady_clock::now();
            auto result=sky.render_rgba(bw,bh,nullptr,&layers);
            const auto t1=std::chrono::steady_clock::now();
            require(result.size()==std::size_t(bw)*bh*4,
                    "Production sky RGBA dimensions");
            ms[i]=std::chrono::duration<double,std::milli>(t1-t0).count();
            sky.advance(.5);
        }
        std::sort(ms.begin(),ms.end());
        const auto p95=ms[14],p99=ms[15];
        std::printf("SUN sunrise=%.3f noon=%.3f sunset=%.3f elevations_deg\n",
            dawn.sun_elevation_degrees,noon.sun_elevation_degrees,
            dusk.sun_elevation_degrees);
        std::printf("PIXELS time_transition=%llu dusk_transition=%llu "
            "haze=%llu coverage=%llu drift=%llu authored=%llu authored_drift=%llu "
            "at %ux%u\n",
            static_cast<unsigned long long>(pixel_diff(dawn_img,noon_img)),
            static_cast<unsigned long long>(pixel_diff(noon_img,dusk_img)),
            static_cast<unsigned long long>(haze_delta),
            static_cast<unsigned long long>(coverage_delta),
            static_cast<unsigned long long>(movement_delta),
            static_cast<unsigned long long>(layer_delta),
            static_cast<unsigned long long>(flow_delta),w,h);
        std::printf("BRIGHTNESS sun_dawn=%.2f noon=%.2f sunset=%.2f "
            "cloudless=%.2f overcast=%.2f\n",
            mean_brightness(dawn_img),mean_brightness(noon_img),
            mean_brightness(dusk_img),mean_brightness(cloudless),
            mean_brightness(cloudy));
        std::printf("CPU 512x256 authored sky RGBA8 x%d: min=%.2f "
            "median=%.2f p95=%.2f p99=%.2f ms (no GPU upload)\n",
            iterations,ms.front(),(ms[7]+ms[8])*.5,p95,p99);
        std::puts("PASS: engine.sky sun path, atmospheric haze, cloud dissolve/coverage, "
                  "advection, authored flow, pause, CPU composition");
        return 0;
    }catch(std::exception const& e){
        std::fprintf(stderr,"FAIL engine.sky dynamics: %s\n",e.what());
        return 1;
    }
}
