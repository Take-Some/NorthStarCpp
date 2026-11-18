// Native Vulkan GPU skinned PBR: original eight YDD influences per vertex.
// GPU compute writes the same MaterialClipVertex layout consumed by the
// existing material PSO. No host-side bone deformation or OBJ expansion.
#include "renderer_private.hpp"
#include "renderer_material_submission.hpp"
#include "../../shared/frame_memory_counters.hpp"
#include "material_spirv.hpp"
#include "material_graph_glsl.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::render::vulkan {
namespace frame_memory = northstarcpp::diagnostics::frame_memory;
namespace {
void check(VkResult result,const char* name) {
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(name)+" failed: VkResult="+
                                 std::to_string(static_cast<int>(result)));
}
constexpr std::uint32_t max_skin_vertices=
    NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES;
}
void Renderer::set_skinned_material_batches(
    const NorthStarSkinnedMaterialBatch* batches,std::uint32_t count)
{
    if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES || (count&&!batches))
        throw std::invalid_argument("Vulkan GPU skin batch count/pointer invalid");
    std::vector<SkinSource> next;
    frame_memory::reserve(next,count);
    std::uint32_t total_vertices=0;
    std::uint64_t total_textures=0;
    VkDeviceSize palette_floats=0;
    bool textures_same=count==skinned_sources_.size();
    bool geometry_same=count==skinned_sources_.size();
    for(std::uint32_t i=0;i<count;++i) {
        const auto& input=batches[i];
        const auto& policy=input.material;
        const auto& material=policy.material;
        if(policy.graph_hlsl_bytes || policy.blend_mode!=0 ||
           !std::isfinite(policy.opacity) || policy.opacity!=1.f)
            throw std::invalid_argument(
                "Vulkan native GPU skin v1 requires opaque, default PBR");
        if(!input.vertices || !input.palette || !input.vertex_count ||
           input.vertex_count%3 || input.joint_count==0 ||
           input.joint_count>NORTHSTARCPP_SKIN_MAX_JOINTS ||
           input.vertex_count>max_skin_vertices-total_vertices)
            throw std::invalid_argument("Vulkan native GPU skin topology invalid");
        SkinSource source;
        auto const* previous=i<skinned_sources_.size()?&skinned_sources_[i]:nullptr;
        frame_memory::assign(source.palette,input.palette,
            input.palette+std::size_t(input.joint_count)*16u,
            &NorthStarFrameMemoryCounters::palette_copy_bytes);
        for(float f:source.palette)
            if(!std::isfinite(f)||std::abs(f)>1e6f)
                throw std::invalid_argument("Invalid GPU bone palette");
        if(!previous || previous->palette.size()!=source.palette.size() ||
           !matches_geometry(previous->vertices,input.vertices,input.vertex_count)) {
            geometry_same=false;
            for(std::uint32_t index=0;index<input.vertex_count;++index) {
                auto const& vertex=input.vertices[index];
                for(float value:{vertex.x,vertex.y,vertex.z,
                                 vertex.nx,vertex.ny,vertex.nz,vertex.u,vertex.v})
                    if(!std::isfinite(value)||std::abs(value)>1e6f)
                        throw std::invalid_argument("Invalid native skin vertex");
                float weights=0.f;
                for(unsigned influence=0;influence<8;++influence) {
                    auto w=vertex.weights[influence];
                    if(!std::isfinite(w)||w<0.f||w>1.f ||
                       (w>0.f&&vertex.joints[influence]>=input.joint_count))
                        throw std::invalid_argument("Invalid GPU bone influence");
                    weights+=w;
                }
                if(std::abs(weights-1.f)>.01f)
                    throw std::invalid_argument("GPU skin weights must sum to one");
            }
            frame_memory::assign(source.vertices,input.vertices,input.vertices+input.vertex_count,
                &NorthStarFrameMemoryCounters::geometry_copy_bytes);
        }
        auto& output=source.material;
        auto const* previous_material=previous?&previous->material:nullptr;
        output.roughness=material.roughness;
        output.metallic=material.metallic;
        output.unlit=material.unlit;
        if(!std::isfinite(output.roughness)||output.roughness<0.f||
           output.roughness>1.f||!std::isfinite(output.metallic)||
           output.metallic<0.f||output.metallic>1.f||output.unlit>1)
            throw std::invalid_argument("Invalid GPU skin PBR parameters");
        for(unsigned k=0;k<4;++k) {
            auto value=material.base_color[k];
            if(!std::isfinite(value)||value<0.f||value>1.f)
                throw std::invalid_argument("Invalid GPU skin material tint");
            output.tint[k]=value;
        }
        for(unsigned slot=0;slot<5;++slot) {
            auto const& image=material.textures[slot];
            if(!image.rgba8) {
                if(image.width||image.height||image.data_bytes)
                    throw std::invalid_argument("Malformed GPU skin texture slot");
                textures_same &= stage_material_texture(output,previous_material,slot,image);
                continue;
            }
            if(!image.width||!image.height||image.width>4096||
               image.height>4096||std::uint64_t(image.width)*image.height*4!=
               image.data_bytes)
                throw std::invalid_argument("Invalid GPU skin material texture");
            total_textures+=image.data_bytes;
            if(total_textures>256u*1024u*1024u)
                throw std::invalid_argument("GPU skin material texture budget exceeded");
            textures_same &= stage_material_texture(output,previous_material,slot,image);
        }
        total_vertices+=input.vertex_count;
        palette_floats+=source.palette.size();
        next.push_back(std::move(source));
    }
    for(std::size_t i=0;i<std::min(next.size(),skinned_sources_.size());++i) {
        adopt_retained_textures(next[i].material,skinned_sources_[i].material);
        if(next[i].vertices.empty())next[i].vertices.swap(skinned_sources_[i].vertices);
    }
    skinned_sources_=std::move(next);
    skin_vertex_count_=total_vertices;
    skin_palette_floats_=palette_floats;
    if(!textures_same)material_textures_dirty_=true;
    if(!geometry_same)skin_geometry_dirty_=true;
}
void Renderer::create_skin_resources() {
    if(skin_pipeline_)return;
    try {
        std::array<VkDescriptorSetLayoutBinding,3> bindings{};
        for(unsigned i=0;i<bindings.size();++i) {
            bindings[i].binding=i;
            bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount=1;
            bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount=static_cast<std::uint32_t>(bindings.size());
        layout.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&skin_set_layout_),
              "vkCreateDescriptorSetLayout(GPU skin)");
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};
        VkDescriptorPoolCreateInfo pool{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;
        check(vkCreateDescriptorPool(device_,&pool,nullptr,&skin_descriptor_pool_),
              "vkCreateDescriptorPool(GPU skin)");
        VkDescriptorSetAllocateInfo allocate{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool=skin_descriptor_pool_;
        allocate.descriptorSetCount=1;
        allocate.pSetLayouts=&skin_set_layout_;
        check(vkAllocateDescriptorSets(device_,&allocate,&skin_set_),
              "vkAllocateDescriptorSets(GPU skin)");
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize=sizeof(shaders::material_skin_compute);
        shader.pCode=shaders::material_skin_compute;
        check(vkCreateShaderModule(device_,&shader,nullptr,&skin_shader_),
              "vkCreateShaderModule(GPU skin compute)");
        VkPushConstantRange push{};
        push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        push.size=sizeof(SkinComputePush);
        VkPipelineLayoutCreateInfo pipeline_layout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_layout.setLayoutCount=1;
        pipeline_layout.pSetLayouts=&skin_set_layout_;
        pipeline_layout.pushConstantRangeCount=1;
        pipeline_layout.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device_,&pipeline_layout,nullptr,
                                     &skin_pipeline_layout_),
              "vkCreatePipelineLayout(GPU skin)");
        VkComputePipelineCreateInfo compute{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.layout=skin_pipeline_layout_;
        compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module=skin_shader_;
        compute.stage.pName="main";
        check(vkCreateComputePipelines(device_,VK_NULL_HANDLE,1,&compute,nullptr,
                                       &skin_pipeline_),
              "vkCreateComputePipelines(GPU skin)");
    }catch(...) {destroy_skin_resources();throw;}
}
void Renderer::ensure_skin_buffers() {
    if(!skin_vertex_count_)return;
    constexpr auto host_memory=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    struct Binding {
        SkinBuffer& buffer;
        VkDeviceSize required;
        VkBufferUsageFlags usage;
        VkMemoryPropertyFlags memory;
    };
    const std::array<Binding,3> bindings{{
        {skin_input_,skin_vertex_count_*sizeof(NorthStarSkinMaterialVertex),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host_memory},
        {skin_output_,skin_vertex_count_*sizeof(MaterialClipVertex),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT},
        {skin_palette_,skin_palette_floats_*sizeof(float),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host_memory}
    }};
    if(std::all_of(bindings.begin(),bindings.end(),[](auto const& binding) {
        return binding.required<=binding.buffer.capacity;
    }))return;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_,&properties);
    const VkDeviceSize limit=properties.limits.maxStorageBufferRange;
    for(auto const& binding:bindings)
        if(binding.required>limit)
            throw std::runtime_error("GPU skin storage buffer requires "+
                std::to_string(binding.required)+" bytes, device range is "+
                std::to_string(limit));
    create_skin_resources();

    std::array<SkinBuffer,3> replacement{};
    try {
        for(std::size_t i=0;i<bindings.size();++i) {
            auto const& binding=bindings[i];
            if(binding.required<=binding.buffer.capacity)continue;
            auto& next=replacement[i];
            const auto current=binding.buffer.capacity;
            next.capacity=std::min(limit,std::max(binding.required,current+current/2));
            VkBufferCreateInfo desc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            desc.size=next.capacity;
            desc.usage=binding.usage;
            check(vkCreateBuffer(device_,&desc,nullptr,&next.handle),
                "vkCreateBuffer(GPU skin)");
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device_,next.handle,&requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize=requirements.size;
            allocation.memoryTypeIndex=find_memory_type(
                requirements.memoryTypeBits,binding.memory);
            check(vkAllocateMemory(device_,&allocation,nullptr,&next.memory),
                "vkAllocateMemory(GPU skin)");
            frame_memory::device_allocated(requirements.size);
            diagnostics_.add(VulkanCounter::skin_buffer_grows);
            check(vkBindBufferMemory(device_,next.handle,next.memory,0),
                "vkBindBufferMemory(GPU skin)");
        }
    }catch(...) {
        for(auto& buffer:replacement)destroy_skin_buffer(buffer);
        throw;
    }

    VkDescriptorBufferInfo info[3]{};
    VkWriteDescriptorSet writes[3]{};
    for(std::uint32_t i=0;i<bindings.size();++i) {
        auto& buffer=bindings[i].buffer;
        if(replacement[i].handle) {
            if(i==0)skin_geometry_dirty_=true;
            std::swap(buffer,replacement[i]);
        }
        info[i]={buffer.handle,0,buffer.capacity};
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet=skin_set_;
        writes[i].dstBinding=i;
        writes[i].descriptorCount=1;
        writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo=&info[i];
    }
    vkUpdateDescriptorSets(device_,3,writes,0,nullptr);
    for(auto& buffer:replacement)destroy_skin_buffer(buffer);
}
void Renderer::destroy_skin_buffer(SkinBuffer& buffer) noexcept {
    if(buffer.handle)vkDestroyBuffer(device_,buffer.handle,nullptr);
    if(buffer.memory)vkFreeMemory(device_,buffer.memory,nullptr);
    buffer={};
}
void Renderer::destroy_skin_resources() noexcept {
    if(!device_)return;
    if(skin_pipeline_)vkDestroyPipeline(device_,skin_pipeline_,nullptr);
    if(skin_pipeline_layout_)
        vkDestroyPipelineLayout(device_,skin_pipeline_layout_,nullptr);
    if(skin_shader_)vkDestroyShaderModule(device_,skin_shader_,nullptr);
    if(skin_descriptor_pool_)
        vkDestroyDescriptorPool(device_,skin_descriptor_pool_,nullptr);
    if(skin_set_layout_)
        vkDestroyDescriptorSetLayout(device_,skin_set_layout_,nullptr);
    destroy_skin_buffer(skin_input_);
    destroy_skin_buffer(skin_output_);
    destroy_skin_buffer(skin_palette_);
    skin_pipeline_=VK_NULL_HANDLE;skin_pipeline_layout_=VK_NULL_HANDLE;
    skin_shader_=VK_NULL_HANDLE;skin_descriptor_pool_=VK_NULL_HANDLE;
    skin_set_layout_=VK_NULL_HANDLE;skin_set_=VK_NULL_HANDLE;
    skin_geometry_dirty_=true;
}
void Renderer::upload_skin_geometry() {
    skin_draws_.clear();
    if(skinned_sources_.empty())return;
    if(!skin_pipeline_)throw std::logic_error("Vulkan GPU skin is uninitialized");
    // The input mesh is immutable between topology updates: the GPU compute
    // shader still runs every frame, but only palette matrices must be updated.
    // A resized skin input buffer always sets skin_geometry_dirty_ again.
    void* vertices=nullptr;
    void* bones=nullptr;
    bool palette_mapped=false;
    if(skin_geometry_dirty_)
        check(vkMapMemory(device_,skin_input_.memory,0,
            skin_vertex_count_*sizeof(NorthStarSkinMaterialVertex),0,&vertices),
            "vkMapMemory(GPU skin vertices)");
    try {
        check(vkMapMemory(device_,skin_palette_.memory,0,
            skin_palette_floats_*sizeof(float),0,&bones),
            "vkMapMemory(GPU bone matrices)");
        palette_mapped=true;
        if(vertices) {
            std::uint64_t vertex_offset=0;
            for(auto const& source:skinned_sources_) {
                auto const bytes=source.vertices.size()*sizeof(NorthStarSkinMaterialVertex);
                std::memcpy(static_cast<std::uint8_t*>(vertices)+
                    vertex_offset*sizeof(NorthStarSkinMaterialVertex),
                    source.vertices.data(),bytes);
                vertex_offset+=source.vertices.size();
                diagnostics_.add(VulkanCounter::skin_vertex_upload_bytes,bytes);
                frame_memory::copied(&NorthStarFrameMemoryCounters::geometry_copy_bytes,bytes);
            }
        }
        auto* dest=static_cast<float*>(bones);
        for(auto const& source:skinned_sources_) {
            dest=std::copy(source.palette.begin(),source.palette.end(),dest);
            auto const bytes=source.palette.size()*sizeof(float);
            diagnostics_.add(VulkanCounter::skin_palette_upload_bytes,bytes);
            frame_memory::copied(&NorthStarFrameMemoryCounters::palette_copy_bytes,bytes);
        }
        vkUnmapMemory(device_,skin_palette_.memory);
        palette_mapped=false;
    }catch(...) {
        if(palette_mapped)vkUnmapMemory(device_,skin_palette_.memory);
        if(vertices)vkUnmapMemory(device_,skin_input_.memory);
        throw;
    }
    if(vertices)vkUnmapMemory(device_,skin_input_.memory);
    skin_geometry_dirty_=false;
    std::uint32_t first=0,palette_offset=0;
    frame_memory::reserve(skin_draws_,skinned_sources_.size());
    for(std::size_t index=0;index<skinned_sources_.size();++index) {
        SkinDraw draw{};
        const auto& source=skinned_sources_[index];
        draw.first=first;
        draw.count=static_cast<std::uint32_t>(source.vertices.size());
        draw.palette_offset=palette_offset;
        draw.textures=material_sets_.at(material_sources_.size()+index);
        draw.pipeline=material_graph_pipeline(
            skinned_sources_[index].material.variant_prefix,0);
        if(deferred_enabled_)
            draw.gbuffer_pipeline=material_graph_pipeline(
                skinned_sources_[index].material.variant_prefix,0,true);
        skin_draws_.push_back(draw);
        first+=draw.count;
        palette_offset+=static_cast<std::uint32_t>(source.palette.size());
    }
}
void Renderer::record_skin_compute(VkCommandBuffer command,
                                    const NorthStarRenderFrame& frame) {
    if(skin_draws_.empty())return;
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,skin_pipeline_);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,
        skin_pipeline_layout_,0,1,&skin_set_,0,nullptr);
    float const near_clip=frame.camera_near_clip>0.f?
        frame.camera_near_clip:.1f;
    float const far_clip=frame.camera_far_clip>near_clip?
        frame.camera_far_clip:250.f;
    float const fov=frame.camera_fov_y_degrees>1.f?
        frame.camera_fov_y_degrees:70.f;
    const float factor=1.f/std::tan(fov*3.14159265f/360.f);
    const float aspect=static_cast<float>(extent_.width)/extent_.height;
    const float cy=std::cos(frame.camera_yaw),sy=std::sin(frame.camera_yaw);
    const float cp=std::cos(frame.camera_pitch),sp=std::sin(frame.camera_pitch);
    for(std::size_t i=0;i<skin_draws_.size();++i) {
        const auto& draw=skin_draws_[i];
        const auto& material=skinned_sources_[i].material;
        SkinComputePush push{};
        push.camera[0]=frame.camera_x;push.camera[1]=frame.camera_y;
        push.camera[2]=frame.camera_z;
        push.angles[0]=cy;push.angles[1]=sy;
        push.angles[2]=cp;push.angles[3]=sp;
        push.projection[0]=factor;push.projection[1]=aspect;
        push.projection[2]=near_clip;push.projection[3]=far_clip;
        std::memcpy(push.tint,material.tint.data(),sizeof(push.tint));
        unsigned textures=0;
        for(unsigned slot=0;slot<5;++slot)
            if(!material.textures[slot].empty())textures|=1u<<slot;
        push.params[0]=material.metallic;
        push.params[1]=material.roughness;
        push.params[2]=material.unlit?1.f:0.f;
        push.params[3]=static_cast<float>(textures);
        push.offsets[3]=draw.palette_offset;
        // Vulkan guarantees at least 65,535 workgroups along X. Large
        // sections can use multiple dispatches with disjoint output ranges.
        constexpr std::uint32_t vertices_per_dispatch=65'535u*64u;
        for(std::uint32_t vertex=0;vertex<draw.count;) {
            const auto count=std::min(draw.count-vertex,vertices_per_dispatch);
            push.offsets[0]=push.offsets[1]=draw.first+vertex;
            push.offsets[2]=count;
            vkCmdPushConstants(command,skin_pipeline_layout_,
                VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
            vkCmdDispatch(command,(count+63)/64,1,1);
            vertex+=count;
        }
    }
    VkBufferMemoryBarrier2 transition{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    transition.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    transition.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    transition.dstStageMask=VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    transition.dstAccessMask=VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    transition.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.buffer=skin_output_.handle;
    transition.offset=0;
    transition.size=skin_vertex_count_*sizeof(MaterialClipVertex);
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount=1;
    dependency.pBufferMemoryBarriers=&transition;
    vkCmdPipelineBarrier2(command,&dependency);
}
} // namespace northstarcpp::render::vulkan
