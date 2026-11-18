// NorthStar Sky — renderer-neutral, deterministic game-world atmosphere.
// Authored YDD/YTD sky models are presentation assets, never a physics object.
// The native Sky state is independent of Vulkan/D3D12 and script ownership.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>
#include "sky_blue_noise64.hpp"

namespace northstarcpp {
struct SkyDirection {float x=0.f,y=0.f,z=1.f;};
struct SkySettings {
    bool enabled=false;
    bool paused=false;
    bool sun=true;
    bool moon=true;
    bool stars=true;
    bool clouds=true;
    double time_of_day=12.0;        // local solar time, hours [0,24)
    double day_length_seconds=1200; // 20 real minutes per full day
    double latitude_degrees=42.0;   // axial declination and season honored
    int day_of_year=172;            // 1..365
    double moon_phase=.5;           // 0=new moon, .5=full moon
    double cloud_coverage=.3;       // 0..1
    double cloud_speed=.015;        // texture UV units per real simulation second
    double haze=.2;                 // 0..1
    double fog_density=0.;          // 0..0.25, scene units^-1; 0 disables aerial fog
    double sun_intensity=1.0;       // 0..8
    double moon_intensity=.25;      // 0..4
    double star_density=.55;        // 0..1
    std::string preset="sky-act2-watchtower-overcast";
};
struct SkySnapshot {
    SkySettings settings{};
    SkyDirection sun_direction{},moon_direction{};
    double sun_elevation_degrees=0;
    double moon_elevation_degrees=0;
    double moon_illumination=0;
    double daylight=0;
    double cloud_offset=0;
    double flow_phase=0;
    std::uint64_t revision=0;
};

struct SkyLayerImage {
    std::uint32_t width=0,height=0;
    std::vector<std::uint8_t> rgba8;
    bool valid() const noexcept {
        return width>0 && height>0 &&
            rgba8.size()==std::size_t(width)*height*4u;
    }
};
struct SkyTextureLayers {
    SkyLayerImage atmosphere,cloud_projection,flow_phase,cloud_breakup;
    bool valid() const noexcept {
        return atmosphere.valid() && cloud_projection.valid() &&
            flow_phase.valid() && cloud_breakup.valid();
    }
};
class SkyEnvironment {
public:
    SkyEnvironment()=default;
    explicit SkyEnvironment(SkySettings settings){configure(settings);}
    SkySettings const& settings() const noexcept{return settings_;}
    void configure(SkySettings next) {
        validate(next);
        settings_=next;
        ++revision_;
    }
    void reset() noexcept {settings_=SkySettings{};elapsed_days_=0;elapsed_seconds_=0;revision_++;}
    void set_time(double hour) {
        if(!std::isfinite(hour)||hour<0||hour>=24)
            throw std::invalid_argument("Sky time must be [0,24) hours");
        settings_.time_of_day=hour;++revision_;
    }
    void advance(double delta_seconds) {
        if(!std::isfinite(delta_seconds)||delta_seconds<0||delta_seconds>1)
            throw std::invalid_argument("Sky delta must be [0,1] seconds");
        if(settings_.paused||!settings_.enabled||delta_seconds==0)return;
        const auto days=delta_seconds/settings_.day_length_seconds;
        settings_.time_of_day=std::fmod(settings_.time_of_day+24.*days,24.);
        elapsed_days_+=days;
        elapsed_seconds_+=delta_seconds;
    }
    SkySnapshot snapshot() const noexcept {
        SkySnapshot s{};s.settings=settings_;
        constexpr double pi=3.14159265358979323846;
        const double lat=settings_.latitude_degrees*pi/180;
        const double decl=23.44*pi/180.*
            std::sin(2.*pi*(settings_.day_of_year-81.)/365.);
        const double hour=(settings_.time_of_day-12.)*pi/12.;
        auto vector=[&](double angle,double tilt)->SkyDirection {
            const double x=-std::cos(tilt)*std::sin(angle);
            const double y=std::sin(lat)*std::sin(tilt)+
                std::cos(lat)*std::cos(tilt)*std::cos(angle);
            const double z=-std::cos(lat)*std::sin(tilt)+
                std::sin(lat)*std::cos(tilt)*std::cos(angle);
            return {static_cast<float>(x),static_cast<float>(y),
                static_cast<float>(z)};
        };
        s.sun_direction=vector(hour,decl);
        s.moon_direction=vector(hour+settings_.moon_phase*2.*pi,-decl);
        s.sun_elevation_degrees=std::asin(std::clamp(
            double(s.sun_direction.y),-1.,1.))*180./pi;
        s.moon_elevation_degrees=std::asin(std::clamp(
            double(s.moon_direction.y),-1.,1.))*180./pi;
        s.moon_illumination=(1.-std::cos(
            2.*pi*settings_.moon_phase))*.5;
        s.daylight=std::clamp((double(s.sun_direction.y)+.10)/.27,0.,1.);
        s.daylight=s.daylight*s.daylight*(3.-2.*s.daylight);
        s.cloud_offset=std::fmod(elapsed_seconds_*settings_.cloud_speed,1.);
        s.flow_phase=s.cloud_offset;
        s.revision=revision_;
        return s;
    }
    static void validate(SkySettings const& s) {
        auto range=[](double v,double lo,double hi,char const* what) {
            if(!std::isfinite(v)||v<lo||v>hi)
                throw std::invalid_argument(what);
        };
        range(s.time_of_day,0,23.999999999,"Sky timeOfDay");
        range(s.day_length_seconds,30,86400,"Sky dayLengthSeconds");
        range(s.latitude_degrees,-90,90,"Sky latitudeDegrees");
        if(s.day_of_year<1||s.day_of_year>365)
            throw std::invalid_argument("Sky dayOfYear must be 1..365");
        range(s.moon_phase,0,1,"Sky moonPhase");
        range(s.cloud_coverage,0,1,"Sky cloudCoverage");
        range(s.cloud_speed,0,1,"Sky cloudSpeed");
        range(s.haze,0,1,"Sky haze");
        range(s.fog_density,0,.25,"Sky fogDensity");
        range(s.sun_intensity,0,8,"Sky sunIntensity");
        range(s.moon_intensity,0,4,"Sky moonIntensity");
        range(s.star_density,0,1,"Sky starDensity");
        if(s.preset.size()>96 || s.preset.rfind("sky-",0)!=0 ||
           s.preset.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=
               std::string::npos)
            throw std::invalid_argument("Sky preset must be a safe sky-* identifier");
    }

    // Stable equirectangular RGBA8 sky texture. This is a CPU reference
    // renderer for all providers and can later be swapped for shader LUTs
    // without changing the public API. The optional native YTD texture is
    // preserved as cloud-scale modulation rather than discarded.
    // GPU upload occurs only when the sky source/preset changes. Unlike
    // render_rgba(), this texture contains cloud fields rather than the final
    // coloured sky: R=projection, G=breakup, B/A=flow vector.
    // An authored cloud map is retained instead of being mistaken for a
    // blue sky photograph in the GPU atmospheric shader.
    static std::vector<std::uint8_t> render_gpu_cloud_rgba(
        std::uint32_t width,std::uint32_t height,
        SkyTextureLayers const* layers=nullptr) {
        if(width<16||height<8||width>2048||height>1024)
            throw std::invalid_argument("GPU Sky cloud atlas dimensions invalid");
        const bool authored=layers && layers->valid();
        auto fract=[](double x){return x-std::floor(x);};
        auto hash=[](int x,int y)->double {
            std::uint32_t h=std::uint32_t(x)*0x9e3779b9u+
                std::uint32_t(y)*0x85ebca6bu+0x3198afe5u;
            h^=h>>16;h*=0x7feb352du;h^=h>>15;
            h*=0x846ca68bu;h^=h>>16;
            return double(h&65535u)/65535.;
        };
        auto tile_noise=[&](double u,double v,int period,double vscale) {
            double x=fract(u)*period;
            const double y=std::clamp(v,0.,1.)*vscale;
            int x0=int(std::floor(x))%period;
            const int x1=(x0+1)%period;
            const int y0=int(std::floor(y)),y1=y0+1;
            const double tx=x-std::floor(x),ty=y-y0;
            const double sx=tx*tx*(3.-2.*tx),sy=ty*ty*(3.-2.*ty);
            return (hash(x0,y0)*(1.-sx)+hash(x1,y0)*sx)*(1.-sy)+
                   (hash(x0,y1)*(1.-sx)+hash(x1,y1)*sx)*sy;
        };
        auto sample=[&](SkyLayerImage const& tex,double u,double v,unsigned ch) {
            const double xf=fract(u)*tex.width;
            const double yf=std::clamp(v,0.,1.)*(tex.height-1);
            const auto x0=std::uint32_t(std::floor(xf))%tex.width;
            const auto x1=(x0+1)%tex.width;
            const auto y0=std::uint32_t(std::floor(yf));
            const auto y1=std::min(y0+1,tex.height-1);
            const double tx=xf-std::floor(xf),ty=yf-y0;
            auto pixel=[&](std::uint32_t x,std::uint32_t y) {
                return tex.rgba8[(std::size_t(y)*tex.width+x)*4+ch]/255.;
            };
            return (pixel(x0,y0)*(1.-tx)+pixel(x1,y0)*tx)*(1.-ty)+
                   (pixel(x0,y1)*(1.-tx)+pixel(x1,y1)*tx)*ty;
        };
        std::vector<std::uint8_t> rgba(std::size_t(width)*height*4);
        for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
            const double u=(double(x)+.5)/width;
            const double v=(double(y)+.5)/height;
            const double n0=.56*tile_noise(u,v,9,7)+
                .31*tile_noise(u,v,21,13)+
                .13*tile_noise(u,v,47,31);
            const double n1=.50*tile_noise(u+.17,v,17,12)+
                .34*tile_noise(u+.43,v,37,24)+
                .16*tile_noise(u+.07,v,67,39);
            const double projection=authored?
                .76*sample(layers->cloud_projection,u*2.,v*1.15,0)+.24*n0:n0;
            const double breakup=authored?
                .70*sample(layers->cloud_breakup,u*4.5,v*2.5,0)+.30*n1:n1;
            const double fx=authored?sample(layers->flow_phase,u*2.,v*2.,0):
                tile_noise(u+.12,v,7,6);
            const double fy=authored?sample(layers->flow_phase,u*2.,v*2.,1):
                tile_noise(u+.53,v,7,6);
            const auto offset=(std::size_t(y)*width+x)*4;
            for(unsigned c=0;c<4;++c) {
                // R/G: weather/breakup, B: periodic high-pass dither,
                // A: authored flow Y. No new GPU texture/descriptor needed.
                const double value=c==0?projection:c==1?breakup:c==3?fy:fx;
                rgba[offset+c]=c==2?
                    sky_noise::tile64[(std::size_t(y&63u)*64u)+(x&63u)]:
                    static_cast<std::uint8_t>(
                        std::clamp(value,0.,1.)*255.+.5);
            }
        }
        return rgba;
    }

    std::vector<std::uint8_t> render_rgba(std::uint32_t width,
        std::uint32_t height,const std::uint8_t* native_ytd=nullptr,
        SkyTextureLayers const* layers=nullptr) const {
        if(width<16||height<8||width>2048||height>1024)
            throw std::invalid_argument("Sky RGBA size must fit 16..2048 x 8..1024");
        const auto sky=snapshot();
        constexpr double pi=3.14159265358979323846;
        auto fract=[](double x){return x-std::floor(x);};
        // Provider-neutral source sky layer sampler. All GPU backends receive
        // the same composited equirectangular RGBA8 map from this function.
        auto sample=[](SkyLayerImage const& tex,double u,double v)
            ->std::array<double,3> {
            if(!tex.valid())return {0.,0.,0.};
            u=u-std::floor(u);
            v=std::clamp(v,0.,1.);
            const double xf=u*tex.width;
            const double yf=v*(tex.height-1);
            const auto x0=std::uint32_t(std::floor(xf))%tex.width;
            const auto x1=(x0+1)%tex.width;
            const auto y0=std::uint32_t(std::floor(yf));
            const auto y1=std::min(y0+1,tex.height-1);
            const double fx=xf-std::floor(xf),fy=yf-y0;
            std::array<double,3> c{};
            for(unsigned channel=0;channel<3;++channel) {
                auto px=[&](std::uint32_t x,std::uint32_t y)->double {
                    return tex.rgba8[(std::size_t(y)*tex.width+x)*4+channel]/255.;
                };
                const double top=px(x0,y0)*(1.-fx)+px(x1,y0)*fx;
                const double bottom=px(x0,y1)*(1.-fx)+px(x1,y1)*fx;
                c[channel]=top*(1.-fy)+bottom*fy;
            }
            return c;
        };
        const bool has_layers=layers && layers->valid();
        auto hash=[](int x,int y)->double {
            std::uint32_t h=uint32_t(x)*0x9e3779b9u+
                uint32_t(y)*0x85ebca6bu+0x3198afe5u;
            h^=h>>16;h*=0x7feb352du;h^=h>>15;
            h*=0x846ca68bu;h^=h>>16;
            return double(h&65535u)/65535.;
        };
        auto noise=[&](double x,double y) {
            const int ix=int(std::floor(x)),iy=int(std::floor(y));
            const auto dx=x-ix,dy=y-iy;
            const auto sx=dx*dx*(3-2*dx),sy=dy*dy*(3-2*dy);
            const auto a=hash(ix,iy)*(1-sx)+hash(ix+1,iy)*sx;
            const auto b=hash(ix,iy+1)*(1-sx)+hash(ix+1,iy+1)*sx;
            return a*(1-sy)+b*sy;
        };
        std::vector<std::uint8_t> image(std::size_t(width)*height*4);
        auto dot=[](SkyDirection a,SkyDirection b) {
            return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;
        };
        for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
            const auto u=(double(x)+.5)/width;
            const auto v=(double(y)+.5)/height;
            auto smooth=[](double lo,double hi,double value) {
                auto t=std::clamp((value-lo)/(hi-lo),0.,1.);
                return t*t*(3.-2.*t);
            };
            // Equirectangular mapping, horizon at v=.5; wrapped azimuth.
            const double longitude=2.*pi*u;
            const double elevation=pi*(v-.5);
            const SkyDirection direction{
                float(std::cos(elevation)*std::sin(longitude)),
                float(std::sin(elevation)),
                float(std::cos(elevation)*std::cos(longitude))};
            const double day=sky.daylight;
            const double horizon=std::pow(1.-std::abs(direction.y),3.);
            const double twilight=std::clamp(1.-std::abs(sky.sun_elevation_degrees)/18.,0.,1.);
            double rgb[3]={
                (1.-day)*(.010+.025*horizon)+day*(.13+.31*horizon),
                (1.-day)*(.013+.020*horizon)+day*(.31+.33*horizon),
                (1.-day)*(.032+.045*horizon)+day*(.62+.24*horizon)
            };
            const double sunset=(1.-day)*twilight*horizon*.58;
            rgb[0]+=sunset;rgb[1]+=sunset*.29;rgb[2]+=sunset*.08;
            const double haze=settings_.haze*horizon*(.35+.65*day);
            rgb[0]=rgb[0]*(1.-haze*.42)+.59*haze*.42;
            rgb[1]=rgb[1]*(1.-haze*.42)+.62*haze*.42;
            rgb[2]=rgb[2]*(1.-haze*.42)+.67*haze*.42;
            if(has_layers) {
                // The original PC sky HDR is preserved as BC6H DDS; the
                // compatible YTD version is its tone-mapped source preview.
                // This is a real scene texture, not synthetic cloud noise.
                auto const source=sample(layers->atmosphere,u,v);
                const double blend=.78;
                for(int channel=0;channel<3;++channel)
                    rgb[channel]=rgb[channel]*(1.-blend)+
                        std::pow(source[channel],2.2)*blend;
            }
            const double sun_dot=dot(direction,sky.sun_direction);
            const double moon_dot=dot(direction,sky.moon_direction);
            const double sunglow=settings_.sun?std::exp(-45.*(1.-sun_dot))*day*
                settings_.sun_intensity:0.;
            rgb[0]+=sunglow*.34;rgb[1]+=sunglow*.22;rgb[2]+=sunglow*.07;
            if(settings_.sun&&sun_dot>std::cos(.012)&&sky.sun_direction.y>0.) {
                rgb[0]=1.;rgb[1]=.91;rgb[2]=.70;
            }
            if(settings_.moon&&sky.moon_illumination>.02 &&
               moon_dot>std::cos(.016)&&sky.moon_direction.y>0.) {
                const auto moon=settings_.moon_intensity*
                    sky.moon_illumination;
                rgb[0]+=.55*moon;rgb[1]+=.60*moon;rgb[2]+=.73*moon;
            }
            if(settings_.stars&&sky.daylight<.20&&direction.y>.01f) {
                // Sidereal rotation: the starfield moves with world time.
                const int sx=int(std::floor(fract(u+
                    sky.settings.time_of_day/24.)*1024));
                const int sy=int(std::floor(v*512));
                const auto candidate=hash(sx,sy);
                if(candidate>1.-settings_.star_density*.011) {
                    const auto strength=(1.-sky.daylight*5.)*
                        (candidate-(1.-settings_.star_density*.011))/
                        std::max(.0001,settings_.star_density*.011);
                    for(int c=0;c<3;++c)rgb[c]+=.75*strength;
                }
            }
            const double cloud_visibility=smooth(.015,.16,direction.y);
            if(settings_.clouds&&settings_.cloud_coverage>0.001 &&
               cloud_visibility>0.) {
                const auto cx=u*18.+sky.cloud_offset*18.;
                const auto cy=v*10.;
                auto density=.64*noise(cx,cy)+
                    .25*noise(cx*2.2,cy*2.2)+
                    .11*noise(cx*4.5,cy*4.5);
                if(has_layers) {
                    // Two independent moving cloud layers. The original
                    // g_tNdFetchFlowPhase01Map modulates the UV displacement;
                    // projection and breakup are sampled from authored maps.
                    auto const flow=sample(layers->flow_phase,
                        u*2.4+sky.flow_phase*.16,v*2.4);
                    const double du=(flow[0]-.5)*.22;
                    const double dv=(flow[1]-.5)*.14;
                    auto const projected=sample(layers->cloud_projection,
                        u*2.0+sky.flow_phase*.21+du,
                        v*1.1+dv);
                    auto const breakup=sample(layers->cloud_breakup,
                        u*4.5-sky.flow_phase*.13-du*.4,
                        v*2.7-dv*.5);
                    density=std::clamp(projected[0]*.68+
                        breakup[0]*.22+noise(cx*.8,cy*.8)*.10,0.,1.);
                }
                if(native_ytd) {
                    const auto idx=(std::size_t(y)*width+x)*4;
                    const double source=(double(native_ytd[idx])+
                        native_ytd[idx+1]+native_ytd[idx+2])/765.;
                    density=.8*density+.2*source;
                }
                const auto threshold=.77-.60*settings_.cloud_coverage;
                const auto coverage=smooth(threshold-.04,threshold+.24,density)*
                    cloud_visibility;
                const auto shading=(.12+.78*day)*
                    std::max(.1,settings_.sun_intensity);
                for(int c=0;c<3;++c)
                    rgb[c]=rgb[c]*(1.-coverage*.68)+
                        shading*coverage*.68;
            }
            // Fill the whole lower hemisphere and converge to the same
            // horizon haze used by the native atmospheric providers.
            const auto sky_visibility=smooth(-.015,.105,direction.y);
            const auto lower=1.-smooth(-.38,-.06,direction.y);
            const auto twilight_glow=settings_.sun?
                twilight*std::pow(std::max(0.,sun_dot),6.)*
                (1.-smooth(.04,.32,std::abs(direction.y))):0.;
            constexpr double night_haze[]{.055,.075,.11};
            constexpr double day_haze[]{.23,.315,.395};
            constexpr double warm_scatter[]{.34,.09,.015};
            for(unsigned c=0;c<3;++c) {
                auto const fill=(night_haze[c]+(day_haze[c]-night_haze[c])*day+
                    warm_scatter[c]*twilight_glow)*(1.-.44*lower);
                rgb[c]=fill*(1.-sky_visibility)+rgb[c]*sky_visibility;
            }
            const auto idx=(std::size_t(y)*width+x)*4;
            for(int c=0;c<3;++c)
                image[idx+c]=std::uint8_t(std::clamp(
                    std::pow(std::clamp(rgb[c],0.,1.),1./2.2)*255.,0.,255.));
            image[idx+3]=255;
        }
        return image;
    }
private:
    SkySettings settings_{};
    double elapsed_days_=0;
    double elapsed_seconds_=0;
    std::uint64_t revision_=0;
};
} // namespace northstarcpp
