#pragma once
#include "VkRenderDevice.h"

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <vector>

// Trying to manage all of the pipelines here
// Should be able to cache pipelines
class PipelineManager
{
    ~PipelineManager();
    void CreateStaticObjectPipeline();
public:
    std::unordered_map<std::string, VkPipeline> m_vPipelines;
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
    std::vector<char> ReadSpv(const std::string& fileName);

    VkPipelineInputAssemblyStateCreateInfo GetIAInfo(
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        bool bEnablePrimtiveStart = false);

    VkPipelineViewportStateCreateInfo GetViewportState(uint32_t width, uint32_t height);
    VkPipelineRasterizationStateCreateInfo GetRasterInfo(bool bWireframe = false);
    VkPipelineMultisampleStateCreateInfo GetMultisampleState();
    VkPipelineColorBlendStateCreateInfo GetBlendState();

};
