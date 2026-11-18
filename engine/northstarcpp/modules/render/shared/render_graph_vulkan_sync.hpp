#pragma once
// Vulkan Synchronization2 lowering for backend-neutral RenderGraph accesses.
// CPU-only preparation; the Vulkan GPU provider may use these barriers when
// it is integrated. Does not own VkImage, device, queues or semaphores.
#include "render_graph.hpp"
#include <vulkan/vulkan.h>
#include <stdexcept>

namespace northstarcpp::render {
struct VulkanAccessInfo {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
};
inline VulkanAccessInfo vulkan_sync2_access(ResourceAccess usage) {
    switch(usage) {
    case ResourceAccess::Undefined:
        return {VK_PIPELINE_STAGE_2_NONE,VK_ACCESS_2_NONE,VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceAccess::ShaderRead:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ResourceAccess::ColorAttachment:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case ResourceAccess::DepthWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case ResourceAccess::DepthRead:
        // Depth SRVs are consumed in fragment shaders as well as fixed
        // depth tests. Both execution and memory dependency are required.
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    case ResourceAccess::CopySource:
        return {VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ResourceAccess::CopyDestination:
        return {VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ResourceAccess::Present:
        return {VK_PIPELINE_STAGE_2_NONE,VK_ACCESS_2_NONE,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    throw std::logic_error("Unrecognized render graph Vulkan access");
}
inline VkImageMemoryBarrier2 vulkan_sync2_image_barrier(
    const AccessTransition& transition,VkImage image,
    VkImageSubresourceRange range,
    std::uint32_t source_queue_family=VK_QUEUE_FAMILY_IGNORED,
    std::uint32_t destination_queue_family=VK_QUEUE_FAMILY_IGNORED) {
    if(image==VK_NULL_HANDLE)
        throw std::invalid_argument("RenderGraph synchronization requires VkImage");
    if(!range.aspectMask||!range.levelCount||!range.layerCount)
        throw std::invalid_argument("Invalid image subresource range");
    const auto before=vulkan_sync2_access(transition.before);
    const auto after=vulkan_sync2_access(transition.after);
    VkImageMemoryBarrier2 barrier{};
    barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask=before.stage;
    barrier.srcAccessMask=before.access;
    barrier.dstStageMask=after.stage;
    barrier.dstAccessMask=after.access;
    barrier.oldLayout=before.layout;
    barrier.newLayout=after.layout;
    barrier.srcQueueFamilyIndex=source_queue_family;
    barrier.dstQueueFamilyIndex=destination_queue_family;
    barrier.image=image;
    barrier.subresourceRange=range;
    return barrier;
}
} // namespace northstarcpp::render
