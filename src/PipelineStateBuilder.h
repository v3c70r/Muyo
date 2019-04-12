#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cassert>

// Fluent builder
class VertexBuffer;

class PipelineStateBuilder
{
public:
    PipelineStateBuilder& setShaderModules(
        const std::vector<VkShaderModule>& shaderModules);
    PipelineStateBuilder& setVertextInfo(
        const std::vector<VkVertexInputBindingDescription>& bindingDescriptions,
        const std::vector<VkVertexInputAttributeDescription>&
            attribDescriptions);
    PipelineStateBuilder& setAssembly(
        const VkPipelineInputAssemblyStateCreateInfo& assemblyInfo)
    {
        mInputAssemblyInfo = assemblyInfo;
        return *this;
    }
    PipelineStateBuilder& setViewport(const VkViewport& viewport,
                                      const VkRect2D& scissor);
    PipelineStateBuilder& setRasterizer(const VkPipelineRasterizationStateCreateInfo& rasterizerInfo)
    {
        mRasterizerInfo = rasterizerInfo;
        return *this;
    }
    PipelineStateBuilder& setMSAA(
        const VkPipelineMultisampleStateCreateInfo& multisamplingInfo)
    {
        mMultisamplingInfo = multisamplingInfo;
        return *this;
    }
    PipelineStateBuilder& setColorBlending(const VkPipelineColorBlendStateCreateInfo& colorBlendState)
    {

        mColorBlendStateInfo = colorBlendState;
        return *this;
    }
    PipelineStateBuilder& setDynamicStates(const std::vector<VkDynamicState>& dynamicStates)
    {
        mDynamicStatesInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        mDynamicStatesInfo.dynamicStateCount = dynamicStates.size();
        mDynamicStatesInfo.pDynamicStates = dynamicStates.data();
        return *this;
    }
    PipelineStateBuilder& setPipelineLayout(
        VkDevice device,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
        const std::vector<VkPushConstantRange>& pushConstants = std::vector<VkPushConstantRange>())
    {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = descriptorSetLayouts.size();
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = pushConstants.size();
        pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();
        assert(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                      &mPipelineLayout) == VK_SUCCESS);

        return *this;
    }
    PipelineStateBuilder& setDepthStencil(const VkPipelineDepthStencilStateCreateInfo& depthStencil)
    {
        mDepthStencilInfo = depthStencil;
        return *this;
    }
    PipelineStateBuilder& setRenderPass(VkRenderPass renderPass)
    {
        mRenderPass = renderPass;
        return *this;
    }
    VkPipeline build(VkDevice device);
private:
    std::vector<VkPipelineShaderStageCreateInfo> mShaderStagesInfo;
    VkPipelineVertexInputStateCreateInfo mVertexInputInfo = {};
    VkPipelineInputAssemblyStateCreateInfo mInputAssemblyInfo = {};

    VkPipelineViewportStateCreateInfo mViewportState = {};
    VkPipelineRasterizationStateCreateInfo mRasterizerInfo = {};

    VkPipelineMultisampleStateCreateInfo mMultisamplingInfo = {};
    VkPipelineColorBlendStateCreateInfo mColorBlendStateInfo = {};
    VkPipelineLayout mPipelineLayout;
    VkPipelineDepthStencilStateCreateInfo mDepthStencilInfo = {};
    VkPipelineDynamicStateCreateInfo mDynamicStatesInfo = {};
    VkRenderPass mRenderPass = {};

};
