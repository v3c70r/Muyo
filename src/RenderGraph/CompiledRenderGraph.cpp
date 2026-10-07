#include "CompiledRenderGraph.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Muyo::RenderGraph
{
void CompiledRenderGraph::RebuildExecutionPlan(const RenderGraphQueueFamilies& families)
{
    m_executionPlan = RenderGraphExecutionPlan{};

    // ── Split the nodes into contiguous per-queue segments; CPU nodes run inline on the host. ──
    std::size_t segmentBegin = 0;
    QueueType segmentQueue = QueueType::COUNT;
    bool bInSegment = false;

    for (std::size_t i = 0; i < m_nodes.size(); ++i)
    {
        const CompiledRenderGraphNode& node = m_nodes[i];
        if (node.queueType == QueueType::CPU)
        {
            // A host-side node belongs to no queue segment, and a segment must never span one: the
            // recording loop would otherwise process the CPU node as GPU work, running its callback
            // a second time and emitting a barrier whose destination access mask is a HOST bit.
            // Close the open segment so every range contains GPU nodes only.
            if (bInSegment)
            {
                m_executionPlan.segments.push_back({segmentQueue, segmentBegin, i});
                bInSegment = false;
            }
            m_executionPlan.cpuNodes.push_back(i);
            continue;
        }

        const QueueType key = GetQueueKey(node.queueType, node.async);
        if (!bInSegment || key != segmentQueue)
        {
            if (bInSegment) m_executionPlan.segments.push_back({segmentQueue, segmentBegin, i});
            segmentBegin = i;
            segmentQueue = key;
            bInSegment = true;
        }
    }
    if (bInSegment) m_executionPlan.segments.push_back({segmentQueue, segmentBegin, m_nodes.size()});

    if (m_executionPlan.segments.empty()) return;

    // ── Detect resources handed from one queue family to another. ─────────────────────────────────
    std::unordered_map<ResourceHandle, std::size_t> lastSegmentForResource;
    for (std::size_t s = 0; s < m_executionPlan.segments.size(); ++s)
    {
        std::unordered_set<ResourceHandle> resourcesInSegment;
        for (std::size_t i = m_executionPlan.segments[s].begin; i < m_executionPlan.segments[s].end; ++i)
        {
            for (const auto& use : m_nodes[i].resourceUses)
            {
                resourcesInSegment.insert(use.handle);
            }
        }
        for (const auto& handle : resourcesInSegment)
        {
            auto it = lastSegmentForResource.find(handle);
            if (it != lastSegmentForResource.end() && it->second != s)
            {
                const uint32_t producerFamily =
                    GetQueueFamilyForQueueType(m_executionPlan.segments[it->second].queueType, families);
                const uint32_t consumerFamily =
                    GetQueueFamilyForQueueType(m_executionPlan.segments[s].queueType, families);
                if (producerFamily != consumerFamily)
                {
                    m_executionPlan.transfers.push_back({it->second, s, handle, producerFamily, consumerFamily});
                }
            }
            lastSegmentForResource[handle] = s;
        }
    }

    // Deterministic order: transfers are collected from unordered sets, and a stable plan makes
    // submission order (and therefore captures and logs) reproducible once the executor keys
    // semaphores off these entries.
    std::sort(m_executionPlan.transfers.begin(), m_executionPlan.transfers.end(),
              [](const RenderGraphQueueTransfer& a, const RenderGraphQueueTransfer& b)
              {
                  if (a.producer != b.producer) return a.producer < b.producer;
                  if (a.consumer != b.consumer) return a.consumer < b.consumer;
                  return a.handle < b.handle;
              });
}

void CompiledRenderGraph::Destroy(VkDevice device, VkDescriptorPool descriptorPool)
{
    for (CompiledRenderGraphNode& rgn : m_nodes)
    {
        if (rgn.ownsDescriptorSets && !rgn.descriptorSets.empty())
        {
            vkFreeDescriptorSets(device, descriptorPool, static_cast<uint32_t>(rgn.descriptorSets.size()),
                                 rgn.descriptorSets.data());
        }
        if (rgn.ownsDescriptorSetLayouts)
        {
            for (VkDescriptorSetLayout layout : rgn.descriptorSetLayouts)
            {
                if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
            }
        }
        if (rgn.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, rgn.pipelineLayout, nullptr);
        if (rgn.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, rgn.pipeline, nullptr);
    }
    m_nodes.clear();
    // The plan holds indices into m_nodes; leaving it populated would make a later Execute() index
    // out of range.
    m_executionPlan = RenderGraphExecutionPlan{};
}
}  // namespace Muyo::RenderGraph
