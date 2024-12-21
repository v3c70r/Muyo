/* This is an example to serialize PSO cache file to disk
 */

#include <vulkan/vulkan_core.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>

#include "PipelineStateBuilder.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
using namespace Muyo;

std::vector<VkDescriptorSetLayout> CreateDescriptorSetLayout()
{
    std::vector<VkDescriptorSetLayout> res(2, VK_NULL_HANDLE);

    // Set0, binding 0
    VkDescriptorSetLayoutBinding bindingInfo = {};
    bindingInfo.binding                      = 0;
    bindingInfo.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindingInfo.descriptorCount              = 1;
    bindingInfo.stageFlags                   = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pBindings                       = &bindingInfo;
    layoutInfo.bindingCount                    = 1;

    VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(), &layoutInfo, nullptr, &res[0]));

    bindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(), &layoutInfo, nullptr, &res[1]));

    return res;
}

VkPipelineLayout CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& descLayouts)
{
    VkPipelineLayoutCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.setLayoutCount = static_cast<uint32_t>(descLayouts.size());
    createInfo.pSetLayouts = descLayouts.data();
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VK_ASSERT(vkCreatePipelineLayout(GetRenderDevice()->GetDevice(), &createInfo, nullptr, &pipelineLayout));
    return pipelineLayout;
}

VkPipeline CreatePipeline(VkPipelineLayout pipelineLayout, VkPipelineCache pipelineCache)
{
    ComputePipelineBuilder builder;

    VkShaderModule compShader = CreateShaderModule(ReadSpv("shaders/linearizeDepth.comp.spv"));

    VkComputePipelineCreateInfo createInfo = builder.AddShaderModule(compShader).SetPipelineLayout(pipelineLayout).Build();

    VkPipeline computePipeline = VK_NULL_HANDLE;
    vkCreateComputePipelines(GetRenderDevice()->GetDevice(), pipelineCache, 1, &createInfo, nullptr, &computePipeline);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), compShader, nullptr);
    return computePipeline;
}

VkPipelineCache CreatePipelineCache()
{
    VkPipelineCacheCreateInfo info = {};
    info.sType                     = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize           = 0;
    info.pInitialData              = nullptr;

    VkPipelineCache cache = VK_NULL_HANDLE;
    vkCreatePipelineCache(
      GetRenderDevice()->GetDevice(),
      &info,
      nullptr,
      &cache);

    return cache;
}

void SerializeCacheData(VkPipelineCache pipelineCache, std::filesystem::path filePath)
{
    size_t bufferSize = 0;
    VK_ASSERT(vkGetPipelineCacheData(GetRenderDevice()->GetDevice(), pipelineCache, &bufferSize, nullptr));
    bufferSize += sizeof(size_t); // buffer contains buffer size and buffer


    uint8_t* buffer = new uint8_t[bufferSize + sizeof(size_t)];
    memcpy(buffer, &bufferSize, sizeof(size_t));

    uint8_t* cacheBuffer = buffer + sizeof(size_t);
    VK_ASSERT(vkGetPipelineCacheData(GetRenderDevice()->GetDevice(), pipelineCache, &bufferSize, cacheBuffer));

    std::ofstream outputFile(filePath);

    outputFile.write(reinterpret_cast<const char*>(buffer), bufferSize);
    outputFile.close();

    // Extract header
    struct VkPipelineCacheHeaderVersionOne
    {
        uint32_t headerSize;
        VkPipelineCacheHeaderVersion headerVersion;
        uint32_t vendorID;
        uint32_t deviceID;
        uint8_t pipelineCacheUUID[VK_UUID_SIZE];
    } pipelineCacheHeader;

    memcpy(&pipelineCacheHeader, cacheBuffer, sizeof(VkPipelineCacheHeaderVersionOne));

    delete[] buffer;
}

VkPipelineCache DeserializeCacheData(std::filesystem::path filePath)
{
    std::ifstream inFile(filePath, std::ios::binary);
    size_t fileSize = 0;
    inFile.read(reinterpret_cast<char*>(&fileSize), sizeof(size_t));
    assert (fileSize != 0);
    uint8_t* buffer = new uint8_t[fileSize];
    inFile.read(reinterpret_cast<char*>(buffer), fileSize);

    VkPipelineCacheCreateInfo info = {};
    info.sType                     = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize           = fileSize;
    info.pInitialData              = buffer+sizeof(size_t);

    VkPipelineCache cache = VK_NULL_HANDLE;
    vkCreatePipelineCache(
      GetRenderDevice()->GetDevice(),
      &info,
      nullptr,
      &cache);

    return cache;
}

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;

class ScopedTimer
{
  public:
    ScopedTimer()
      : m_StartTime(high_resolution_clock::now())
    {
    }
    ~ScopedTimer()
    {
        auto endTime = high_resolution_clock::now();
        auto elapsedTime = duration_cast<std::chrono::nanoseconds>(endTime - m_StartTime);
        std::cout << elapsedTime.count() << "ns\n";
    }

  private:
    std::chrono::steady_clock::time_point m_StartTime;
};

int main()
{
    std::vector<const char*> vInstanceExtensions = {};
    Muyo::GetRenderDevice()->Initialize(vInstanceExtensions);

    std::vector<const char*> vDeviceExtensions = {};
    Muyo::GetRenderDevice()->CreateDevice(vDeviceExtensions,              // Extensions
                                          std::vector<const char*>());    // Layers

    GetMemoryAllocator()->Initalize(GetRenderDevice());
    GetRenderDevice()->CreateCommandPools();

    std::vector<VkDescriptorSetLayout> descLayouts = CreateDescriptorSetLayout();
    VkPipelineLayout layout = CreatePipelineLayout(descLayouts);

    std::filesystem::path cacheBinary("psoCache.bin");

    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    if (std::filesystem::exists(cacheBinary))
    {
        pipelineCache = DeserializeCacheData(cacheBinary);
    }
    else
    {
        pipelineCache = CreatePipelineCache();
    }

    VkPipeline pipeline = VK_NULL_HANDLE;

    {
        ScopedTimer timer;
        pipeline = CreatePipeline(layout, pipelineCache);
    }

    if (!std::filesystem::exists(cacheBinary))
    {
        SerializeCacheData(pipelineCache, cacheBinary);
    }

    vkDestroyPipeline(GetRenderDevice()->GetDevice(), pipeline, nullptr);
    vkDestroyPipelineCache(GetRenderDevice()->GetDevice(), pipelineCache, nullptr);

    vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(), descLayouts[0], nullptr);
    vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(), descLayouts[1], nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), layout, nullptr);

    // Clean up
    GetRenderDevice() ->DestroyCommandPools();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
    return 0;
}

