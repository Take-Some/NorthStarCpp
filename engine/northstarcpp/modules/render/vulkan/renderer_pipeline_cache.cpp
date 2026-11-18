// Persistent Vulkan PSO cache with strict device UUID validation, size limit
// and fallback to uncached pipeline creation on corrupt/unavailable storage.
#include "renderer_private.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <vector>
namespace northstarcpp::render::vulkan {
namespace {
constexpr std::size_t max_cache_size=64u*1024u*1024u;
bool valid_blob(const std::vector<std::uint8_t>& bytes,
                VkPhysicalDeviceProperties const& properties) {
    if(bytes.size()<sizeof(VkPipelineCacheHeaderVersionOne)||
       bytes.size()>max_cache_size)return false;
    VkPipelineCacheHeaderVersionOne header{};
    std::memcpy(&header,bytes.data(),sizeof(header));
    return header.headerSize==sizeof(VkPipelineCacheHeaderVersionOne) &&
           header.headerVersion==VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID==properties.vendorID &&
           header.deviceID==properties.deviceID &&
           std::memcmp(header.pipelineCacheUUID,
               properties.pipelineCacheUUID,VK_UUID_SIZE)==0;
}
}
void Renderer::create_pipeline_cache() {
    if(!device_)return;
    auto const* disabled=std::getenv("NORTHSTARCPP_DISABLE_VULKAN_PSO_CACHE");
    if(disabled && disabled[0]=='1')return;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_,&properties);
    std::vector<std::uint8_t> data;
    try {
        auto const* local=std::getenv("LOCALAPPDATA");
        if(local && *local) {
            pipeline_cache_path_=std::filesystem::path(local)/
                "KaylasSystems"/"NorthStarCpp"/"PipelineCache"/
                ("vulkan_"+std::to_string(properties.vendorID)+"_"+
                 std::to_string(properties.deviceID)+".bin");
            std::ifstream stream(pipeline_cache_path_,std::ios::binary|std::ios::ate);
            if(stream) {
                const auto length=stream.tellg();
                if(length>=std::streamoff(sizeof(VkPipelineCacheHeaderVersionOne))&&
                   length<=std::streamoff(max_cache_size)) {
                    data.resize(static_cast<std::size_t>(length));
                    stream.seekg(0);
                    if(!stream.read(reinterpret_cast<char*>(data.data()),
                                    static_cast<std::streamsize>(data.size())) ||
                       !valid_blob(data,properties))
                        data.clear();
                }
            }
        }
    }catch(...) {data.clear();pipeline_cache_path_.clear();}
    VkPipelineCacheCreateInfo create{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if(!data.empty()) {
        create.initialDataSize=data.size();
        create.pInitialData=data.data();
    }
    auto result=vkCreatePipelineCache(device_,&create,nullptr,&pipeline_cache_);
    if(result!=VK_SUCCESS && !data.empty()) {
        create.initialDataSize=0;create.pInitialData=nullptr;
        result=vkCreatePipelineCache(device_,&create,nullptr,&pipeline_cache_);
    }
    if(result!=VK_SUCCESS)pipeline_cache_=VK_NULL_HANDLE;
}
VkResult Renderer::create_cached_graphics_pipelines(
    std::uint32_t count,const VkGraphicsPipelineCreateInfo* infos,
    VkPipeline* result) {
    std::lock_guard<std::mutex> lock(pipeline_cache_mutex_);
    return vkCreateGraphicsPipelines(device_,pipeline_cache_,count,
                                     infos,nullptr,result);
}
void Renderer::save_pipeline_cache() noexcept {
    if(!device_)return;
    std::lock_guard<std::mutex> lock(pipeline_cache_mutex_);
    if(!pipeline_cache_)return;
    try {
        if(!pipeline_cache_path_.empty()) {
            std::size_t bytes=0;
            if(vkGetPipelineCacheData(device_,pipeline_cache_,&bytes,nullptr)==
               VK_SUCCESS && bytes>0 && bytes<=max_cache_size) {
                std::vector<std::uint8_t> data(bytes);
                if(vkGetPipelineCacheData(device_,pipeline_cache_,&bytes,
                                          data.data())==VK_SUCCESS) {
                    data.resize(bytes);
                    std::error_code error;
                    std::filesystem::create_directories(
                        pipeline_cache_path_.parent_path(),error);
                    if(!error) {
                        const auto temp=pipeline_cache_path_.wstring()+L".tmp";
                        std::ofstream out(std::filesystem::path(temp),
                                          std::ios::binary|std::ios::trunc);
                        out.write(reinterpret_cast<char const*>(data.data()),
                                  static_cast<std::streamsize>(data.size()));
                        out.flush();
                        const bool saved=out.good();
                        out.close();
                        if(saved) {
                            MoveFileExW(temp.c_str(),
                                pipeline_cache_path_.c_str(),
                                MOVEFILE_REPLACE_EXISTING|
                                MOVEFILE_WRITE_THROUGH);
                        }
                    }
                }
            }
        }
    }catch(...) {
        // Cache disk failure must never prevent renderer shutdown.
    }
    vkDestroyPipelineCache(device_,pipeline_cache_,nullptr);
    pipeline_cache_=VK_NULL_HANDLE;
}
} // namespace northstarcpp::render::vulkan
