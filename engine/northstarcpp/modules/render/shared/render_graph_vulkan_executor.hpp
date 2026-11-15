#pragma once
// Provider-independent Vulkan Sync2 command-buffer executor for RenderGraph.
// The caller owns VkDevice, VkCommandBuffer, VkImage resources and render-pass
// recording. This executor emits layout/access/sync2 barriers in plan order;
// it does not implement a NorthStar engine.render Vulkan GPU provider.
#include "render_graph_vulkan_sync.hpp"
#include <functional>
#include <stdexcept>
#include <vector>

namespace northstarcpp::render {
struct VulkanGraphImage {
    VkImage image=VK_NULL_HANDLE;
    VkImageSubresourceRange range{};
    std::uint32_t src_queue_family=VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t dst_queue_family=VK_QUEUE_FAMILY_IGNORED;
};
inline void record_vulkan_graph(
    VkCommandBuffer command,PFN_vkCmdPipelineBarrier2 cmd_barrier,
    const RenderGraphPlan& plan,
    const std::vector<VulkanGraphImage>& images,
    const std::function<void(PassId,VkCommandBuffer)>& pass_recorder) {
    if(command==VK_NULL_HANDLE||!cmd_barrier||!pass_recorder||
       images.size()!=plan.lifetimes.size())
        throw std::invalid_argument("Incomplete Vulkan graph execution contract");
    for(const auto& pass:plan.passes) {
        std::vector<VkImageMemoryBarrier2> barriers;
        barriers.reserve(pass.transitions.size());
        for(const auto& transition:pass.transitions) {
            if(transition.resource>=images.size())
                throw std::out_of_range("Vulkan graph resource ID outside binding table");
            const auto& image=images[transition.resource];
            barriers.push_back(vulkan_sync2_image_barrier(
                transition,image.image,image.range,
                image.src_queue_family,image.dst_queue_family));
        }
        if(!barriers.empty()) {
            VkDependencyInfo deps{};
            deps.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            deps.imageMemoryBarrierCount=static_cast<std::uint32_t>(barriers.size());
            deps.pImageMemoryBarriers=barriers.data();
            cmd_barrier(command,&deps);
        }
        pass_recorder(pass.id,command);
    }
}
}
