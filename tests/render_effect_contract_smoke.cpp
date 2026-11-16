// NorthStar cross-provider rendering contracts, no GPU required.
#include <northstarcpp/render_postfx_api.h>
#include "../engine/northstarcpp/modules/render/shared/postfx_look.hpp"
#include "../engine/northstarcpp/modules/render/shared/gbuffer_semantics.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace ns=northstarcpp::render;
static void check(bool condition,char const* message) {
    if(!condition)throw std::runtime_error(message);
}
int main() {
    try {
        auto look=ns::neutral_postfx();
        check(ns::valid_postfx(look),"Neutral PostFX invalid");
        check(!ns::active_postfx(look),"Neutral look must require no extra pass");
        look.exposure_ev=2.f;
        check(ns::valid_postfx(look)&&ns::active_postfx(look),"EV grading rejected");
        look=ns::neutral_postfx();
        look.contrast=1.2f;
        check(ns::valid_postfx(look)&&ns::active_postfx(look),"Contrast rejected");
        look.saturation=.5f;
        look.vignette=.4f;
        look.chromatic_pixels=1.75f;
        look.film_grain=.05f;
        check(ns::valid_postfx(look),"Combined film look rejected");
        auto reject=[&](auto field,float value) {
            auto bad=look;
            bad.*field=value;
            check(!ns::valid_postfx(bad),"Invalid film parameter accepted");
        };
        reject(&NorthStarRenderPostFxSettings::film_grain,-.001f);
        reject(&NorthStarRenderPostFxSettings::film_grain,.201f);
        reject(&NorthStarRenderPostFxSettings::vignette,2.f);
        reject(&NorthStarRenderPostFxSettings::contrast,-1.f);
        reject(&NorthStarRenderPostFxSettings::chromatic_pixels,6.f);
        reject(&NorthStarRenderPostFxSettings::saturation,
               std::numeric_limits<float>::quiet_NaN());
        reject(&NorthStarRenderPostFxSettings::exposure_ev,
               std::numeric_limits<float>::infinity());
        auto old=look;
        old.struct_size=0;
        check(!ns::valid_postfx(old),"Incorrect ABI struct size accepted");
        old=look;
        old.reserved=1;
        check(!ns::valid_postfx(old),"Unknown feature flags accepted");
        static_assert(ns::gbuffer_mrt_count==3);
        static_assert(int(ns::GbufferDebug::Albedo)==1);
        static_assert(int(ns::GbufferDebug::Normal)==2);
        static_assert(int(ns::GbufferDebug::Roughness)==3);
        static_assert(int(ns::GbufferDebug::Metallic)==4);
        static_assert(int(ns::GbufferDebug::Emissive)==5);
        static_assert(int(ns::GbufferDebug::Depth)==6);
        static_assert(int(ns::GbufferDebug::AmbientOcclusion)==7);
        static_assert(int(ns::GbufferDebug::Coverage)==8);
        std::cout<<"NorthStar GBuffer and Film Look contracts: PASS\n";
        return 0;
    }catch(std::exception const& e){
        std::cerr<<"NorthStar rendering contract FAIL: "<<e.what()<<'\n';
        return 1;
    }
}
