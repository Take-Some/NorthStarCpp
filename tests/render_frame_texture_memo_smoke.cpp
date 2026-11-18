// Non-GPU test of the full-resolution per-submission texture memo.
// Alias identity is safe ONLY inside one synchronous material submission.
#include "../engine/northstarcpp/modules/render/shared/frame_texture_memo.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <stdexcept>
int main() {
    using northstarcpp::render::FrameTextureMemo;
    std::array<std::uint8_t,16> pixels{1,2,3,4,5,6,7,8};
    std::array<std::uint8_t,16> clone=pixels;
    NorthStarMaterialTexture img{pixels.data(),2,2,16};
    NorthStarMaterialTexture same{pixels.data(),2,2,16};
    NorthStarMaterialTexture copied{clone.data(),2,2,16};
    int resolves=0;
    auto resolver=[&] {return std::make_shared<int>(++resolves);};
    FrameTextureMemo<std::shared_ptr<int>> frame;
    auto a=frame.get(img,1,resolver);
    assert(frame.get(same,1,resolver)==a);
    assert(resolves==1);
    // Linear and sRGB must not alias, even with the same pixel memory.
    assert(frame.get(img,0,resolver)!=a);
    assert(resolves==2);
    // Different upload source with identical pixels is not pointer-identical.
    // The renderer's existing byte-for-byte cache still deduplicates it.
    assert(frame.get(copied,1,resolver)!=a);
    assert(resolves==3);
    // Different dimensions or byte count must not alias.
    NorthStarMaterialTexture differently_sized{pixels.data(),4,1,16};
    assert(frame.get(differently_sized,1,resolver)!=a);
    assert(resolves==4);
    // Reusing a pointer on a later frame MUST revalidate its contents.
    pixels[0]=230;
    FrameTextureMemo<std::shared_ptr<int>> following;
    assert(following.get(img,1,resolver)!=a);
    assert(resolves==5);
    assert(frame.size()==4 && following.size()==1);
    FrameTextureMemo<std::shared_ptr<int>> rejected;
    try {
        (void)rejected.get(img,1,[]{throw std::runtime_error("bad texture");return std::shared_ptr<int>{};});
        assert(false);
    } catch(std::runtime_error const&) {}
    assert(rejected.size()==0);
    std::puts("D3D12 high-detail shared texture submission: memo isolation PASS");
}
