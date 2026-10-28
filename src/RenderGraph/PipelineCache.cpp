#include "RenderGraph/PipelineCache.h"

#include <stdexcept>
#include <utility>

#include "Debug.h"
#include "VkRenderDevice.h"

namespace Muyo::RenderGraph
{
PipelineCache::PipelineCache(VkRenderDevice* renderDevice)
    : PipelineCache(renderDevice, std::span<const std::uint8_t>{})
{
}

PipelineCache::PipelineCache(VkRenderDevice* renderDevice, std::span<const std::uint8_t> initialData)
{
    if (renderDevice == nullptr)
    {
        throw std::runtime_error(
            "PipelineCache: renderDevice is null, so there is no device to create or "
            "destroy pipelines on.");
    }
    m_device = renderDevice->GetDevice();
    if (m_device == VK_NULL_HANDLE)
    {
        throw std::runtime_error("PipelineCache: renderDevice has no logical device.");
    }

    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    createInfo.initialDataSize = initialData.size();
    createInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();
    VK_ASSERT(vkCreatePipelineCache(m_device, &createInfo, nullptr, &m_vkPipelineCache));
}

PipelineCache::~PipelineCache()
{
    Reset();
    if (m_vkPipelineCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(m_device, m_vkPipelineCache, nullptr);
        m_vkPipelineCache = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

VkPipeline PipelineCache::Find(std::string_view key) const
{
    const auto it = m_pipelines.find(std::string(key));
    return it == m_pipelines.end() ? VK_NULL_HANDLE : it->second;
}

bool PipelineCache::Insert(std::string_view key, VkPipeline pipeline)
{
    if (pipeline == VK_NULL_HANDLE)
    {
        // Caching a null handle would turn every later Find() into a miss that inserts another null, and
        // the caller would lose the place where the pipeline creation failed.
        throw std::runtime_error("PipelineCache::Insert: pipeline is VK_NULL_HANDLE.");
    }
    const auto [it, inserted] = m_pipelines.emplace(std::string(key), pipeline);
    (void)it;
    return inserted;
}

void PipelineCache::Reset()
{
    for (const auto& entry : m_pipelines)
    {
        if (entry.second != VK_NULL_HANDLE) vkDestroyPipeline(m_device, entry.second, nullptr);
    }
    m_pipelines.clear();
}

std::vector<std::uint8_t> PipelineCache::GetSerializedData() const
{
    if (m_vkPipelineCache == VK_NULL_HANDLE) return {};

    std::size_t size = 0;
    VK_ASSERT(vkGetPipelineCacheData(m_device, m_vkPipelineCache, &size, nullptr));
    if (size == 0) return {};

    std::vector<std::uint8_t> data(size);
    VK_ASSERT(vkGetPipelineCacheData(m_device, m_vkPipelineCache, &size, data.data()));
    // The second call can shrink the size when the driver's cache changed between the two calls; keep
    // only the bytes it actually wrote rather than whatever the vector happened to hold.
    data.resize(size);
    return data;
}
}  // namespace Muyo::RenderGraph
