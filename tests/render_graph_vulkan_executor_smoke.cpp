#include "../engine/northstarcpp/modules/render/shared/render_graph_vulkan_executor.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
#define REQUIRE(condition) do { if(!(condition)) \
    throw std::runtime_error("Vulkan graph executor failed: " #condition); } while(false)
namespace {
struct BarrierRecord {
    VkImage image;
    VkImageLayout before,after;
};
std::vector<BarrierRecord> recorded;
unsigned dispatches=0;
void VKAPI_CALL mock_barrier(VkCommandBuffer command,const VkDependencyInfo* info) {
    REQUIRE(command!=VK_NULL_HANDLE);
    REQUIRE(info!=nullptr && info->sType==VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    ++dispatches;
    for(std::uint32_t i=0;i<info->imageMemoryBarrierCount;++i) {
        const auto& b=info->pImageMemoryBarriers[i];
        REQUIRE(b.sType==VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
        recorded.push_back({b.image,b.oldLayout,b.newLayout});
    }
}
}
int main() {
    using namespace northstarcpp::render;
    RenderGraph g;
    auto scene=g.add_resource("scene");
    auto output=g.add_resource("swapchain",true,ResourceAccess::Present);
    auto p0=g.add_pass({"opaque",{}, {scene,output},{},
        {{scene,ResourceAccess::ColorAttachment},
         {output,ResourceAccess::ColorAttachment}}});
    auto p1=g.add_pass({"postfx",{scene},{output},{p0},
        {{scene,ResourceAccess::ShaderRead},
         {output,ResourceAccess::ColorAttachment}}});
    auto p2=g.add_pass({"present",{output},{},{p1},
        {{output,ResourceAccess::Present}}});
    auto plan=g.compile();
    VkCommandBuffer cmd=reinterpret_cast<VkCommandBuffer>(static_cast<std::uintptr_t>(0x1234));
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    std::vector<VulkanGraphImage> images(2);
    images[scene].image=reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x100));
    images[scene].range=range;
    images[output].image=reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x200));
    images[output].range=range;
    std::vector<PassId> order;
    record_vulkan_graph(cmd,mock_barrier,plan,images,
        [&](PassId id,VkCommandBuffer buffer) {
            REQUIRE(buffer==cmd);
            order.push_back(id);
        });
    REQUIRE(order.size()==3);
    REQUIRE(order[0]==p0 && order[1]==p1 && order[2]==p2);
    REQUIRE(dispatches==3);
    REQUIRE(recorded.size()==4);
    REQUIRE(std::any_of(recorded.begin(),recorded.end(),[&](const auto& t) {
        return t.image==images[output].image &&
               t.before==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
               t.after==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }));
    REQUIRE(std::any_of(recorded.begin(),recorded.end(),[&](const auto& t) {
        return t.image==images[output].image &&
               t.before==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               t.after==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }));
    recorded.clear();
    images[output].discard_on_first_use=true;
    record_vulkan_graph(cmd,mock_barrier,plan,images,[](PassId,VkCommandBuffer) {});
    REQUIRE(recorded[1].image==images[output].image &&
            recorded[1].before==VK_IMAGE_LAYOUT_UNDEFINED &&
            recorded.back().after==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    REQUIRE(plan.passes[0].transitions[1].before==ResourceAccess::Present);

    RenderGraph wide;
    Pass bulk{"bulk"};
    std::vector<VulkanGraphImage> wide_images;
    for(unsigned i=0;i<10;++i) {
        auto id=wide.add_resource("image"+std::to_string(i));
        bulk.writes.push_back(id);
        bulk.usages.push_back({id,ResourceAccess::ColorAttachment});
        wide_images.push_back({images[0].image,range});
    }
    auto const bulk_pass=wide.add_pass(bulk);
    bulk.name="download";
    bulk.after={bulk_pass};
    for(auto& usage:bulk.usages)usage.access=ResourceAccess::CopyDestination;
    wide.add_pass(bulk);
    NorthStarFrameMemoryCounters counters{};
    {
        northstarcpp::diagnostics::frame_memory::Scope scope(&counters);
        record_vulkan_graph(cmd,mock_barrier,wide.compile(),wide_images,[](PassId,VkCommandBuffer) {});
    }
    REQUIRE(counters.allocations==1);

    bool rejected=false;
    images[scene].image=VK_NULL_HANDLE;
    try {
        record_vulkan_graph(cmd,mock_barrier,plan,images,
            [](PassId,VkCommandBuffer) {});
    }catch(const std::invalid_argument&) {rejected=true;}
    REQUIRE(rejected);
}
