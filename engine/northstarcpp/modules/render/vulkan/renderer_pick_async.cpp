// Vulkan GPU ID picking: bounded 3-slot asynchronous fence-polled readback.
// Offscreen resources and vertex/camera snapshots remain valid until each
// slot's submission fence completes, independent of swapchain presentation.
#include "renderer_private.hpp"
#include <northstarcpp/render_pick_api.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
namespace northstarcpp::render::vulkan {
namespace {
void checkAsync(VkResult result,char const* stage) {
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(stage)+
            " VkResult="+std::to_string(int(result)));
}
}
void Renderer::destroy_async_pick() noexcept {
    if(!device_)return;
    for(auto& slot:async_pick_slots_) {
        if(slot.pool)vkDestroyCommandPool(device_,slot.pool,nullptr);
        if(slot.fence)vkDestroyFence(device_,slot.fence,nullptr);
        if(slot.vertices)vkDestroyBuffer(device_,slot.vertices,nullptr);
        if(slot.vertices_memory)vkFreeMemory(device_,slot.vertices_memory,nullptr);
        if(slot.readback)vkDestroyBuffer(device_,slot.readback,nullptr);
        if(slot.readback_memory)vkFreeMemory(device_,slot.readback_memory,nullptr);
        if(slot.color_view)vkDestroyImageView(device_,slot.color_view,nullptr);
        if(slot.depth_view)vkDestroyImageView(device_,slot.depth_view,nullptr);
        if(slot.color)vkDestroyImage(device_,slot.color,nullptr);
        if(slot.depth)vkDestroyImage(device_,slot.depth,nullptr);
        if(slot.color_memory)vkFreeMemory(device_,slot.color_memory,nullptr);
        if(slot.depth_memory)vkFreeMemory(device_,slot.depth_memory,nullptr);
        slot={};
    }
}
void Renderer::invalidate_async_pick(std::uint64_t generation) noexcept {
    minimum_pick_generation_=std::max(minimum_pick_generation_,generation);
}
bool Renderer::enqueue_async_pick(NorthStarRenderPickAsyncRequest const& req) {
    if(!device_||!pick_pipeline_||!extent_.width||!extent_.height ||
       !req.request_id||req.reserved||req.x>=extent_.width||req.y>=extent_.height||
       req.scene_generation<minimum_pick_generation_)
        throw std::invalid_argument("Invalid or stale Vulkan async GPU pick request");
    AsyncPickSlot* slot=nullptr;
    for(auto& candidate:async_pick_slots_) {
        if(candidate.active) {
            auto const finished=!candidate.fence ||
                vkGetFenceStatus(device_,candidate.fence)==VK_SUCCESS;
            if(finished &&
               (candidate.scene_generation<minimum_pick_generation_ ||
                candidate.request_id<req.request_id))
                candidate.active=false; // older click superseded by newer one
        }
        if(!candidate.active && !slot)slot=&candidate;
    }
    if(!slot)return false; // no blocking wait, let the caller retry/fallback
    std::vector<std::uint64_t> tokens;
    auto vertices=build_pick_vertices(tokens);
    if(vertices.size()*sizeof(ColoredVertex)>max_vertex_bytes_)
        throw std::runtime_error("Async Vulkan GPU pick staging budget exceeded");
    if(!slot->fence) {
        VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        info.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        checkAsync(vkCreateFence(device_,&info,nullptr,&slot->fence),
                   "vkCreateFence(async pick)");
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex=family_;
        checkAsync(vkCreateCommandPool(device_,&pool,nullptr,&slot->pool),
                   "vkCreateCommandPool(async pick)");
        VkCommandBufferAllocateInfo alloc{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool=slot->pool;
        alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount=1;
        checkAsync(vkAllocateCommandBuffers(device_,&alloc,&slot->command),
                   "vkAllocateCommandBuffers(async pick)");
    }
    if(vertices.empty()) {
        slot->active=true;slot->request_id=req.request_id;
        slot->scene_generation=req.scene_generation;
        slot->tokens.clear();slot->width=extent_.width;
        slot->height=extent_.height;
        return true; // empty hit is available at poll without GPU submission
    }
    auto createBuffer=[&](VkDeviceSize bytes,VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags flags,VkBuffer& buffer,
                          VkDeviceMemory& memory) {
        VkBufferCreateInfo desc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        desc.size=bytes;desc.usage=usage;
        checkAsync(vkCreateBuffer(device_,&desc,nullptr,&buffer),
                   "vkCreateBuffer(async pick)");
        VkMemoryRequirements need{};
        vkGetBufferMemoryRequirements(device_,buffer,&need);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=need.size;
        alloc.memoryTypeIndex=find_memory_type(need.memoryTypeBits,flags);
        checkAsync(vkAllocateMemory(device_,&alloc,nullptr,&memory),
                   "vkAllocateMemory(async pick)");
        checkAsync(vkBindBufferMemory(device_,buffer,memory,0),
                   "vkBindBufferMemory(async pick)");
    };
    auto const bytes=VkDeviceSize(vertices.size()*sizeof(ColoredVertex));
    if(!slot->vertices || bytes>slot->vertices_capacity) {
        if(slot->vertices)vkDestroyBuffer(device_,slot->vertices,nullptr);
        if(slot->vertices_memory)vkFreeMemory(device_,slot->vertices_memory,nullptr);
        slot->vertices=VK_NULL_HANDLE;slot->vertices_memory=VK_NULL_HANDLE;
        slot->vertices_capacity=std::min<VkDeviceSize>(max_vertex_bytes_,
            std::max<VkDeviceSize>(bytes,slot->vertices_capacity?
                slot->vertices_capacity*2:128u*1024u));
        createBuffer(slot->vertices_capacity,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            slot->vertices,slot->vertices_memory);
    }
    void* mapped=nullptr;
    checkAsync(vkMapMemory(device_,slot->vertices_memory,0,bytes,0,&mapped),
               "vkMapMemory(async pick vertices)");
    std::memcpy(mapped,vertices.data(),std::size_t(bytes));
    vkUnmapMemory(device_,slot->vertices_memory);
    if(!slot->readback) {
        createBuffer(4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            slot->readback,slot->readback_memory);
    }
    auto makeImage=[&](VkFormat format,VkImageUsageFlags usage,
                       VkImageAspectFlags aspect,VkImage& image,
                       VkDeviceMemory& memory,VkImageView& view) {
        VkImageCreateInfo desc{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        desc.imageType=VK_IMAGE_TYPE_2D;desc.format=format;
        desc.extent={extent_.width,extent_.height,1};
        desc.mipLevels=desc.arrayLayers=1;
        desc.samples=VK_SAMPLE_COUNT_1_BIT;
        desc.tiling=VK_IMAGE_TILING_OPTIMAL;
        desc.usage=usage;desc.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        checkAsync(vkCreateImage(device_,&desc,nullptr,&image),
                   "vkCreateImage(async pick)");
        VkMemoryRequirements need{};
        vkGetImageMemoryRequirements(device_,image,&need);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=need.size;
        alloc.memoryTypeIndex=find_memory_type(
            need.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checkAsync(vkAllocateMemory(device_,&alloc,nullptr,&memory),
                   "vkAllocateMemory(async pick image)");
        checkAsync(vkBindImageMemory(device_,image,memory,0),
                   "vkBindImageMemory(async pick)");
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        v.image=image;v.viewType=VK_IMAGE_VIEW_TYPE_2D;
        v.format=format;v.subresourceRange.aspectMask=aspect;
        v.subresourceRange.levelCount=v.subresourceRange.layerCount=1;
        checkAsync(vkCreateImageView(device_,&v,nullptr,&view),
                   "vkCreateImageView(async pick)");
    };
    if(!slot->color || slot->width!=extent_.width ||
       slot->height!=extent_.height) {
        if(slot->color_view)vkDestroyImageView(device_,slot->color_view,nullptr);
        if(slot->depth_view)vkDestroyImageView(device_,slot->depth_view,nullptr);
        if(slot->color)vkDestroyImage(device_,slot->color,nullptr);
        if(slot->depth)vkDestroyImage(device_,slot->depth,nullptr);
        if(slot->color_memory)vkFreeMemory(device_,slot->color_memory,nullptr);
        if(slot->depth_memory)vkFreeMemory(device_,slot->depth_memory,nullptr);
        slot->color=slot->depth=VK_NULL_HANDLE;
        slot->color_view=slot->depth_view=VK_NULL_HANDLE;
        slot->color_memory=slot->depth_memory=VK_NULL_HANDLE;
        makeImage(VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,slot->color,slot->color_memory,slot->color_view);
        makeImage(depth_format_,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT,slot->depth,slot->depth_memory,slot->depth_view);
        slot->width=extent_.width;slot->height=extent_.height;
    }
    checkAsync(vkResetCommandPool(device_,slot->pool,0),
               "vkResetCommandPool(async pick)");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkAsync(vkBeginCommandBuffer(slot->command,&begin),
               "vkBeginCommandBuffer(async pick)");
    VkImageMemoryBarrier2 barriers[2]{};
    for(unsigned i=0;i<2;++i) {
        auto& b=barriers[i];
        b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask=VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.dstStageMask=i==0?VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT:
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        b.dstAccessMask=i==0?VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT:
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        // Every pass clears attachment content, so UNDEFINED discards old
        // pixels even when a reused image was previously TRANSFER_SRC.
        b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout=i==0?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=i==0?slot->color:slot->depth;
        b.subresourceRange.aspectMask=
            i==0?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
        b.subresourceRange.levelCount=b.subresourceRange.layerCount=1;
    }
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount=2;dep.pImageMemoryBarriers=barriers;
    vkCmdPipelineBarrier2(slot->command,&dep);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=slot->color_view;
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView=slot->depth_view;
    depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil={1.f,0};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=extent_;
    info.layerCount=1;
    info.colorAttachmentCount=1;info.pColorAttachments=&color;
    info.pDepthAttachment=&depth;
    vkCmdBeginRendering(slot->command,&info);
    VkViewport viewport{0.f,0.f,float(extent_.width),float(extent_.height),0.f,1.f};
    VkRect2D scissor{};scissor.extent=extent_;
    vkCmdSetViewport(slot->command,0,1,&viewport);
    vkCmdSetScissor(slot->command,0,1,&scissor);
    vkCmdBindPipeline(slot->command,VK_PIPELINE_BIND_POINT_GRAPHICS,pick_pipeline_);
    VkDeviceSize offset=0;
    vkCmdBindVertexBuffers(slot->command,0,1,&slot->vertices,&offset);
    vkCmdDraw(slot->command,std::uint32_t(vertices.size()),1,0,0);
    vkCmdEndRendering(slot->command);
    VkImageMemoryBarrier2 transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    transition.srcStageMask=VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    transition.srcAccessMask=VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    transition.dstStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
    transition.dstAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;
    transition.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    transition.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    transition.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.image=slot->color;
    transition.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    transition.subresourceRange.levelCount=1;
    transition.subresourceRange.layerCount=1;
    dep.imageMemoryBarrierCount=1;dep.pImageMemoryBarriers=&transition;
    vkCmdPipelineBarrier2(slot->command,&dep);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount=1;
    copy.imageOffset={std::int32_t(req.x),std::int32_t(req.y),0};
    copy.imageExtent={1,1,1};
    vkCmdCopyImageToBuffer(slot->command,slot->color,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,slot->readback,1,&copy);
    checkAsync(vkEndCommandBuffer(slot->command),
               "vkEndCommandBuffer(async pick)");
    checkAsync(vkResetFences(device_,1,&slot->fence),
               "vkResetFences(async pick)");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1;submit.pCommandBuffers=&slot->command;
    checkAsync(vkQueueSubmit(queue_,1,&submit,slot->fence),
               "vkQueueSubmit(async pick)");
    slot->active=true;
    slot->request_id=req.request_id;
    slot->scene_generation=req.scene_generation;
    slot->tokens=std::move(tokens);
    return true;
}
NorthStarRenderPickAsyncResult Renderer::poll_async_pick(
    std::uint64_t requestId) {
    NorthStarRenderPickAsyncResult result{};
    result.request_id=requestId;
    result.status=NORTHSTARCPP_PICK_UNKNOWN;
    for(auto& slot:async_pick_slots_) {
        if(!slot.active || slot.request_id!=requestId)continue;
        result.scene_generation=slot.scene_generation;
        auto const status=slot.fence?vkGetFenceStatus(device_,slot.fence):VK_SUCCESS;
        if(status==VK_NOT_READY) {
            result.status=NORTHSTARCPP_PICK_PENDING;
            return result;
        }
        checkAsync(status,"vkGetFenceStatus(async pick)");
        if(slot.scene_generation<minimum_pick_generation_ ||
           slot.width!=extent_.width || slot.height!=extent_.height) {
            result.status=NORTHSTARCPP_PICK_STALE;
            slot.active=false;
            return result;
        }
        if(slot.readback && !slot.tokens.empty()) {
            void* mapped=nullptr;
            checkAsync(vkMapMemory(device_,slot.readback_memory,0,4,0,&mapped),
                       "vkMapMemory(async ID result)");
            auto* rgba=static_cast<std::uint8_t const*>(mapped);
            auto const tag=std::uint32_t(rgba[0])|
                (std::uint32_t(rgba[1])<<8)|
                (std::uint32_t(rgba[2])<<16);
            vkUnmapMemory(device_,slot.readback_memory);
            if(tag && tag<=slot.tokens.size())
                result.entity_id=slot.tokens[tag-1u];
        }
        result.status=NORTHSTARCPP_PICK_READY;
        slot.active=false;
        return result;
    }
    return result;
}
} // namespace northstarcpp::render::vulkan
