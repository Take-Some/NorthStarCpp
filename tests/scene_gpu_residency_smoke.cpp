#include <northstarcpp/scene_gpu_residency.hpp>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace northstarcpp::scene_gpu;
namespace {
void require(bool v,char const* m) { if(!v)throw std::runtime_error(m); }
ResourceKey mesh(std::string name) {return {ResourceKind::vertex_buffer,std::move(name)};}
}
int main() {
    std::uint64_t next=1;
    std::vector<std::uint64_t> freed;
    std::size_t uploads=0;
    SectorGpuResidency residency(
        [&](ResourceKey const& key) {
            ++uploads;
            require(!key.asset.empty(),"empty asset uploaded");
            return GpuAllocation{next++,4096,2};
        },
        [&](ResourceKey const&,std::uint64_t handle){freed.push_back(handle);}
    );
    auto common=mesh("props/shared.ydd");
    auto alpha=mesh("district/a.ydd");
    auto beta=mesh("district/b.ydd");
    residency.replace_sector("A",{common,alpha,common});
    residency.replace_sector("B",{common,beta});
    require(uploads==3,"shared resource uploaded twice");
    auto const sector_a=residency.sector_stats("A");
    require(sector_a.resources==2 && sector_a.referenced_bytes==8192 &&
            sector_a.exclusive_bytes==4096 && sector_a.shared_bytes==4096,
            "per-sector shared and exclusive GPU accounting incorrect");
    require(residency.stats().shared_allocations==1 &&
            residency.stats().allocated_bytes==3*4096,
            "shared residency bytes/owners incorrect");
    residency.mark_submitted("A",7);
    residency.mark_submitted("B",9);
    residency.unload_sector("A");
    require(residency.collect(6)==0,"released before GPU fence");
    require(residency.collect(7)==1 && freed.size()==1,
            "failed to release unshared resource after fence");
    residency.unload_sector("B");
    require(residency.stats().pending_releases==2,
            "shared retirement did not wait for last owner");
    residency.replace_sector("C",{common});
    require(uploads==3,"resurrection reuploaded pending resource");
    require(residency.collect(9)==1,"unexpected resource release after resurrection");
    require(residency.stats().allocations==1 &&
            residency.stats().pending_releases==0,
            "resurrection lost live GPU resource");
    residency.unload_sector("C");
    require(residency.collect(9)==1,"shared resource not eventually freed");
    require(residency.stats().allocations==0 && freed.size()==3,
            "leaked retired GPU resource");

    // Provider failure must not alter sector ownership. Completed upload
    // allocations become orphaned until their transfer fences retire.
    std::size_t attempted=0;
    std::vector<std::uint64_t> cleaned;
    SectorGpuResidency unreliable(
        [&](ResourceKey const&) -> GpuAllocation {
            if(++attempted==2)throw std::runtime_error("simulated upload failure");
            return {50+attempted,2048,12};
        },
        [&](ResourceKey const&,std::uint64_t h){cleaned.push_back(h);}
    );
    bool failed=false;
    try {unreliable.replace_sector("X",{mesh("a"),mesh("b")});}
    catch(std::runtime_error const&){failed=true;}
    require(failed && unreliable.stats().sectors==0 &&
            unreliable.stats().pending_releases==1,
            "failed upload partially committed sector");
    require(unreliable.collect(11)==0 && cleaned.empty(),
            "staged upload released before transfer completed");
    require(unreliable.collect(12)==1 && cleaned.size()==1 &&
            unreliable.stats().allocations==0,
            "orphan GPU allocation did not retire");
    return 0;
}
