#include <northstarcpp/sky_environment.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace northstarcpp;
static void check(bool okay,const char* what) {
    if(!okay)throw std::runtime_error(what);
}
int main() {
    try {
        SkyEnvironment sky;
        check(!sky.settings().enabled,"Legacy sky must remain unchanged before opt-in");
        SkySettings settings{};
        settings.enabled=true;
        settings.latitude_degrees=0;
        settings.day_of_year=81; // Equinox at equator.
        settings.time_of_day=12;
        settings.cloud_coverage=.37;
        settings.fog_density=.045;
        check(settings.fog_density>.0,"Authored fog is enabled");
        settings.day_length_seconds=1200;
        settings.moon_phase=.5;
        sky.configure(settings);
        const auto noon=sky.snapshot();
        check(noon.sun_direction.y>.98f,"Equinox noon sun should be high");
        check(noon.daylight>.95,"Noon daylight should be bright");
        check(std::abs(noon.settings.fog_density-.045)<1e-9,
              "Sky fogDensity must persist in a provider-neutral snapshot");
        sky.set_time(0);
        const auto midnight=sky.snapshot();
        check(midnight.sun_direction.y<-.98f,"Midnight sun must be below horizon");
        check(midnight.moon_direction.y>.98f,"Full moon should rise at midnight");
        check(midnight.daylight<.05,"Night must have low daylight");
        auto starry=sky.render_rgba(256,128);
        check(starry.size()==256u*128u*4u,"Night texture byte size");
        for(std::size_t i=3;i<starry.size();i+=4)
            check(starry[i]==255,"Sky alpha should be opaque");
        sky.set_time(12);
        const auto day=sky.render_rgba(256,128);
        check(day!=starry,"Sky texture must change across day and night");
        const auto before=sky.snapshot();
        sky.advance(.5);
        check(sky.snapshot().settings.time_of_day>before.settings.time_of_day,
              "Clock should advance from game dt");
        sky.configure(SkySettings{.enabled=true,.paused=true});
        const auto paused=sky.snapshot().settings.time_of_day;
        sky.advance(.5);
        check(sky.snapshot().settings.time_of_day==paused,"Paused sky clock must stop");
        bool rejected=false;
        try {SkySettings invalid;invalid.time_of_day=24;sky.configure(invalid);}
        catch(std::invalid_argument const&) {rejected=true;}
        check(rejected,"Invalid hour must be rejected");
        rejected=false;
        try {SkySettings invalid;invalid.cloud_coverage=3;sky.configure(invalid);}
        catch(std::invalid_argument const&) {rejected=true;}
        check(rejected,"Invalid cloud coverage must be rejected");
        rejected=false;
        try {SkySettings invalid;invalid.fog_density=.5;sky.configure(invalid);}
        catch(std::invalid_argument const&) {rejected=true;}
        check(rejected,"Invalid sky fog density must be rejected");
        // Provider-neutral authored sky and dual flow-map cloud layers.
        SkySettings textured{};
        textured.enabled=true;
        textured.cloud_coverage=.75;
        textured.cloud_speed=.18; // real-simulation seconds, periodic UV phase
        textured.day_length_seconds=86400;
        sky.configure(textured);
        SkyTextureLayers source{};
        auto paint=[](SkyLayerImage& layer,unsigned seed) {
            layer.width=32;layer.height=16;
            layer.rgba8.resize(32u*16u*4u);
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x) {
                auto index=(y*32+x)*4;
                layer.rgba8[index]=std::uint8_t((x*19+y*7+seed)%256);
                layer.rgba8[index+1]=std::uint8_t((x*9+y*31+seed)%256);
                layer.rgba8[index+2]=std::uint8_t((x*37+y*13+seed)%256);
                layer.rgba8[index+3]=255;
            }
        };
        paint(source.atmosphere,21);
        paint(source.cloud_projection,104);
        paint(source.flow_phase,183);
        paint(source.cloud_breakup,67);
        check(source.valid(),"Authored sky source layers should validate");
        auto const plain=sky.render_rgba(128,64);
        auto const layered=sky.render_rgba(128,64,nullptr,&source);
        check(layered!=plain,"Authored sky.ytd/clouds.ytd must change sky pixels");
        auto const offset0=sky.snapshot().flow_phase;
        for(unsigned i=0;i<8;++i)sky.advance(1.);
        auto const offset1=sky.snapshot().flow_phase;
        check(offset1!=offset0,"Cloud flow phase must change with game dt");
        auto const moving=sky.render_rgba(128,64,nullptr,&source);
        check(moving!=layered,"Original cloud flow layers must animate");
        textured.paused=true;sky.configure(textured);
        auto const paused_phase=sky.snapshot().flow_phase;
        sky.advance(.5);
        check(sky.snapshot().flow_phase==paused_phase,
              "Paused world must stop flow-map animation");
        rejected=false;
        try {
            SkySettings invalid;invalid.preset="../bad";
            sky.configure(invalid);
        }catch(std::invalid_argument const&) {rejected=true;}
        check(rejected,"Path traversal in sky preset must be rejected");
        sky.reset();
        check(!sky.settings().enabled,"Reset must restore original sky rendering");
        std::puts("PASS engine.sky: day/night, sun/moon, texture, pause, validation");
        return 0;
    } catch(std::exception const& error) {
        std::fprintf(stderr,"engine.sky FAIL: %s\n",error.what());
        return 1;
    }
}
