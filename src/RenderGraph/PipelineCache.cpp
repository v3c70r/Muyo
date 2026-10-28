#include "RenderGraph/PipelineCache.h"

#include <stdexcept>
#include <utility>

#include "VkRenderDevice.h"

namespace Muyo::RenderGraph
{
PipelineCache::PipelineCache(VkRenderDevice* renderDevice)
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
}

PipelineCache::~PipelineCache()
{
    Reset();
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
}  // namespace Muyo::RenderGraph
