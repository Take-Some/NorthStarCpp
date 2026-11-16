// Dedicated Vulkan GPU object-ID pass. The stable Render ABI 5 is untouched.
// A readback is performed only after an explicit Editor pick request.
// This v1 backend handles native cube submissions; imported/animated meshes
// must use a per-mesh object-ID contract before enabling this path for them.
#include "renderer_private.hpp"
#include <northstarcpp/render_pick_api.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace northstarcpp::render::vulkan {
namespace {
void checked(VkResult code,char const* action) {
    if(code!=VK_SUCCESS)
        throw std::runtime_error(std::string(action)+": VkResult="+
            std::to_string(int(code)));
}
struct PickImage {
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
};
struct PickResources {
    VkDevice device=VK_NULL_HANDLE;
    PickImage color,depth;
    VkBuffer readback=VK_NULL_HANDLE;
    VkDeviceMemory readback_memory=VK_NULL_HANDLE;
    ~PickResources() noexcept {
        if(!device)return;
        if(readback)vkDestroyBuffer(device,readback,nullptr);
        if(readback_memory)vkFreeMemory(device,readback_memory,nullptr);
        for(auto* attachment:{&color,&depth}) {
            if(attachment->view)vkDestroyImageView(device,attachment->view,nullptr);
            if(attachment->image)vkDestroyImage(device,attachment->image,nullptr);
            if(attachment->memory)vkFreeMemory(device,attachment->memory,nullptr);
        }
    }
};
}
int Renderer::pick_cube(std::uint32_t x,std::uint32_t y) {
    if(!device_ || !pick_pipeline_ || !extent_.width || !extent_.height ||
       x>=extent_.width || y>=extent_.height)
        throw std::invalid_argument("Vulkan GPU ID picking unavailable/coordinates invalid");
    // A cube-only attachment cannot honor occlusion from mesh/skin materials.
    // Report unsupported so the editor uses its exact geometry raycast.
    if(!pick_entity_mode_ && (!scene_.empty() || !material_sources_.empty() || !skinned_sources_.empty()))
        throw std::runtime_error("Vulkan GPU ID picking needs mesh object IDs");
    if(pick_cube_ranges_.empty() && (!pick_entity_mode_ || pick_world_vertices_.empty()))return -1;
    if(pick_cube_ranges_.size() >= (1u<<24))
        throw std::runtime_error("Vulkan GPU ID index exceeds RGB24 range");
    checked(vkWaitForFences(device_,1,&completed_,VK_TRUE,10'000'000'000ull),
            "vkWaitForFences(ID pick)");
    std::vector<std::uint64_t> tokens;
    std::vector<ColoredVertex> tagged=pick_entity_mode_?
        build_pick_vertices(tokens):frame_vertices_;
    for(std::uint32_t i=0;!pick_entity_mode_ && i<pick_cube_ranges_.size();++i) {
        auto const& range=pick_cube_ranges_[i];
        if(std::size_t(range.first)+range.count>tagged.size())
            throw std::runtime_error("Invalid Vulkan GPU ID draw range");
        auto const value=i+1u;
        const float r=float(value&255u)/255.f;
        const float g=float((value>>8)&255u)/255.f;
        const float b=float((value>>16)&255u)/255.f;
        for(std::uint32_t j=0;j<range.count;++j) {
            auto& vertex=tagged[range.first+j];
            vertex.color[0]=r;
            vertex.color[1]=g;
            vertex.color[2]=b;
            vertex.color[3]=1.f;
        }
    }
    if(tagged.empty())return -1;
    // Normal frame submission has already completed. The host-visible GPU
    // vertex memory may now be temporarily uploaded with tagged vertices;
    // the next regular draw uploads its original colors again.
    void* mapped=nullptr;
    auto const vertex_bytes=static_cast<VkDeviceSize>(
        tagged.size()*sizeof(ColoredVertex));
    checked(vkMapMemory(device_,vertices_memory_,0,vertex_bytes,0,&mapped),
            "vkMapMemory(ID vertices)");
    std::memcpy(mapped,tagged.data(),static_cast<std::size_t>(vertex_bytes));
    vkUnmapMemory(device_,vertices_memory_);

    PickResources resources;
    resources.device=device_;
    const auto image=[&](VkFormat format,VkImageUsageFlags usage,
                         VkImageAspectFlags aspect,PickImage& target) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType=VK_IMAGE_TYPE_2D;
        ci.format=format;
        ci.extent={extent_.width,extent_.height,1};
        ci.mipLevels=ci.arrayLayers=1;
        ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL;
        ci.usage=usage;
        ci.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        checked(vkCreateImage(device_,&ci,nullptr,&target.image),
                "vkCreateImage(ID)");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_,target.image,&req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=req.size;
        alloc.memoryTypeIndex=find_memory_type(
            req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checked(vkAllocateMemory(device_,&alloc,nullptr,&target.memory),
                "vkAllocateMemory(ID)");
        checked(vkBindImageMemory(device_,target.image,target.memory,0),
                "vkBindImageMemory(ID)");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image=target.image;
        view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.format=format;
        view.subresourceRange.aspectMask=aspect;
        view.subresourceRange.levelCount=view.subresourceRange.layerCount=1;
        checked(vkCreateImageView(device_,&view,nullptr,&target.view),
                "vkCreateImageView(ID)");
    };
    image(VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT,resources.color);
    image(depth_format_,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT,resources.depth);
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size=4;
    buffer_info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    checked(vkCreateBuffer(device_,&buffer_info,nullptr,&resources.readback),
            "vkCreateBuffer(ID readback)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_,resources.readback,&requirements);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize=requirements.size;
    alloc.memoryTypeIndex=find_memory_type(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checked(vkAllocateMemory(device_,&alloc,nullptr,&resources.readback_memory),
            "vkAllocateMemory(ID readback)");
    checked(vkBindBufferMemory(device_,resources.readback,resources.readback_memory,0),
            "vkBindBufferMemory(ID readback)");

    checked(vkResetCommandPool(device_,command_pool_,0),"vkResetCommandPool(ID)");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checked(vkBeginCommandBuffer(command_,&begin),"vkBeginCommandBuffer(ID)");

    VkImageMemoryBarrier2 transition[2]{};
    for(unsigned i=0;i<2;++i) {
        auto& barrier=transition[i];
        barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask=VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.dstStageMask=i==0?VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT:
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask=i==0?VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT:
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=i==0?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.image=i==0?resources.color.image:resources.depth.image;
        barrier.subresourceRange.aspectMask=
            i==0?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.levelCount=1;
        barrier.subresourceRange.layerCount=1;
    }
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount=2;
    dep.pImageMemoryBarriers=transition;
    vkCmdPipelineBarrier2(command_,&dep);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=resources.color.view;
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color.float32[0]=0.f;
    color.clearValue.color.float32[1]=0.f;
    color.clearValue.color.float32[2]=0.f;
    color.clearValue.color.float32[3]=0.f;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView=resources.depth.view;
    depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil={1.f,0};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=extent_;
    info.layerCount=1;
    info.colorAttachmentCount=1;
    info.pColorAttachments=&color;
    info.pDepthAttachment=&depth;
    vkCmdBeginRendering(command_,&info);
    VkViewport viewport{0.f,0.f,float(extent_.width),float(extent_.height),0.f,1.f};
    VkRect2D scissor{};
    scissor.extent=extent_;
    vkCmdSetViewport(command_,0,1,&viewport);
    vkCmdSetScissor(command_,0,1,&scissor);
    vkCmdBindPipeline(command_,VK_PIPELINE_BIND_POINT_GRAPHICS,pick_pipeline_);
    VkDeviceSize const offset=0;
    vkCmdBindVertexBuffers(command_,0,1,&vertices_buffer_,&offset);
    vkCmdDraw(command_,static_cast<std::uint32_t>(tagged.size()),1,0,0);
    vkCmdEndRendering(command_);

    VkImageMemoryBarrier2 copy_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    copy_barrier.srcStageMask=VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    copy_barrier.srcAccessMask=VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    copy_barrier.dstStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
    copy_barrier.dstAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;
    copy_barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    copy_barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy_barrier.image=resources.color.image;
    copy_barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy_barrier.subresourceRange.levelCount=1;
    copy_barrier.subresourceRange.layerCount=1;
    dep.imageMemoryBarrierCount=1;
    dep.pImageMemoryBarriers=&copy_barrier;
    vkCmdPipelineBarrier2(command_,&dep);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount=1;
    copy.imageOffset={static_cast<std::int32_t>(x),static_cast<std::int32_t>(y),0};
    copy.imageExtent={1,1,1};
    vkCmdCopyImageToBuffer(command_,resources.color.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,resources.readback,1,&copy);
    checked(vkEndCommandBuffer(command_),"vkEndCommandBuffer(ID)");
    checked(vkResetFences(device_,1,&completed_),"vkResetFences(ID)");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1;
    submit.pCommandBuffers=&command_;
    checked(vkQueueSubmit(queue_,1,&submit,completed_),"vkQueueSubmit(ID)");
    checked(vkWaitForFences(device_,1,&completed_,VK_TRUE,10'000'000'000ull),
            "vkWaitForFences(ID readback)");
    void* data=nullptr;
    checked(vkMapMemory(device_,resources.readback_memory,0,4,0,&data),
            "vkMapMemory(ID pixel)");
    auto const* rgba=static_cast<std::uint8_t const*>(data);
    auto const tag=std::uint32_t(rgba[0])|
        (std::uint32_t(rgba[1])<<8)|(std::uint32_t(rgba[2])<<16);
    vkUnmapMemory(device_,resources.readback_memory);
    if(pick_entity_mode_) {
        last_picked_entity_=tag && tag<=tokens.size()?tokens[tag-1u]:0;
        return -1;
    }
    return tag && tag<=pick_cube_ranges_.size()?int(tag-1u):-1;
}
} // namespace northstarcpp::render::vulkan
