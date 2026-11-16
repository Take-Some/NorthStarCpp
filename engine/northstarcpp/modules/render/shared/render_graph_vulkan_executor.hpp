#pragma once
// Provider-independent Vulkan Sync2 command-buffer executor for RenderGraph.
// The caller owns VkDevice, VkCommandBuffer, VkImage resources and render-pass
// recording. This executor emits layout/access/sync2 barriers in plan order;
// it does not implement a NorthStar engine.render Vulkan GPU provider.
#include "render_graph_vulkan_sync.hpp"
#include "../../shared/frame_memory_counters.hpp"
#include <array>
#include <functional>
#include <stdexcept>
#include <vector>

namespace northstarcpp::render {
struct VulkanGraphImage {
    VkImage image=VK_NULL_HANDLE;
    VkImageSubresourceRange range{};
    std::uint32_t src_queue_family=VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t dst_queue_family=VK_QUEUE_FAMILY_IGNORED;
    bool discard_on_first_use=false;
};
inline void record_vulkan_graph(
    VkCommandBuffer command,PFN_vkCmdPipelineBarrier2 cmd_barrier,
    const RenderGraphPlan& plan,
    VulkanGraphImage const* images,std::size_t image_count,
    const std::function<void(PassId,VkCommandBuffer)>& pass_recorder) {
    if(command==VK_NULL_HANDLE || !cmd_barrier || !pass_recorder ||
       (image_count && !images) || image_count!=plan.lifetimes.size())
        throw std::invalid_argument("Incomplete Vulkan graph execution contract");

    // Small passes record barriers in place. Bigger passes share one
    // overflow allocation across the execution.
    std::array<VkImageMemoryBarrier2,8> inline_barriers;
    std::vector<VkImageMemoryBarrier2> overflow;
    for(std::size_t index=0;index<plan.passes.size();++index) {
        auto const& pass=plan.passes[index];
        auto const count=pass.transitions.size();
        auto* barriers=inline_barriers.data();
        if(count>inline_barriers.size()) {
            diagnostics::frame_memory::resize(overflow,count);
            barriers=overflow.data();
        }
        for(std::size_t i=0;i<count;++i) {
            auto transition=pass.transitions[i];
            if(transition.resource>=image_count)
                throw std::out_of_range("Vulkan graph resource ID outside binding table");
            auto const& image=images[transition.resource];
            if(image.discard_on_first_use &&
               plan.lifetimes[transition.resource].first==static_cast<int>(index))
                transition.before=ResourceAccess::Undefined;
            barriers[i]=vulkan_sync2_image_barrier(
                transition,image.image,image.range,
                image.src_queue_family,image.dst_queue_family);
        }
        if(count) {
            VkDependencyInfo deps{};
            deps.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            deps.imageMemoryBarrierCount=static_cast<std::uint32_t>(count);
            deps.pImageMemoryBarriers=barriers;
            cmd_barrier(command,&deps);
        }
        pass_recorder(pass.id,command);
    }
}

inline void record_vulkan_graph(
    VkCommandBuffer command,PFN_vkCmdPipelineBarrier2 cmd_barrier,
    const RenderGraphPlan& plan,
    const std::vector<VulkanGraphImage>& images,
    const std::function<void(PassId,VkCommandBuffer)>& pass_recorder) {
    record_vulkan_graph(command,cmd_barrier,plan,images.data(),images.size(),pass_recorder);
}
} // namespace northstarcpp::render
