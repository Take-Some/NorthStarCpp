#include "renderer_private.hpp"
#include <cmath>
#include <stdexcept>
namespace northstarcpp::render::vulkan {
void Renderer::destroy_ssao_resource() noexcept {
    if(!device_)return;
    if(ssao_view_)vkDestroyImageView(device_,ssao_view_,nullptr);
    if(ssao_image_)vkDestroyImage(device_,ssao_image_,nullptr);
    if(ssao_memory_)vkFreeMemory(device_,ssao_memory_,nullptr);
    ssao_view_=VK_NULL_HANDLE;ssao_image_=VK_NULL_HANDLE;
    ssao_memory_=VK_NULL_HANDLE;ssao_extent_={};
}
void Renderer::create_ssao_resource() {
    if(!ssao_enabled_)return;
    constexpr VkFormat format=VK_FORMAT_R16G16_SFLOAT;
    VkFormatProperties supported{};
    vkGetPhysicalDeviceFormatProperties(physical_,format,&supported);
    constexpr auto required=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if((supported.optimalTilingFeatures&required)!=required)
        throw std::runtime_error("Vulkan SSAO requires RG16F color+sample");
    ssao_extent_={(extent_.width+1)/2,(extent_.height+1)/2};
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType=VK_IMAGE_TYPE_2D;
    image.extent={ssao_extent_.width,ssao_extent_.height,1};
    image.mipLevels=image.arrayLayers=1;image.format=format;
    image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.samples=VK_SAMPLE_COUNT_1_BIT;
    image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if(vkCreateImage(device_,&image,nullptr,&ssao_image_)!=VK_SUCCESS)
        throw std::runtime_error("vkCreateImage(SSAO)");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_,ssao_image_,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=find_memory_type(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(vkAllocateMemory(device_,&allocation,nullptr,&ssao_memory_)!=VK_SUCCESS)
        throw std::runtime_error("vkAllocateMemory(SSAO)");
    if(vkBindImageMemory(device_,ssao_image_,ssao_memory_,0)!=VK_SUCCESS)
        throw std::runtime_error("vkBindImageMemory(SSAO)");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=ssao_image_;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=format;
    view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount=view.subresourceRange.layerCount=1;
    if(vkCreateImageView(device_,&view,nullptr,&ssao_view_)!=VK_SUCCESS)
        throw std::runtime_error("vkCreateImageView(SSAO)");
}
void Renderer::record_ssao_pass(VkCommandBuffer cmd) {
    if(!ssao_enabled_)return;
    VkRenderingAttachmentInfo target{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    target.imageView=ssao_view_;
    target.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    target.clearValue.color.float32[0]=1.f;
    target.clearValue.color.float32[1]=1.f;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent=ssao_extent_;
    rendering.layerCount=1;rendering.colorAttachmentCount=1;
    rendering.pColorAttachments=&target;
    vkCmdBeginRendering(cmd,&rendering);
    VkViewport viewport{0,0,float(ssao_extent_.width),
                        float(ssao_extent_.height),0,1};
    VkRect2D scissor{};scissor.extent=ssao_extent_;
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,ssao_pipeline_);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
        deferred_resolve_layout_,0,1,&gbuffer_set_,0,nullptr);
    // 128-byte layout matches deferred resolve; camera parameters agree.
    struct Push {
        float sky[4],mode[4],camera[4],projection[4];
        float viewport[4],ao[4],film[4],film_extra[4];
    } push{};
    static_assert(sizeof(push)==128);
    push.camera[0]=pick_camera_.camera_x;
    push.camera[1]=pick_camera_.camera_y;
    push.camera[2]=pick_camera_.camera_z;
    push.camera[3]=pick_camera_.camera_yaw;
    const float near_clip=pick_camera_.camera_near_clip>0.f?
        pick_camera_.camera_near_clip:.1f;
    const float far_clip=pick_camera_.camera_far_clip>near_clip?
        pick_camera_.camera_far_clip:250.f;
    const float fov=pick_camera_.camera_fov_y_degrees>1.f?
        pick_camera_.camera_fov_y_degrees:70.f;
    push.projection[0]=pick_camera_.camera_pitch;
    push.projection[1]=near_clip;push.projection[2]=far_clip;
    push.projection[3]=1.f/std::tan(fov*3.14159265358979323846f/360.f);
    push.viewport[0]=float(extent_.width);
    push.viewport[1]=float(extent_.height);
    push.viewport[2]=1.f/float(extent_.width);
    push.viewport[3]=1.f/float(extent_.height);
    push.ao[0]=1.f;push.ao[1]=2.f;
    push.ao[2]=2.f;push.ao[3]=.035f;
    vkCmdPushConstants(cmd,deferred_resolve_layout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
    vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRendering(cmd);
}
} // namespace northstarcpp::render::vulkan
