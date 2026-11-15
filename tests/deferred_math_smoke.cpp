#include "../engine/northstarcpp/modules/render/shared/deferred_math.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
int main() {
    using northstarcpp::render::inverse_view_projection;
    float const diagonal[16]{2,0,0,0, 0,3,0,0, 0,0,4,0, 0,0,0,1};
    auto const inv=inverse_view_projection(diagonal);
    assert(std::abs(inv[0]-.5f)<.00001f);
    assert(std::abs(inv[5]-1.f/3.f)<.00001f);
    assert(std::abs(inv[10]-.25f)<.00001f);
    assert(std::abs(inv[15]-1.f)<.00001f);
    float const translation[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 3,4,5,1};
    auto const translated=inverse_view_projection(translation);
    assert(std::abs(translated[12]+3.f)<.00001f);
    assert(std::abs(translated[13]+4.f)<.00001f);
    assert(std::abs(translated[14]+5.f)<.00001f);
    bool threw=false;
    try { float const singular[16]{}; (void)inverse_view_projection(singular); }
    catch(std::invalid_argument const&) { threw=true; }
    assert(threw);
    return 0;
}
