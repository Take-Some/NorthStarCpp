#include "renderer_private.hpp"
#include "../shared/gpu_capture_ppm.hpp"
#include "../shared/height_fog.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace northstarcpp::render::vulkan {
namespace {
void check(VkResult code,const char* action) {
    if(code!=VK_SUCCESS)
        throw std::runtime_error(std::string(action)+": VkResult="+
                                 std::to_string(static_cast<int>(code)));
}
float component(std::uint32_t color,unsigned bits) {
    return static_cast<float>((color>>bits)&255u)/255.f;
}
}
void Renderer::set_triangles(const NorthStarRenderTriangleVertex* vertices,
                             std::uint32_t count) {
    if(count%3!=0 || count>max_vertex_bytes_/sizeof(ColoredVertex))
        throw std::invalid_argument("Vulkan triangle topology/capacity invalid");
    if(count && !vertices)throw std::invalid_argument("Null Vulkan triangle data");
    if(!count)scene_.clear();
    else scene_.assign(vertices,vertices+count); // Own host data after call.
}
void Renderer::set_sky(const NorthStarSky* source) {
    if(!source) {
        sky_enabled_=false;sky_pending_=false;sky_pending_source_={};
        if(material_sky_index_<material_sources_.size())
            material_sources_.erase(material_sources_.begin()+
                                    static_cast<std::ptrdiff_t>(material_sky_index_));
        material_sky_index_=static_cast<std::size_t>(-1);
        material_textures_dirty_=true;
        sky_clear_={0.037f,0.055f,0.09f,1.f};return;
    }
    // Convert the authored YDD/YTD sky mesh and RGBA8 texture into a
    // regular unlit material. Ordinary Vulkan materials can render a dome.
    if(!source->vertices||!source->indices||!source->rgba8||
       !source->vertex_count||!source->index_count||
       source->index_count%3u||source->vertex_count>200000u||
       source->index_count>1200000u||!source->width||!source->height||
       source->width>8192u||source->height>8192u||
       std::uint64_t(source->width)*source->height*4u>64ull*1024*1024 ||
       !std::isfinite(source->tint_r)||!std::isfinite(source->tint_g)||
       !std::isfinite(source->tint_b))
        throw std::invalid_argument("Invalid Vulkan sky mesh or texture");
    MaterialSource sky{};
    sky.tint={source->tint_r,source->tint_g,source->tint_b,1.f};
    sky.metallic=0.f;sky.roughness=1.f;sky.unlit=1u;
    sky.sky_cloud_fields=gpu_sky_uniforms_.flags[0]>.5f;
    sky.widths[0]=source->width;sky.heights[0]=source->height;
    const auto bytes=std::size_t(source->width)*source->height*4u;
    sky.textures[0].assign(source->rgba8,source->rgba8+bytes);
    sky.vertices.reserve(source->index_count);
    for(std::uint32_t i=0;i<source->index_count;++i) {
        const auto index=source->indices[i];
        if(index>=source->vertex_count)
            throw std::invalid_argument("Sky index out of bounds");
        const auto& v=source->vertices[index];
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||
           !std::isfinite(v.z)||!std::isfinite(v.u)||!std::isfinite(v.v))
            throw std::invalid_argument("Sky vertex contains NaN/Inf");
        sky.vertices.push_back({v.x,v.y,v.z,0.f,1.f,0.f,v.u,v.v});
    }
    // set_sky() is a first-class render API, independent of calls to
    // set_material_batches(). Keep the authored batch list untouched and
    // directly update the resident sky slot, including sky-only scenes.
    if(material_sky_index_<material_sources_.size()) {
        material_sources_[material_sky_index_]=std::move(sky);
    }else {
        material_sky_index_=material_sources_.size();
        material_sources_.push_back(std::move(sky));
    }
    sky_enabled_=true;sky_pending_=false;
    material_textures_dirty_=true;
}
void Renderer::set_gpu_sky_uniforms(const NorthStarGpuSkyUniforms* source) {
    if(!source) {gpu_sky_uniforms_={};height_fog_density_=0.f;return;}
    auto const* values=reinterpret_cast<float const*>(source);
    for(std::size_t i=0;i<sizeof(*source)/sizeof(float);++i)
        if(!std::isfinite(values[i]))
            throw std::invalid_argument("Non-finite Vulkan GPU Sky uniforms");
    if(source->atmosphere[0]<0.f||source->atmosphere[0]>1.f||
       source->atmosphere[1]<0.f||source->atmosphere[1]>1.f||
       source->cloud[0]<0.f||source->cloud[0]>1.f||
       source->cloud[2]<0.f||source->cloud[2]>=1.f)
        throw std::invalid_argument("Vulkan Sky uniform value outside range");
    if(source->sun_direction[3]<0.f||source->sun_direction[3]>.25f)
        throw std::invalid_argument("Vulkan sky fog density out of range");
    gpu_sky_uniforms_=*source; // no GPU wait or texture updates
    height_fog_density_=source->flags[0]>.5f?
        source->sun_direction[3]:0.f;
}
void Renderer::request_capture() {
    if(capture_requested_ || capture_inflight_)
        throw std::runtime_error("Vulkan GPU capture already pending");
    capture_rgba_.clear();
    capture_width_=capture_height_=0;
    capture_requested_=true;
}
void Renderer::complete_capture() {
    if(!capture_inflight_)return;
    auto const status=vkGetFenceStatus(device_,completed_);
    if(status==VK_NOT_READY)return; // Poll: never submit or wait on this path.
    check(status,"vkGetFenceStatus(async capture)");
    auto const bytes=std::size_t(capture_width_)*capture_height_*4u;
    if(!bytes||bytes>readback_capacity_)
        throw std::runtime_error("Vulkan async GPU capture staging overflow");
    void* mapped=nullptr;
    check(vkMapMemory(device_,readback_memory_,0,bytes,0,&mapped),
          "vkMapMemory(async capture)");
    auto const* source=static_cast<const std::uint8_t*>(mapped);
    capture_rgba_.resize(bytes);
    // Public capture ABI expects RGBA8; Vulkan swapchain is usually BGRA8.
    const bool bgra=format_==VK_FORMAT_B8G8R8A8_UNORM||
                    format_==VK_FORMAT_B8G8R8A8_SRGB;
    for(std::size_t i=0;i<bytes;i+=4) {
        capture_rgba_[i]=source[i+(bgra?2:0)];
        capture_rgba_[i+1]=source[i+1];
        capture_rgba_[i+2]=source[i+(bgra?0:2)];
        capture_rgba_[i+3]=source[i+3];
    }
    vkUnmapMemory(device_,readback_memory_);
    capture_inflight_=false;
}
bool Renderer::read_capture(std::uint8_t* pixels,std::uint32_t capacity,
                             std::uint32_t* width,std::uint32_t* height) {
    if(!width||!height)
        throw std::invalid_argument("Vulkan capture width/height pointers missing");
    complete_capture();
    if(capture_rgba_.empty())return false;
    *width=capture_width_;*height=capture_height_;
    if(!pixels && !capacity)return true;
    if(!pixels||capacity<capture_rgba_.size())
        throw std::invalid_argument("Vulkan capture output buffer too small");
    std::memcpy(pixels,capture_rgba_.data(),capture_rgba_.size());
    return true;
}
bool Renderer::device_info(NorthStarRenderDeviceInfo& result) const noexcept {
    if(!physical_)return false;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_,&props);
    result={};
    result.abi_version=NORTHSTARCPP_RENDER_DEVICE_ABI;
    result.struct_size=sizeof(result);
    result.backend=NORTHSTARCPP_RENDER_DEVICE_VULKAN;
    result.software=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU?1u:0u;
    result.vendor_id=props.vendorID;result.device_id=props.deviceID;
    result.api_version=props.apiVersion;
    std::strncpy(result.adapter_name_utf8,props.deviceName,
                 sizeof(result.adapter_name_utf8)-1);
    return true;
}
bool Renderer::timing(NorthStarRenderTimingStats& stats) const noexcept {
    stats=last_timing_;
    return stats.valid!=0;
}
void Renderer::collect_vertices(const NorthStarRenderFrame& frame) {
    if(frame.count && !frame.cubes)
        throw std::invalid_argument("Vulkan frame cubes pointer is null");
    if(!world_surface_ids_.empty() &&
       world_surface_ids_.size()!=frame.count)
        throw std::invalid_argument("Vulkan cube surface IDs do not match frame count");
    if(std::any_of(world_surface_ids_.begin(),world_surface_ids_.end(),
        [](std::uint32_t id){return id>0;})) {
        if(!std::all_of(world_texture_resident_.begin(),
           world_texture_resident_.end(),[](bool resident){return resident;}))
            throw std::runtime_error(
                "Vulkan world surface was requested before all 9 BCn textures became resident");
    }
    frame_vertices_.clear();
    pick_cube_ranges_.assign(frame.count,{});
    frame_vertices_.reserve(scene_.size()+static_cast<std::size_t>(frame.count)*36);
    const float near_clip=frame.camera_near_clip>0.f?frame.camera_near_clip:.1f;
    const float far_clip=frame.camera_far_clip>near_clip?frame.camera_far_clip:250.f;
    const float fov=frame.camera_fov_y_degrees>1.f?frame.camera_fov_y_degrees:70.f;
    const float scale=1.f/std::tan(fov*3.14159265f/(360.f));
    const float aspect=static_cast<float>(extent_.width)/extent_.height;
    const float cy=std::cos(frame.camera_yaw),sy=std::sin(frame.camera_yaw);
    const float cp=std::cos(frame.camera_pitch),sp=std::sin(frame.camera_pitch);
    const auto project=[&](float x,float y,float z,std::uint32_t color,
                           ColoredVertex& out)->bool {
        const float dx=x-frame.camera_x;
        const float dy=y-frame.camera_y;
        const float dz=z-frame.camera_z;
        const float horizontal=dx*cy-dz*sy;
        const float forward=dx*sy+dz*cy;
        const float vertical=dy*cp-forward*sp;
        const float distance=dy*sp+forward*cp;
        // Let Vulkan clip homogeneous triangles at the near plane. Rejecting
        // a single vertex removed whole floor triangles during camera motion.
        out.clip[0]=horizontal*scale/aspect;
        out.clip[1]=-vertical*scale; // Vulkan framebuffer Y convention
        out.clip[2]=(far_clip*distance-far_clip*near_clip)/(far_clip-near_clip);
        out.clip[3]=distance;
        out.color[0]=component(color,16);
        out.color[1]=component(color,8);
        out.color[2]=component(color,0);
        // Opaque vertex colors retain full alpha. Atmospheric extinction
        // is evaluated at each fragment using interpolated world XYZ.
        out.color[3]=1.f;
        out.world[0]=x;out.world[1]=y;out.world[2]=z;
        out.surface=0.f;
        return true;
    };
    for(std::size_t i=0;i+2<scene_.size();i+=3) {
        ColoredVertex vertices[3];
        bool visible=true;
        for(unsigned j=0;j<3;++j) {
            const auto& v=scene_[i+j];
            visible=project(v.x,v.y,v.z,v.rgb,vertices[j]) && visible;
        }
        if(visible)frame_vertices_.insert(frame_vertices_.end(),vertices,vertices+3);
    }
    static constexpr float corners[8][3]{
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    static constexpr unsigned triangles[36]{
        0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1,
        1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
    static constexpr float cube_face_shade[6]{
        .74f,1.f,.60f,.82f,.95f,.68f};
    for(std::uint32_t i=0;i<frame.count;++i) {
        const auto& cube=frame.cubes[i];
        if(!std::isfinite(cube.size)||cube.size<=0.f)continue;
        auto const cube_first=static_cast<std::uint32_t>(frame_vertices_.size());
        const bool native_surface=!world_surface_ids_.empty() && world_surface_ids_[i]!=0;
        for(unsigned k=0;k<36;k+=3) {
            ColoredVertex vertices[3];
            bool visible=true;
            const auto& one=corners[triangles[k]];
            const auto& two=corners[triangles[k+1]];
            const auto& three=corners[triangles[k+2]];
            // Same D3D12 authoring convention: cube base at y=cube.y,
            // row-major rotation and six face luminance coefficients.
            const unsigned face=one[1]==two[1]&&one[1]==three[1]?
                (one[1]>0.f?1u:0u):
                one[2]==two[2]&&one[2]==three[2]?
                (one[2]>0.f?4u:2u):(one[0]>0.f?3u:5u);
            const auto color=frame.selected==static_cast<int>(i)?
                0xffd474u:cube.rgb;
            for(unsigned j=0;j<3;++j) {
                const auto& p=corners[triangles[k+j]];
                const float scale_factor=cube.size*.5f;
                const float a=p[0]*scale_factor,b=p[1]*scale_factor,c=p[2]*scale_factor;
                const float x=(native_surface ? cube.x+cube.rotation[0]*a+cube.rotation[3]*b+cube.rotation[6]*c : cube.x+cube.rotation[0]*a+cube.rotation[1]*b+cube.rotation[2]*c);
                const float y=(native_surface ? cube.y+cube.rotation[1]*a+cube.rotation[4]*b+cube.rotation[7]*c : cube.y+cube.size*.5f+cube.rotation[3]*a+cube.rotation[4]*b+cube.rotation[5]*c);
                const float z=(native_surface ? cube.z+cube.rotation[2]*a+cube.rotation[5]*b+cube.rotation[8]*c : cube.z+cube.rotation[6]*a+cube.rotation[7]*b+cube.rotation[8]*c);
                visible=project(x,y,z,color,vertices[j])&&visible;
                if(!native_surface) for(int channel=0;channel<3;++channel)
                    vertices[j].color[channel]*=cube_face_shade[face];
                vertices[j].surface=world_surface_ids_.empty()?0.f:
                    static_cast<float>(world_surface_ids_[i]);
            }
            if(visible)frame_vertices_.insert(frame_vertices_.end(),vertices,vertices+3);
        }
        pick_cube_ranges_[i]={cube_first,
            static_cast<std::uint32_t>(frame_vertices_.size())-cube_first};
    }
    if(frame_vertices_.size()*sizeof(ColoredVertex)>max_vertex_bytes_)
        throw std::runtime_error("Vulkan vertex staging capacity exceeded");
    render_vertices_=static_cast<std::uint32_t>(frame_vertices_.size());
    if(render_vertices_) {
        void* mapped=nullptr;
        check(vkMapMemory(device_,vertices_memory_,0,
            frame_vertices_.size()*sizeof(ColoredVertex),0,&mapped),"vkMapMemory(vertices)");
        std::memcpy(mapped,frame_vertices_.data(),
            frame_vertices_.size()*sizeof(ColoredVertex));
        vkUnmapMemory(device_,vertices_memory_);
    }
}
void Renderer::record_opaque(VkCommandBuffer cmd,std::uint32_t image,
                              const NorthStarRenderFrame& frame) {
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    const bool world_hdr=hdr_enabled_&&std::any_of(
        world_surface_ids_.begin(),world_surface_ids_.end(),
        [](std::uint32_t id){return id!=0;});
    attachment.imageView=world_hdr?hdr_scene_view_:image_views_.at(image);
    attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    for(unsigned i=0;i<4;++i)attachment.clearValue.color.float32[i]=sky_clear_[i];
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent=extent_;
    rendering.layerCount=1;
    rendering.colorAttachmentCount=1;
    rendering.pColorAttachments=&attachment;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView=depth_view_;
    depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil={1.f,0};
    rendering.pDepthAttachment=&depth;
    vkCmdBeginRendering(cmd,&rendering);
    if(render_vertices_) {
        VkViewport viewport{0.f,0.f,static_cast<float>(extent_.width),
                            static_cast<float>(extent_.height),0.f,1.f};
        VkRect2D scissor{};scissor.extent=extent_;
        vkCmdSetViewport(cmd,0,1,&viewport);
        vkCmdSetScissor(cmd,0,1,&scissor);
        const bool textured_world=std::any_of(
            world_surface_ids_.begin(),world_surface_ids_.end(),
            [](std::uint32_t id){return id>0;});
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                          textured_world?
                           (world_hdr?world_pipeline_hdr_:world_pipeline_):
                           (world_hdr?pipeline_hdr_:pipeline_));
        // Forward fog push: camera origin and density, sky-derived haze tint.
        // Both primitive and BCn world pipelines use this 32-byte layout.
        struct ForwardAtmosphere {float camera[4];float haze[4];} fog{};
        static_assert(sizeof(fog)==32);
        fog.camera[0]=frame.camera_x;
        fog.camera[1]=frame.camera_y;
        fog.camera[2]=frame.camera_z;
        fog.camera[3]=height_fog_density_;
        const float daylight=std::clamp(gpu_sky_uniforms_.atmosphere[0],0.f,1.f);
        const float night_haze[3]={.055f,.075f,.11f};
        const float day_haze[3]={.23f,.315f,.395f};
        for(unsigned c=0;c<3;++c)
            fog.haze[c]=night_haze[c]+daylight*(day_haze[c]-night_haze[c]);
        vkCmdPushConstants(cmd,textured_world?world_pipeline_layout_:pipeline_layout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(fog),&fog);
        if(textured_world)
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                world_pipeline_layout_,0,1,&world_set_,0,nullptr);
        const VkDeviceSize offset=0;
        vkCmdBindVertexBuffers(cmd,0,1,&vertices_buffer_,&offset);
        vkCmdDraw(cmd,render_vertices_,1,0,0);
    }
    if(!material_draws_.empty()||!skin_draws_.empty()) {
        VkViewport viewport{0.f,0.f,static_cast<float>(extent_.width),
                            static_cast<float>(extent_.height),0.f,1.f};
        VkRect2D scissor{};scissor.extent=extent_;
        vkCmdSetViewport(cmd,0,1,&viewport);
        vkCmdSetScissor(cmd,0,1,&scissor);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,material_pipeline_);
        const VkDeviceSize offset=0;
        vkCmdBindVertexBuffers(cmd,0,1,&material_vertices_buffer_,&offset);
        // Draw opaque/masked before translucent irrespective of host order.
        for(unsigned pass=0;pass<2;++pass) {
            if(pass==0 && !skin_draws_.empty()) {
                vkCmdBindVertexBuffers(cmd,0,1,&skin_output_.handle,&offset);
                for(const auto& skin:skin_draws_) {
                    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      skin.pipeline);
                    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                        material_pipeline_layout_,0,1,&skin.textures,0,nullptr);
                    vkCmdDraw(cmd,skin.count,1,skin.first,0);
                }
                vkCmdBindVertexBuffers(cmd,0,1,&material_vertices_buffer_,&offset);
            }
            for(const auto& batch:material_draws_) {
                if((batch.blend_mode==2)!=(pass==1))continue;
                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,batch.pipeline);
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                    material_pipeline_layout_,0,1,&batch.textures,0,nullptr);
                vkCmdDraw(cmd,batch.count,1,batch.first,0);
            }
        }
    }
    vkCmdEndRendering(cmd);
}
void Renderer::record_readback(VkCommandBuffer cmd,std::uint32_t image,
                               const NorthStarRenderFrame& frame) {
    if(!frame.verify_pixels && !capture_requested_)return;
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount=1;
    copy.imageExtent={extent_.width,extent_.height,1};
    vkCmdCopyImageToBuffer(cmd,images_.at(image),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer_,1,&copy);
}
void Renderer::validate_readback(const NorthStarRenderFrame& frame) {
    if(!frame.verify_pixels)return;
    if(frame.verify_pixels!=1 && frame.verify_pixels!=2 &&
       frame.verify_pixels!=3 && frame.verify_pixels!=4 &&
       frame.verify_pixels!=5 && frame.verify_pixels!=6 &&
       frame.verify_pixels!=7 && frame.verify_pixels!=8 &&
       frame.verify_pixels!=9 &&
       frame.verify_pixels!=10 && frame.verify_pixels!=11)
        throw std::runtime_error("Vulkan requested readback mode is unsupported");
    void* data=nullptr;
    const auto bytes=static_cast<VkDeviceSize>(extent_.width)*extent_.height*4;
    if(bytes>readback_capacity_)throw std::runtime_error("Vulkan readback buffer too small");
    check(vkMapMemory(device_,readback_memory_,0,bytes,0,&data),
          "vkMapMemory(readback)");
    auto const* pixels=static_cast<const std::uint8_t*>(data);
    northstarcpp::render::diagnostics::capture_ppm_if_requested(
        pixels,extent_.width,extent_.height,extent_.width*4u,true,
        frame.verify_pixels);
    bool nonblack=false,bright=false;
    for(std::size_t i=0;i<bytes;i+=4) {
        const auto b=pixels[i],g=pixels[i+1],r=pixels[i+2];
        if(b||g||r)nonblack=true;
        if(b>96||g>96||r>96)bright=true;
    }
    if(frame.verify_pixels==8) {
        const size_t center=(static_cast<size_t>(extent_.height/2)*
                             extent_.width+extent_.width/2)*4;
        // Vulkan swapchain is BGRA8. UI texture itself is RGBA8.
        const bool ok=!ui_cpu_rgba_.empty() && center+3<bytes &&
                      pixels[center+2]>200 && pixels[center+1]<80 &&
                      pixels[center]<80;
        vkUnmapMemory(device_,readback_memory_);
        if(!ok)throw std::runtime_error(
            "Vulkan GPU engine.ui compositing readback mismatch");
        return;
    }
    if(frame.verify_pixels==9) {
        // Camera-near clipping regression: a GREEN triangle intersects
        // the near plane (or the player stands above a 50m ground slab).
        // Hardware clipping must retain its visible centre; the legacy
        // per-vertex reject incorrectly removed the entire primitive.
        auto const center=(static_cast<std::size_t>(extent_.height/2)*
                           extent_.width+extent_.width/2)*4;
        bool const floor_visible=center+3<bytes &&
            pixels[center+1]>170 &&
            pixels[center+2]<100 && pixels[center]<100;
        std::uint64_t green_pixels=0;
        int x_min=static_cast<int>(extent_.width),x_max=-1;
        int y_min=static_cast<int>(extent_.height),y_max=-1;
        for(std::uint32_t y=0;y<extent_.height;++y) {
            for(std::uint32_t x=0;x<extent_.width;++x) {
                const auto p=(std::size_t(y)*extent_.width+x)*4;
                if(p+3<bytes && pixels[p+1]>150 &&
                   pixels[p+1]>pixels[p]+40 &&
                   pixels[p+1]>pixels[p+2]+40) {
                    ++green_pixels;
                    x_min=std::min(x_min,static_cast<int>(x));
                    x_max=std::max(x_max,static_cast<int>(x));
                    y_min=std::min(y_min,static_cast<int>(y));
                    y_max=std::max(y_max,static_cast<int>(y));
                }
            }
        }
        const auto diagnostic="green="+std::to_string(green_pixels)+
            " bounds="+std::to_string(x_min)+","+
            std::to_string(y_min)+".."+std::to_string(x_max)+","+
            std::to_string(y_max)+" BGRA="+
            std::to_string(pixels[center])+","+
            std::to_string(pixels[center+1])+","+
            std::to_string(pixels[center+2])+
            " pitch="+std::to_string(frame.camera_pitch)+
            " count="+std::to_string(frame.count)+
            " vertices="+std::to_string(render_vertices_);
        vkUnmapMemory(device_,readback_memory_);
        if(!floor_visible)
            throw std::runtime_error(
                "Vulkan near-plane clip lost visible floor triangle: "+diagnostic);
        return;
    }
    if(frame.verify_pixels==4) {
        // Hardware depth acceptance: the near GREEN triangle must occlude
        // the far RED one, irrespective of geometry submission order.
        // Vulkan swapchain formats are BGRA8, so green is component 1.
        auto const center=(static_cast<std::size_t>(extent_.height/2)*
                           extent_.width+extent_.width/2)*4;
        bool const near_visible=center+3<bytes &&
            pixels[center+1]>170 &&
            pixels[center+2]<100 &&
            pixels[center+0]<100;
        vkUnmapMemory(device_,readback_memory_);
        if(!near_visible)
            throw std::runtime_error("Vulkan depth occlusion GPU readback mismatch");
        return;
    }
    if(frame.verify_pixels==7) {
        // Native BC1 world surfaces: each authored surface group has a
        // distinct GPU-sampled color in the central visible cube pixel.
        auto const center=(static_cast<std::size_t>(extent_.height/2)*
                           extent_.width+extent_.width/2)*4;
        const auto id=world_surface_ids_.empty()?0u:world_surface_ids_[0];
        bool valid=id>=1&&id<=3&&center+3<bytes;
        if(valid) {
            const int expected=id==1?2:id==2?1:0; // BGRA swapchain.
            valid=pixels[center+expected]>85;
            for(int channel=0;channel<3;++channel)
                if(channel!=expected&&pixels[center+channel]>50)
                    valid=false;
        }
        vkUnmapMemory(device_,readback_memory_);
        if(!valid)
            throw std::runtime_error(
                "Vulkan BC1/BC5 world surface GPU color/selector mismatch");
        return;
    }
    if(frame.verify_pixels==5) {
        // PBR light acceptance: a lit white quad must brighten the central
        // pixel above ambient under a strong nearby physical point light.
        auto const center=(static_cast<std::size_t>(extent_.height/2)*
                           extent_.width+extent_.width/2)*4;
        auto const lit=!material_draws_.empty()&&!dynamic_lights_.empty() &&
            std::any_of(material_sources_.begin(),material_sources_.end(),
                [](auto const& source){return source.unlit==0;}) &&
            center+3<bytes &&
            (pixels[center]>110||pixels[center+1]>110||pixels[center+2]>110);
        vkUnmapMemory(device_,readback_memory_);
        if(!lit)
            throw std::runtime_error("Vulkan point/spot GPU lighting readback failed");
        return;
    }
    if(frame.verify_pixels==2 || frame.verify_pixels==3 ||
       frame.verify_pixels==6) {
        std::uint64_t red=0,green=0;
        for(std::size_t index=0;index+3<bytes;index+=4) {
            auto const* pixel=pixels+index;
            if(pixel[2]>130 && pixel[1]<90 && pixel[0]<90)++red;
            if(pixel[1]>130 && pixel[2]<90 && pixel[0]<90)++green;
        }
        vkUnmapMemory(device_,readback_memory_);
        if(frame.verify_pixels==2) {
            if((material_draws_.empty()&&skin_draws_.empty())||
               red<100||green<100)
                throw std::runtime_error(
                    "Vulkan textured GPU readback lacks two sampled UV colors");
        }else if(frame.verify_pixels==6) {
            if(skin_draws_.empty()||red>25||green>25)
                throw std::runtime_error(
                    "Vulkan GPU bone matrix translation was not applied");
        }else if(material_draws_.empty() || green<100 || red>25)
            throw std::runtime_error(
                "Vulkan generated Material Graph GPU color mismatch");
        return;
    }
    vkUnmapMemory(device_,readback_memory_);
    if(!nonblack || (render_vertices_ && !bright))
        throw std::runtime_error("Vulkan pixel readback is empty or missing rendered geometry");
}
void Renderer::draw(const NorthStarRenderFrame& frame) {
    if(frame.verify_pixels>3 && frame.verify_pixels!=9 &&
       frame.verify_pixels!=4 &&
       frame.verify_pixels!=5 && frame.verify_pixels!=6 &&
       frame.verify_pixels!=7 && frame.verify_pixels!=8 &&
       frame.verify_pixels!=10 && frame.verify_pixels!=11)
        throw std::runtime_error("Vulkan unsupported GPU verification mode");
    if(!device_||!swapchain_)throw std::runtime_error("Vulkan renderer not initialized");
    check(diagnostics_.time_wait(VulkanWaitKind::frame_fence, [&] { return vkWaitForFences(device_,1,&completed_,VK_TRUE,10'000'000'000ull); }),
          "vkWaitForFences");
    complete_capture();
    if(timing_pending_) {
        collect_completed_gpu_timing();
        timing_pending_=false;
    }
    collect_hdr_exposure();
    pump_shader_reload();
    RECT client{};
    if(!GetClientRect(hwnd_,&client))throw std::runtime_error("Vulkan GetClientRect failed");
    const auto width=static_cast<std::uint32_t>(std::max<LONG>(1,client.right-client.left));
    const auto height=static_cast<std::uint32_t>(std::max<LONG>(1,client.bottom-client.top));
    if(extent_.width!=width||extent_.height!=height) {
        check(diagnostics_.time_wait(VulkanWaitKind::device_idle, [&] { return vkDeviceWaitIdle(device_); }),"vkDeviceWaitIdle(resize)");
        destroy_swapchain();create_swapchain();
        temporal_tracker_.invalidate();
    }
    northstarcpp::render::TemporalCamera camera{};
    camera.position={frame.camera_x,frame.camera_y,frame.camera_z};
    camera.yaw=frame.camera_yaw;camera.pitch=frame.camera_pitch;
    camera.roll=frame.camera_roll;
    camera.fov_y_degrees=frame.camera_fov_y_degrees>1.f?
        frame.camera_fov_y_degrees:70.f;
    camera.near_clip=frame.camera_near_clip>0.f?
        frame.camera_near_clip:.1f;
    camera.far_clip=frame.camera_far_clip>camera.near_clip?
        frame.camera_far_clip:250.f;
    camera.width=extent_.width;camera.height=extent_.height;
    camera.first_person=frame.first_person||frame.camera_override;
    temporal_frame_=temporal_tracker_.begin(camera);
    if(!temporal_frame_.history_valid)temporal_instances_.invalidate();
    // The previous compute and vertex reads are complete. Grow before
    // acquiring a swapchain image so allocation failure leaves it untouched.
    ensure_skin_buffers();
    std::uint32_t image=0;
    auto acquired=diagnostics_.time_wait(VulkanWaitKind::image_acquire, [&] { return vkAcquireNextImageKHR(device_,swapchain_,UINT64_MAX,available_,
                                        VK_NULL_HANDLE,&image); });
    if(acquired==VK_ERROR_OUT_OF_DATE_KHR) {
        check(diagnostics_.time_wait(VulkanWaitKind::device_idle, [&] { return vkDeviceWaitIdle(device_); }),"vkDeviceWaitIdle(out-of-date)");
        destroy_swapchain();create_swapchain();
        acquired=vkAcquireNextImageKHR(device_,swapchain_,UINT64_MAX,available_,
                                      VK_NULL_HANDLE,&image);
    }
    if(acquired!=VK_SUCCESS&&acquired!=VK_SUBOPTIMAL_KHR)
        check(acquired,"vkAcquireNextImageKHR");
    pick_camera_=frame; // Copy camera scalars for the ID pass; no pointers dereferenced.
    collect_vertices(frame);
    upload_light_buffer();
    if(sky_uniform_memory_) {
        gpu_sky_uniforms_.camera_world[0]=frame.camera_x;
        gpu_sky_uniforms_.camera_world[1]=frame.camera_y;
        gpu_sky_uniforms_.camera_world[2]=frame.camera_z;
        gpu_sky_uniforms_.camera_world[3]=1.f;
        void* mapped=nullptr;
        check(vkMapMemory(device_,sky_uniform_memory_,0,
            sizeof(gpu_sky_uniforms_),0,&mapped),"vkMapMemory(Sky UBO)");
        std::memcpy(mapped,&gpu_sky_uniforms_,sizeof(gpu_sky_uniforms_));
        vkUnmapMemory(device_,sky_uniform_memory_);
    }

    if(deferred_enabled_)update_deferred_tiles(frame);
    upload_material_textures();
    collect_material_vertices(frame);
    upload_skin_geometry();
    if(std::getenv("NORTHSTARCPP_TRACE_VULKAN_MATERIALS")) {
        static unsigned trace_frame=0;
        if(trace_frame++<12 || trace_frame%45==0)
            std::fprintf(stderr,
                "[QA.VK] deferred=%u scene_tri=%u material_sources=%zu"
                " material_draws=%zu skin_sources=%zu skin_draws=%zu"
                " skin_vertices=%llu material_sets=%zu\\n",
                unsigned(deferred_enabled_),render_vertices_,
                material_sources_.size(),material_draws_.size(),
                skinned_sources_.size(),skin_draws_.size(),
                static_cast<unsigned long long>(skin_vertex_count_),
                material_sets_.size());
    }
    ensure_ui_resources();
    check(vkResetCommandPool(device_,command_pool_,0),"vkResetCommandPool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command_,&begin),"vkBeginCommandBuffer");
    if(timing_queries_) {
        vkCmdResetQueryPool(command_,timing_queries_,0,5);
        vkCmdWriteTimestamp2(command_,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            timing_queries_,0);
    }
    record_skin_compute(command_,frame);
    // World BCn surfaces retain the proven forward path until their
    // texture/material attributes have dedicated MRT emitters.
    bool const textured_world=std::any_of(world_surface_ids_.begin(),
        world_surface_ids_.end(),[](std::uint32_t id){return id!=0;});
    bool const deferred=deferred_enabled_&&!textured_world;
    bool const forward_hdr=hdr_enabled_&&textured_world;
    auto const& execution=deferred?deferred_graph_plan_:
        (forward_hdr?hdr_forward_graph_plan_:graph_plan_);
    VkImageSubresourceRange subresource{};
    subresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.levelCount=1;subresource.layerCount=1;
    std::array<northstarcpp::render::VulkanGraphImage,16> bindings{};
    const std::size_t base_count=6u+(ssao_enabled_?1u:0u)+
                                  (csm_enabled_?1u:0u);
    const std::size_t binding_count=deferred?
        base_count+(hdr_enabled_?1u+bloom_levels_+
                    (auto_exposure_enabled_?1u:0u)+
                    (velocity_enabled_?1u:0u):0u):
        (forward_hdr?3u+bloom_levels_+
                    (auto_exposure_enabled_?1u:0u)+
                    (velocity_enabled_?1u:0u):2u);
    bindings[0]={images_[image],subresource,VK_QUEUE_FAMILY_IGNORED,
                 VK_QUEUE_FAMILY_IGNORED,!swapchain_seen_.at(image)};
    auto depth_range=subresource;
    depth_range.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    bindings[1]={depth_image_,depth_range,VK_QUEUE_FAMILY_IGNORED,
                 VK_QUEUE_FAMILY_IGNORED};
    if(deferred)
        for(unsigned i=0;i<(gbuffer_v2_?4u:3u);++i)
            bindings[2+i]={gbuffer_images_[i],subresource,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
    if(deferred&&ssao_enabled_)
        bindings[6]={ssao_image_,subresource,
            VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
    if(deferred&&csm_enabled_) {
        auto shadow_range=depth_range;
        shadow_range.layerCount=csm_cascades_;
        bindings[6u+(ssao_enabled_?1u:0u)]={csm_image_,shadow_range,
            VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
    }
    if(deferred&&hdr_enabled_) {
        bindings[base_count]={hdr_scene_image_,subresource,
            VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
        // Resource order: scene, optional luminance meter, then bloom levels.
        const unsigned first_bloom=base_count+1u+
                                   (auto_exposure_enabled_?1u:0u);
        if(auto_exposure_enabled_)
            bindings[base_count+1u]={luminance_image_,subresource,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
        for(unsigned i=0;i<bloom_levels_;++i)
            bindings[first_bloom+i]={bloom_images_[i],subresource,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
        if(velocity_enabled_)
            bindings[first_bloom+bloom_levels_]={velocity_image_,subresource,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
    }
    if(forward_hdr) {
        bindings[2]={hdr_scene_image_,subresource,
            VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
        for(unsigned i=0;i<bloom_levels_;++i)
            bindings[3+i]={bloom_images_[i],subresource,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
        if(auto_exposure_enabled_)
            bindings[3+bloom_levels_]={luminance_image_,subresource,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
        if(velocity_enabled_)
            bindings[3+bloom_levels_+(auto_exposure_enabled_?1u:0u)]={
                velocity_image_,subresource,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED};
    }
    // Contract diagnostics are attached to the renderer, not hidden behind
    // the generic RenderGraph executor exception. The planner's resource IDs
    // and the binding table must describe exactly the same image set.
    if(command_==VK_NULL_HANDLE || !vkCmdPipelineBarrier2 ||
       binding_count!=execution.lifetimes.size())
        throw std::runtime_error(
            "Vulkan graph command/bindings mismatch: command="+
            std::to_string(command_!=VK_NULL_HANDLE)+
            " sync2="+std::to_string(vkCmdPipelineBarrier2!=nullptr)+
            " bindings="+std::to_string(binding_count)+
            " lifetimes="+std::to_string(execution.lifetimes.size())+
            " deferred="+std::to_string(deferred)+
            " gbuffer_v2="+std::to_string(gbuffer_v2_));
    northstarcpp::render::record_vulkan_graph(
        command_,vkCmdPipelineBarrier2,execution,bindings.data(),binding_count,
        [&](northstarcpp::render::PassId pass,VkCommandBuffer cmd) {
            if(deferred) {
                auto adjusted=pass;
                if(ssao_enabled_) {
                    if(adjusted==1u){record_ssao_pass(cmd);return;}
                    if(adjusted>1u)--adjusted;
                }
                if(csm_enabled_) {
                    if(adjusted==1u){record_csm_pass(cmd);return;}
                    if(adjusted>1u)--adjusted;
                }
                if(hdr_enabled_&&adjusted>=3u) {
                    auto hdr_index=adjusted-3u;
                    if(velocity_enabled_) {
                        if(hdr_index==0u){record_hdr_velocity(cmd);return;}
                        --hdr_index;
                    }
                    if(auto_exposure_enabled_) {
                        if(hdr_index==0u) {record_hdr_luminance(cmd);return;}
                        if(hdr_index==1u) {record_hdr_luminance_copy(cmd);return;}
                        hdr_index-=2u;
                    }
                    if(hdr_index<active_bloom_levels_) {
                        record_hdr_bloom(cmd,hdr_index);
                    } else if(hdr_index==active_bloom_levels_) {
                        record_hdr_compositor(cmd,image);
                    } else if(hdr_index==active_bloom_levels_+1u) {
                        record_ui_overlay(cmd,image);
                        if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,3);
                    } else if(hdr_index==active_bloom_levels_+2u) {
                        record_readback(cmd,image,frame);
                    } else if(hdr_index==active_bloom_levels_+3u) {
                        if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,4);
                    } else throw std::logic_error("Unknown Vulkan HDR pass");
                    return;
                }
                switch(adjusted) {
                case 0:
                    record_gbuffer(cmd,frame);
                    if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,1);
                    break;
                case 1:
                    record_deferred_resolve(cmd,image);
                    break;
                case 2:
                    record_deferred_forward(cmd,image);
                    if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,2);
                    break;
                case 3:
                    record_ui_overlay(cmd,image);
                    if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,3);
                    break;
                case 4:
                    record_readback(cmd,image,frame);
                    break;
                case 5:
                    if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,4);
                    break;
                default:throw std::logic_error("Unknown Vulkan deferred pass");
                }
            } else {
                if(forward_hdr) {
                    if(pass==0u) {
                        record_opaque(cmd,image,frame);
                        if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,1);
                        return;
                    }
                    auto post=pass-1u;
                    if(velocity_enabled_) {
                        if(post==0u){record_hdr_velocity(cmd);return;}
                        --post;
                    }
                    if(auto_exposure_enabled_) {
                        if(post==0u){record_hdr_luminance(cmd);return;}
                        if(post==1u){record_hdr_luminance_copy(cmd);return;}
                        post-=2u;
                    }
                    if(post<active_bloom_levels_) {
                        record_hdr_bloom(cmd,post);
                    } else if(post==active_bloom_levels_) {
                        record_hdr_compositor(cmd,image);
                        if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,2);
                    } else if(post==active_bloom_levels_+1u) {
                        record_ui_overlay(cmd,image);
                        if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,3);
                    } else if(post==active_bloom_levels_+2u) {
                        record_readback(cmd,image,frame);
                    } else if(post==active_bloom_levels_+3u) {
                        if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,4);
                    } else throw std::logic_error("Unknown Vulkan HDR forward pass");
                    return;
                }
                switch(pass) {
                case 0:
                    record_opaque(cmd,image,frame);
                    if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,1);
                    break;
                case 1:
                    record_ui_overlay(cmd,image);
                    if(timing_queries_) {
                        vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,2);
                        vkCmdWriteTimestamp2(cmd,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,3);
                    }
                    break;
                case 2:record_readback(cmd,image,frame);break;
                case 3:
                    if(timing_queries_)vkCmdWriteTimestamp2(cmd,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timing_queries_,4);
                    break;
                default:throw std::logic_error("Unknown Vulkan forward pass");
                }
            }
        });
    check(vkEndCommandBuffer(command_),"vkEndCommandBuffer");
    check(vkResetFences(device_,1,&completed_),"vkResetFences");
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&available_;
    submit.pWaitDstStageMask=&stage;
    submit.commandBufferCount=1;submit.pCommandBuffers=&command_;
    submit.signalSemaphoreCount=1;
    VkSemaphore const presented_signal=present_ready_.at(image);
    submit.pSignalSemaphores=&presented_signal;
    check(vkQueueSubmit(queue_,1,&submit,completed_),"vkQueueSubmit");
    if(capture_requested_) {
        capture_requested_=false;
        capture_inflight_=true;
        capture_width_=extent_.width;
        capture_height_=extent_.height;
    }
    timing_pending_=timing_queries_!=VK_NULL_HANDLE;
    swapchain_seen_[image]=true;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount=1;
    present.pWaitSemaphores=&presented_signal;
    present.swapchainCount=1;present.pSwapchains=&swapchain_;
    present.pImageIndices=&image;
    const auto result=vkQueuePresentKHR(queue_,&present);
    if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR&&result!=VK_ERROR_OUT_OF_DATE_KHR)
        check(result,"vkQueuePresentKHR");
    // Swapchain-image-indexed present semaphores avoid a forced queue idle on
    // every frame. A GPU readback explicitly waits for its own submission.
    if(frame.verify_pixels) {
        check(diagnostics_.time_wait(VulkanWaitKind::frame_fence, [&] { return vkWaitForFences(device_,1,&completed_,VK_TRUE,10'000'000'000ull); }),
              "vkWaitForFences(pixel verification)");
        if(timing_pending_) {
            collect_completed_gpu_timing();
            timing_pending_=false;
        }
        validate_readback(frame);
    }
    if(validation_errors_.load()>0)
        throw std::runtime_error("Vulkan validation reported GPU errors");
    if(result==VK_ERROR_OUT_OF_DATE_KHR) {
        check(diagnostics_.time_wait(VulkanWaitKind::device_idle, [&] { return vkDeviceWaitIdle(device_); }),"vkDeviceWaitIdle(out-of-date present)");
        destroy_swapchain();create_swapchain();
    }
    // Only a successfully submitted/presented frame advances instance pose.
    temporal_instances_.commit();
    diagnostics_.end_frame();
}
void Renderer::collect_completed_gpu_timing() {
    if(timing_queries_) {
        std::uint64_t ticks[5]{};
        auto result=vkGetQueryPoolResults(device_,timing_queries_,0,5,
            sizeof(ticks),ticks,sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if(result==VK_SUCCESS) {
            const auto mask=timestamp_valid_bits_>=64?
                UINT64_MAX:((std::uint64_t{1}<<timestamp_valid_bits_)-1);
            bool monotonic=true;
            for(unsigned i=1;i<5;++i)
                if((ticks[i]&mask)<(ticks[i-1]&mask))monotonic=false;
            if(monotonic&&ticks[4]>ticks[0]) {
                const auto ms=timestamp_period_ns_/1e6;
                last_timing_={};
                last_timing_.gpu_timestamp_frequency_hz=static_cast<std::uint64_t>(
                    1e9/timestamp_period_ns_);
                last_timing_.total_gpu_ms=(ticks[4]-ticks[0])*ms;
                last_timing_.opaque_gpu_ms=(ticks[1]-ticks[0])*ms;
                last_timing_.deferred_gpu_ms=(ticks[2]-ticks[1])*ms;
                last_timing_.postfx_gpu_ms=(ticks[3]-ticks[2])*ms;
                last_timing_.hud_gpu_ms=(ticks[4]-ticks[3])*ms;
                last_timing_.valid=1;
            }else last_timing_={};
        }else last_timing_={};
    }
}
}
