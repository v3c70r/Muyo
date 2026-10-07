#include "CompiledRenderGraph.h"

namespace Muyo::RenderGraph
{
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
}
}  // namespace Muyo::RenderGraph
