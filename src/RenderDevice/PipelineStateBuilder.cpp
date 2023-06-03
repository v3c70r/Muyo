#include "PipelineStateBuilder.h"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cassert>

namespace Muyo
{

PipelineStateBuilder& PipelineStateBuilder::setShaderModules(
    const std::vector<VkShaderModule>& shaderModules)
{
    assert(shaderModules.size() == 2);
    m_vShaderStageInfos.reserve(shaderModules.size());

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = shaderModules[0];
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = shaderModules[1];
    fragShaderStageInfo.pName = "main";

    m_vShaderStageInfos = {vertShaderStageInfo, fragShaderStageInfo};
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::setVertextInfo(
    const std::vector<VkVertexInputBindingDescription>& bindingDescriptions,
    const std::vector<VkVertexInputAttributeDescription>& attribDescriptions)
{
    m_vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    m_vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    m_vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    m_vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attribDescriptions.size());
    m_vertexInputInfo.pVertexAttributeDescriptions = attribDescriptions.data();

    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::setViewport(
    const VkViewport& viewport, const VkRect2D& scissor)
{
    m_viewPortState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    m_viewPortState.viewportCount = 1;
    m_viewPortState.pViewports = &viewport;
    m_viewPortState.scissorCount = 1;
    m_viewPortState.pScissors = &scissor;

    return *this;
}

VkPipeline PipelineStateBuilder::Build(VkDevice device)
{
    VkPipeline res = VK_NULL_HANDLE;
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = (uint32_t)m_vShaderStageInfos.size();
    pipelineInfo.pStages = m_vShaderStageInfos.data();

    pipelineInfo.pVertexInputState = &m_vertexInputInfo;

    pipelineInfo.pInputAssemblyState = &m_inputAssemblyInfo;

    pipelineInfo.pViewportState = &m_viewPortState;

    pipelineInfo.pRasterizationState = &m_rasterizerInfo;

    pipelineInfo.pMultisampleState = &m_multisamplingInfo;

    pipelineInfo.pDepthStencilState = &m_depthStencilInfo;

    pipelineInfo.pColorBlendState = &m_colorBlendStateInfo;

    pipelineInfo.pDynamicState = &m_dynamicStatesInfo;

    pipelineInfo.layout = m_pipelineLayout;

    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = mSubpassIndex;

    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                     nullptr,
                                     &res) == VK_SUCCESS);
    return res;
}
// Helper function
ComputePipelineBuilder& ComputePipelineBuilder::AddShaderModule(const VkShaderModule& shaderModule, VkShaderStageFlagBits shaderStage)
{
    // only a single stage for compute pipeline
    assert(shaderStage == VK_SHADER_STAGE_COMPUTE_BIT);

    m_shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_shaderStageInfo.stage = shaderStage;
    m_shaderStageInfo.module = shaderModule;
    m_shaderStageInfo.pName = "main";
    m_info.stage = m_shaderStageInfo;

    return *this;
}
RayTracingPipelineBuilder& RayTracingPipelineBuilder::AddShaderModule(const VkShaderModule& shaderModule, VkShaderStageFlagBits shaderStage)
{
    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = shaderStage;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    uint32_t stageIndex = (uint32_t)m_vRTShaderGroupInfos.size();

    m_vShaderStageInfos.push_back(shaderStageInfo);

    // Add to ray tracing shader group if it's a ray tracing shader
    if (shaderStage &
        (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR))
    {
        VkRayTracingShaderGroupCreateInfoKHR info = {};
        if (shaderStage & (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR))
        {
            info = {
                VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                nullptr,
                VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
                stageIndex,            // generalShader
                VK_SHADER_UNUSED_KHR,  // closestHitShader
                VK_SHADER_UNUSED_KHR,  // anyHitShader
                VK_SHADER_UNUSED_KHR,  // intersectionShader
                nullptr};
        }
        else if (shaderStage & VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
        {
            info = {
                VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                nullptr,
                VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
                VK_SHADER_UNUSED_KHR,  // generalShader
                stageIndex,            // closestHitShader
                VK_SHADER_UNUSED_KHR,  // anyHitShader
                VK_SHADER_UNUSED_KHR,  // intersectionShader
                nullptr};
        }
        else
        {
            assert(false && "Unsupported RT stage");
        }
        m_vRTShaderGroupInfos.push_back(info);
    }
    // Update related fields
    m_info.pGroups = m_vRTShaderGroupInfos.data();
    m_info.groupCount = (uint32_t)m_vRTShaderGroupInfos.size();
    m_info.pStages = m_vShaderStageInfos.data();
    m_info.stageCount = (uint32_t)m_vShaderStageInfos.size();
    return *this;
}

VkShaderModule CreateShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shdrModule;
    assert(vkCreateShaderModule(GetRenderDevice()->GetDevice(), &createInfo,
                                nullptr, &shdrModule) == VK_SUCCESS);
    return shdrModule;
}

std::vector<char> ReadSpv(const std::string& fileName)
{
    std::ifstream file(fileName, std::ios::ate | std::ios::binary);
    assert(file.is_open());
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

}  // namespace Muyo
