#include "../engine/northstarcpp/modules/render/shared/render_graph_vulkan_sync.hpp"
#include <stdexcept>
#define REQUIRE(condition) do { if(!(condition))     throw std::runtime_error("Vulkan sync2 acceptance failed: " #condition); } while(false)
int main() {
    using namespace northstarcpp::render;
    RenderGraph g;
    auto color=g.add_resource("gbuffer");
    auto p0=g.add_pass({"gbuffer",{}, {color},{},
        {{color,ResourceAccess::ColorAttachment}}});
    g.add_pass({"resolve",{color},{},{p0},
        {{color,ResourceAccess::ShaderRead}}});
    auto plan=g.compile();
    REQUIRE(plan.passes.size()==2);
    REQUIRE(plan.passes[1].transitions.size()==1);
    auto change=plan.passes[1].transitions[0];
    REQUIRE(change.before==ResourceAccess::ColorAttachment);
    REQUIRE(change.after==ResourceAccess::ShaderRead);
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    auto image=reinterpret_cast<VkImage>(static_cast<uintptr_t>(0x1234));
    auto barrier=vulkan_sync2_image_barrier(change,image,range);
    REQUIRE(barrier.sType==VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    REQUIRE(barrier.srcStageMask==VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    REQUIRE((barrier.srcAccessMask&VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)!=0);
    REQUIRE(barrier.dstStageMask&VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    REQUIRE(barrier.dstAccessMask==VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    REQUIRE(barrier.oldLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    REQUIRE(barrier.newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    REQUIRE(barrier.srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
    REQUIRE(barrier.dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
    auto depth=vulkan_sync2_access(ResourceAccess::DepthWrite);
    REQUIRE(depth.layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    REQUIRE(depth.access&VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    auto copied=vulkan_sync2_access(ResourceAccess::CopyDestination);
    REQUIRE(copied.layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    REQUIRE(copied.stage==VK_PIPELINE_STAGE_2_COPY_BIT);
    auto presented=vulkan_sync2_access(ResourceAccess::Present);
    REQUIRE(presented.layout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    auto first=vulkan_sync2_access(ResourceAccess::Undefined);
    REQUIRE(first.layout==VK_IMAGE_LAYOUT_UNDEFINED);
    RenderGraph imported_graph;
    auto imported=imported_graph.add_resource("swap",true,ResourceAccess::Present);
    imported_graph.add_pass({"acquire",{}, {imported},{},
        {{imported,ResourceAccess::ColorAttachment}}});
    auto acquire_plan=imported_graph.compile();
    REQUIRE(acquire_plan.passes[0].transitions.size()==1);
    const auto acquire=acquire_plan.passes[0].transitions[0];
    REQUIRE(acquire.before==ResourceAccess::Present);
    REQUIRE(acquire.after==ResourceAccess::ColorAttachment);
    auto acquire_barrier=vulkan_sync2_image_barrier(acquire,image,range);
    REQUIRE(acquire_barrier.oldLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    REQUIRE(acquire_barrier.newLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    bool rejected=false;
    try{(void)vulkan_sync2_image_barrier(change,VK_NULL_HANDLE,range);}
    catch(const std::invalid_argument&){rejected=true;}
    REQUIRE(rejected);
}
