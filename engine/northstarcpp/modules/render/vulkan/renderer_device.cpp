#include "renderer_private.hpp"
#include "shader_spirv.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace northstarcpp::render::vulkan {
namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(operation)+" failed: VkResult="+
                                 std::to_string(static_cast<int>(result)));
}
VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* info,void* context) {
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        static_cast<std::atomic<std::uint32_t>*>(context)->fetch_add(1);
    if(severity&(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|
                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT))
        std::fprintf(stderr,"[NorthStar Vulkan Validation][%s] %s\n",
            (severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)?
            "ERROR":"WARN",info&&info->pMessage?info->pMessage:"unknown");
    return VK_FALSE;
}
bool layer_supported(const char* wanted) {
    std::uint32_t count=0;
    vkEnumerateInstanceLayerProperties(&count,nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count,layers.data());
    return std::any_of(layers.begin(),layers.end(),[&](auto const& layer){
        return std::strcmp(layer.layerName,wanted)==0;
    });
}
bool extension_supported(VkPhysicalDevice gpu,const char* wanted) {
    std::uint32_t count=0;
    vkEnumerateDeviceExtensionProperties(gpu,nullptr,&count,nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(gpu,nullptr,&count,extensions.data());
    return std::any_of(extensions.begin(),extensions.end(),[&](auto const& e) {
        return std::strcmp(e.extensionName,wanted)==0;
    });
}
}
std::uint32_t Renderer::find_memory_type(std::uint32_t bits,
    VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_,&memory);
    for(std::uint32_t i=0;i<memory.memoryTypeCount;++i)
        if((bits&(1u<<i)) &&
            (memory.memoryTypes[i].propertyFlags&properties)==properties)return i;
    throw std::runtime_error("Vulkan GPU has no compatible memory type");
}
Renderer::Renderer(HWND window,bool software):hwnd_(window) {
    if(!hwnd_||!IsWindow(hwnd_))throw std::invalid_argument("Vulkan provider requires an HWND");
    try {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName="NorthStarCpp";app.applicationVersion=VK_MAKE_VERSION(1,0,0);
        app.pEngineName="NorthStarCpp";app.engineVersion=VK_MAKE_VERSION(1,0,0);
        app.apiVersion=VK_API_VERSION_1_3;
        const auto* validation_env=std::getenv("NORTHSTARCPP_VULKAN_VALIDATION");
        const bool validation=validation_env&&validation_env[0]=='1';
        if(validation&&!layer_supported("VK_LAYER_KHRONOS_validation"))
            throw std::runtime_error("Vulkan validation layer not installed");
        const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        const char* layers[]{"VK_LAYER_KHRONOS_validation"};
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo=&app;
        ici.enabledExtensionCount=validation?3u:2u;
        ici.ppEnabledExtensionNames=extensions;
        ici.enabledLayerCount=validation?1u:0u;
        ici.ppEnabledLayerNames=validation?layers:nullptr;
        check(vkCreateInstance(&ici,nullptr,&instance_),"vkCreateInstance");
        if(validation) {
            auto create_debug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_,"vkCreateDebugUtilsMessengerEXT"));
            if(!create_debug)throw std::runtime_error("Missing Vulkan debug utils entry point");
            VkDebugUtilsMessengerCreateInfoEXT debug{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug.pfnUserCallback=validation_callback;
            debug.pUserData=&validation_errors_;
            check(create_debug(instance_,&debug,nullptr,&messenger_),
                  "vkCreateDebugUtilsMessengerEXT");
        }
        VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        sci.hinstance=GetModuleHandleW(nullptr);
        sci.hwnd=hwnd_;
        check(vkCreateWin32SurfaceKHR(instance_,&sci,nullptr,&surface_),
              "vkCreateWin32SurfaceKHR");
        std::uint32_t count=0;
        check(vkEnumeratePhysicalDevices(instance_,&count,nullptr),
              "vkEnumeratePhysicalDevices");
        if(count==0)throw std::runtime_error("No Vulkan physical devices");
        std::vector<VkPhysicalDevice> gpus(count);
        check(vkEnumeratePhysicalDevices(instance_,&count,gpus.data()),
              "vkEnumeratePhysicalDevices(list)");
        // Software flag requests a Vulkan CPU adapter only. Do not silently
        // switch to a hardware adapter when the user asked for software.
        for(auto candidate:gpus) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate,&props);
            if(props.apiVersion<VK_API_VERSION_1_3 ||
               (software && props.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU) ||
               !extension_supported(candidate,VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                continue;
            VkPhysicalDeviceVulkan13Features f13{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            f2.pNext=&f13;
            vkGetPhysicalDeviceFeatures2(candidate,&f2);
            if(!f13.dynamicRendering||!f13.synchronization2)continue;
            std::uint32_t families=0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
            std::vector<VkQueueFamilyProperties> queues(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
            for(std::uint32_t q=0;q<families;++q) {
                VkBool32 present=VK_FALSE;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,q,surface_,&present),
                      "vkGetPhysicalDeviceSurfaceSupportKHR");
                if((queues[q].queueFlags&VK_QUEUE_GRAPHICS_BIT) && present) {
                    physical_=candidate;family_=q;break;
                }
            }
            if(physical_)break;
        }
        if(!physical_)
            throw std::runtime_error("Vulkan 1.3 graphics+present device with dynamic rendering/sync2 unavailable");
        const float priority=1.f;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex=family_;
        qi.queueCount=1;qi.pQueuePriorities=&priority;
        VkPhysicalDeviceVulkan13Features required{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        required.dynamicRendering=VK_TRUE;
        required.synchronization2=VK_TRUE;
        const char* swap_extension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.pNext=&required;
        dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qi;
        dci.enabledExtensionCount=1;dci.ppEnabledExtensionNames=&swap_extension;
        check(vkCreateDevice(physical_,&dci,nullptr,&device_),"vkCreateDevice");
        vkGetDeviceQueue(device_,family_,0,&queue_);
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex=family_;
        pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device_,&pci,nullptr,&command_pool_),
              "vkCreateCommandPool");
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool=command_pool_;ai.commandBufferCount=1;
        ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        check(vkAllocateCommandBuffers(device_,&ai,&command_),"vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device_,&si,nullptr,&available_),"vkCreateSemaphore available");
        check(vkCreateSemaphore(device_,&si,nullptr,&ready_),"vkCreateSemaphore ready");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device_,&fi,nullptr,&completed_),"vkCreateFence");
        VkPhysicalDeviceProperties gpu_properties{};
        vkGetPhysicalDeviceProperties(physical_,&gpu_properties);
        std::uint32_t family_count=0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_,&family_count,nullptr);
        std::vector<VkQueueFamilyProperties> family_properties(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_,&family_count,
                                                 family_properties.data());
        if(family_<family_count &&
           family_properties[family_].timestampValidBits>0 &&
           gpu_properties.limits.timestampPeriod>0.f) {
            VkQueryPoolCreateInfo pool{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            pool.queryType=VK_QUERY_TYPE_TIMESTAMP;pool.queryCount=5;
            check(vkCreateQueryPool(device_,&pool,nullptr,&timing_queries_),
                  "vkCreateQueryPool(timing)");
            timestamp_period_ns_=gpu_properties.limits.timestampPeriod;
            timestamp_valid_bits_=family_properties[family_].timestampValidBits;
        }
        create_vertex_buffer();
        create_swapchain();
        northstarcpp::render::RenderGraph graph;
        auto image=graph.add_resource("swapchain",true,
            northstarcpp::render::ResourceAccess::Present);
        auto p0=graph.add_pass({"geometry",{}, {image},{},
            {{image,northstarcpp::render::ResourceAccess::ColorAttachment}}});
        auto p1=graph.add_pass({"GPU readback",{image},{image},{p0},
            {{image,northstarcpp::render::ResourceAccess::CopySource}}});
        graph.add_pass({"present",{image},{},{p1},
            {{image,northstarcpp::render::ResourceAccess::Present}}});
        graph_plan_=graph.compile();
    }catch(...) {shutdown();throw;}
}
Renderer::~Renderer() noexcept {shutdown();}
void Renderer::destroy_pipeline() noexcept {
    if(!device_)return;
    if(pipeline_)vkDestroyPipeline(device_,pipeline_,nullptr);
    if(pipeline_layout_)vkDestroyPipelineLayout(device_,pipeline_layout_,nullptr);
    if(vertex_shader_)vkDestroyShaderModule(device_,vertex_shader_,nullptr);
    if(fragment_shader_)vkDestroyShaderModule(device_,fragment_shader_,nullptr);
    pipeline_=VK_NULL_HANDLE;pipeline_layout_=VK_NULL_HANDLE;
    vertex_shader_=fragment_shader_=VK_NULL_HANDLE;
}
void Renderer::destroy_readback() noexcept {
    if(!device_)return;
    if(readback_buffer_)vkDestroyBuffer(device_,readback_buffer_,nullptr);
    if(readback_memory_)vkFreeMemory(device_,readback_memory_,nullptr);
    readback_buffer_=VK_NULL_HANDLE;readback_memory_=VK_NULL_HANDLE;
    readback_capacity_=0;
}
void Renderer::destroy_vertex_buffer() noexcept {
    if(!device_)return;
    if(vertices_buffer_)vkDestroyBuffer(device_,vertices_buffer_,nullptr);
    if(vertices_memory_)vkFreeMemory(device_,vertices_memory_,nullptr);
    vertices_buffer_=VK_NULL_HANDLE;vertices_memory_=VK_NULL_HANDLE;
}
void Renderer::destroy_swapchain() noexcept {
    if(!device_)return;
    destroy_pipeline();
    destroy_readback();
    for(auto view:image_views_)
        if(view)vkDestroyImageView(device_,view,nullptr);
    image_views_.clear();images_.clear();swapchain_seen_.clear();
    if(swapchain_)vkDestroySwapchainKHR(device_,swapchain_,nullptr);
    swapchain_=VK_NULL_HANDLE;extent_={};format_=VK_FORMAT_UNDEFINED;
}
void Renderer::shutdown() noexcept {
    if(device_)vkDeviceWaitIdle(device_);
    destroy_swapchain();
    destroy_vertex_buffer();
    if(device_) {
        if(completed_)vkDestroyFence(device_,completed_,nullptr);
        if(timing_queries_)vkDestroyQueryPool(device_,timing_queries_,nullptr);
        if(available_)vkDestroySemaphore(device_,available_,nullptr);
        if(ready_)vkDestroySemaphore(device_,ready_,nullptr);
        if(command_pool_)vkDestroyCommandPool(device_,command_pool_,nullptr);
        vkDestroyDevice(device_,nullptr);
    }
    device_=VK_NULL_HANDLE;
    if(surface_&&instance_)vkDestroySurfaceKHR(instance_,surface_,nullptr);
    surface_=VK_NULL_HANDLE;
    if(messenger_&&instance_) {
        auto destroy_debug=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_,"vkDestroyDebugUtilsMessengerEXT"));
        if(destroy_debug)destroy_debug(instance_,messenger_,nullptr);
    }
    messenger_=VK_NULL_HANDLE;
    if(instance_)vkDestroyInstance(instance_,nullptr);
    instance_=VK_NULL_HANDLE;
}
void Renderer::create_vertex_buffer() {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size=max_vertex_bytes_;ci.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    check(vkCreateBuffer(device_,&ci,nullptr,&vertices_buffer_),"vkCreateBuffer(vertices)");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_,vertices_buffer_,&req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize=req.size;
    ai.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&ai,nullptr,&vertices_memory_),"vkAllocateMemory(vertices)");
    check(vkBindBufferMemory(device_,vertices_buffer_,vertices_memory_,0),"vkBindBufferMemory(vertices)");
}
void Renderer::create_readback() {
    destroy_readback();
    const VkDeviceSize bytes=static_cast<VkDeviceSize>(extent_.width)*extent_.height*4;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size=bytes;ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(device_,&ci,nullptr,&readback_buffer_),"vkCreateBuffer(readback)");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_,readback_buffer_,&req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize=req.size;
    ai.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&ai,nullptr,&readback_memory_),"vkAllocateMemory(readback)");
    check(vkBindBufferMemory(device_,readback_buffer_,readback_memory_,0),
          "vkBindBufferMemory(readback)");
    readback_capacity_=bytes;
}
void Renderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_,surface_,&caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::uint32_t count=0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if(!count)throw std::runtime_error("Vulkan surface has no formats");
    std::vector<VkSurfaceFormatKHR> choices(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,choices.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR(list)");
    VkSurfaceFormatKHR chosen=choices.front();
    for(auto const& format:choices) {
        if(format.format==VK_FORMAT_B8G8R8A8_UNORM) {chosen=format;break;}
        if(format.format==VK_FORMAT_B8G8R8A8_SRGB)chosen=format;
    }
    VkExtent2D dimensions=caps.currentExtent;
    if(dimensions.width==UINT32_MAX) {
        RECT client{};
        if(!GetClientRect(hwnd_,&client))throw std::runtime_error("Vulkan GetClientRect failed");
        const auto w=static_cast<std::uint32_t>(std::max<LONG>(1,client.right-client.left));
        const auto h=static_cast<std::uint32_t>(std::max<LONG>(1,client.bottom-client.top));
        dimensions.width=std::clamp(w,caps.minImageExtent.width,caps.maxImageExtent.width);
        dimensions.height=std::clamp(h,caps.minImageExtent.height,caps.maxImageExtent.height);
    }
    if(!dimensions.width||!dimensions.height)
        throw std::runtime_error("Zero-sized Vulkan drawable");
    if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        throw std::runtime_error("Vulkan swapchain cannot support GPU pixel verification");
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface=surface_;
    sci.minImageCount=std::max(2u,caps.minImageCount);
    if(caps.maxImageCount&&sci.minImageCount>caps.maxImageCount)
        sci.minImageCount=caps.maxImageCount;
    sci.imageFormat=chosen.format;
    sci.imageColorSpace=chosen.colorSpace;
    sci.imageExtent=dimensions;
    sci.imageArrayLayers=1;
    sci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform=caps.currentTransform;
    sci.compositeAlpha=(caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)?
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR:VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if(!(caps.supportedCompositeAlpha&sci.compositeAlpha))
        throw std::runtime_error("Vulkan unsupported swapchain alpha");
    sci.presentMode=VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped=VK_TRUE;
    check(vkCreateSwapchainKHR(device_,&sci,nullptr,&swapchain_),"vkCreateSwapchainKHR");
    count=0;
    check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,nullptr),
          "vkGetSwapchainImagesKHR");
    images_.resize(count);
    check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,images_.data()),
          "vkGetSwapchainImagesKHR(list)");
    image_views_.resize(count,VK_NULL_HANDLE);
    swapchain_seen_.resize(count,false);
    extent_=dimensions;format_=chosen.format;swapchain_transfer_src_=true;
    for(std::uint32_t i=0;i<count;++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=images_[i];vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vi.format=format_;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount=1;vi.subresourceRange.layerCount=1;
        check(vkCreateImageView(device_,&vi,nullptr,&image_views_[i]),
              "vkCreateImageView(swapchain)");
    }
    create_readback();
    create_pipeline();
}
void Renderer::create_pipeline() {
    const auto shader=[&](const std::uint32_t* words,std::size_t n,
                          VkShaderModule& module) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize=n*4;ci.pCode=words;
        check(vkCreateShaderModule(device_,&ci,nullptr,&module),"vkCreateShaderModule");
    };
    shader(shaders::basic_vertex,std::size(shaders::basic_vertex),vertex_shader_);
    shader(shaders::basic_fragment,std::size(shaders::basic_fragment),fragment_shader_);
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    check(vkCreatePipelineLayout(device_,&li,nullptr,&pipeline_layout_),
          "vkCreatePipelineLayout");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(int i=0;i<2;++i) {
        stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i==0?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module=i==0?vertex_shader_:fragment_shader_;
        stages[i].pName="main";
    }
    VkVertexInputBindingDescription binding{0,sizeof(ColoredVertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0]={0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0};
    attributes[1]={1,0,VK_FORMAT_R32G32B32A32_SFLOAT,16};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;
    vertex.vertexAttributeDescriptionCount=2;vertex.pVertexAttributeDescriptions=attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount=1;viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;
    raster.cullMode=VK_CULL_MODE_NONE;
    raster.frontFace=VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth=1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                               VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1;blend.pAttachments=&attachment;
    VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount=2;dyn.pDynamicStates=dynamics;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&format_;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext=&rendering;
    pi.stageCount=2;pi.pStages=stages;
    pi.pVertexInputState=&vertex;pi.pInputAssemblyState=&assembly;
    pi.pViewportState=&viewport;pi.pRasterizationState=&raster;
    pi.pMultisampleState=&ms;pi.pColorBlendState=&blend;pi.pDynamicState=&dyn;
    pi.layout=pipeline_layout_;
    check(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline_),
          "vkCreateGraphicsPipelines");
}
}
