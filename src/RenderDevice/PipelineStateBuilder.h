#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <cassert>
#include <fstream>
#include <string>
#include <vector>

#include "VkRenderDevice.h"

namespace Muyo
{
// Fluent builder
template <typename T>
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
        m_inputAssemblyInfo = assemblyInfo;
        return *this;
    }
    PipelineStateBuilder& setViewport(const VkViewport& viewport,
                                      const VkRect2D& scissor);
    PipelineStateBuilder& setRasterizer(
        const VkPipelineRasterizationStateCreateInfo& rasterizerInfo)
    {
        m_rasterizerInfo = rasterizerInfo;
        return *this;
    }
    PipelineStateBuilder& setMSAA(
        const VkPipelineMultisampleStateCreateInfo& multisamplingInfo)
    {
        m_multisamplingInfo = multisamplingInfo;
        return *this;
    }
    PipelineStateBuilder& setColorBlending(
        const VkPipelineColorBlendStateCreateInfo& colorBlendState)
    {
        m_colorBlendStateInfo = colorBlendState;
        return *this;
    }
    PipelineStateBuilder& setDynamicStates(
        const std::vector<VkDynamicState>& dynamicStates)
    {
        m_dynamicStatesInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        m_dynamicStatesInfo.dynamicStateCount =
            static_cast<uint32_t>(dynamicStates.size());
        m_dynamicStatesInfo.pDynamicStates = dynamicStates.data();
        return *this;
    }

    PipelineStateBuilder& setPipelineLayout(VkPipelineLayout& pipelineLayout)
    {
        m_pipelineLayout = pipelineLayout;
        return *this;
    }

    PipelineStateBuilder& setDepthStencil(
        const VkPipelineDepthStencilStateCreateInfo& depthStencil)
    {
        m_depthStencilInfo = depthStencil;
        return *this;
    }
    PipelineStateBuilder& setRenderPass(VkRenderPass renderPass)
    {
        mRenderPass = renderPass;
        return *this;
    }
    PipelineStateBuilder& setSubpassIndex(uint32_t subpassIndex)
    {
        mSubpassIndex = subpassIndex;
        return *this;
    }

    VkPipeline Build(VkDevice device);

private:
    std::vector<VkPipelineShaderStageCreateInfo> m_vShaderStageInfos;
    VkPipelineVertexInputStateCreateInfo m_vertexInputInfo = {};
    VkPipelineInputAssemblyStateCreateInfo m_inputAssemblyInfo = {};

    VkPipelineViewportStateCreateInfo m_viewPortState = {};
    VkPipelineRasterizationStateCreateInfo m_rasterizerInfo = {};

    VkPipelineMultisampleStateCreateInfo m_multisamplingInfo = {};
    VkPipelineColorBlendStateCreateInfo m_colorBlendStateInfo = {};
    VkPipelineLayout m_pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo m_depthStencilInfo = {};
    VkPipelineDynamicStateCreateInfo m_dynamicStatesInfo = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 0, nullptr};
    VkRenderPass mRenderPass = {};
    uint32_t mSubpassIndex = 0;
};

// TODO: Finish other builders
// Builders should contains resonable default values, so it can be
// used without specifying default values
// Should I use static builder or non static builder?
// Viewport state builder
// Raster
template <class T>
class IBuilder
{
public:
    virtual T Build() const = 0;
};

template <class T>
class InfoBuilder : public IBuilder<T>
{
public:
    T Build() const override { return m_info; }

protected:
    T m_info = {};
};

class ComputePipelineBuilder : public InfoBuilder<VkComputePipelineCreateInfo>
{
public:
    ComputePipelineBuilder()
    {
        m_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        m_info.pNext = nullptr;
    }
    ComputePipelineBuilder& SetPipelineLayout(VkPipelineLayout layout)
    {
        m_info.layout = layout;
        return *this;
    }
    ComputePipelineBuilder& AddShaderModule(const VkShaderModule& shaderModule, VkShaderStageFlagBits shaderStage = VK_SHADER_STAGE_COMPUTE_BIT);

private:
    VkPipelineShaderStageCreateInfo m_shaderStageInfo = {};
};

class RayTracingPipelineBuilder : public InfoBuilder<VkRayTracingPipelineCreateInfoKHR>
{
public:
    RayTracingPipelineBuilder()
    {
        m_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        m_info.pNext = nullptr;
        m_info.basePipelineHandle = nullptr;
        m_info.basePipelineIndex = 0;
    }
    RayTracingPipelineBuilder& AddShaderModule(const VkShaderModule& shaderModule, VkShaderStageFlagBits shaderStage);
    RayTracingPipelineBuilder& SetMaxRecursionDepth(uint32_t nDepth)
    {
        m_info.maxPipelineRayRecursionDepth = nDepth;
        return *this;
    }
    RayTracingPipelineBuilder& SetPipelineLayout(VkPipelineLayout layout)
    {
        m_info.layout = layout;
        return *this;
    }

    std::vector<VkPipelineShaderStageCreateInfo> GetShaderStageInfos() const
    {
        return m_vShaderStageInfos;
    }
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> GetShaderGroupInfos() const
    {
        return m_vRTShaderGroupInfos;
    }

private:
    std::vector<VkPipelineShaderStageCreateInfo> m_vShaderStageInfos;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_vRTShaderGroupInfos;
};

class ViewportBuilder : public InfoBuilder<VkViewport>
{
public:
    ViewportBuilder()
    {
        m_info.x = 0.0;
        m_info.y = 0.0;
        m_info.width = 0.0;
        m_info.height = 0.0;
        m_info.minDepth = 0.0;
        m_info.maxDepth = 1.0;
    }
    ViewportBuilder& setXY(float x, float y)
    {
        m_info.x = x;
        m_info.y = y;
        return *this;
    }
    ViewportBuilder& setWH(float w, float h)
    {
        m_info.width = w;
        m_info.height = h;
        return *this;
    }

    ViewportBuilder& setWH(VkExtent2D wh)
    {
        m_info.width = (float)wh.width;
        m_info.height = (float)wh.height;
        return *this;
    }

    ViewportBuilder& setMinMaxDepth(float minDepth, float maxDepth)
    {
        m_info.minDepth = minDepth;
        m_info.maxDepth = maxDepth;
        return *this;
    }
};

class RasterizationStateCIBuilder
    : public InfoBuilder<VkPipelineRasterizationStateCreateInfo>
{
public:
    RasterizationStateCIBuilder()
    {
        m_info.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        m_info.depthClampEnable = VK_FALSE;
        m_info.rasterizerDiscardEnable = VK_FALSE;
        m_info.polygonMode = VK_POLYGON_MODE_FILL;
        m_info.lineWidth = 1.0f;
        m_info.cullMode = VK_CULL_MODE_BACK_BIT;
        m_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
        m_info.depthBiasEnable = VK_FALSE;
        m_info.depthBiasConstantFactor = 0.0f;  // Optional
        m_info.depthBiasClamp = 0.0f;           // Optional
        m_info.depthBiasSlopeFactor = 0.0f;     // Optional
    }
    RasterizationStateCIBuilder& SetDepthClampEnable(bool isEnabled)
    {
        m_info.depthClampEnable = isEnabled;
        return *this;
    }
    RasterizationStateCIBuilder& SetPolygonMode(VkPolygonMode polygonMode)
    {
        m_info.polygonMode = polygonMode;
        return *this;
    }

    RasterizationStateCIBuilder& SetFrontFace(VkFrontFace frontFace)
    {
        m_info.frontFace = frontFace;
        return *this;
    }
    RasterizationStateCIBuilder& SetCullMode(VkCullModeFlags uCullMode)
    {
        m_info.cullMode = uCullMode;
        return *this;
    }
    // TODO: Add more builders if necessary
};

class MultisampleStateCIBuilder
    : public InfoBuilder<VkPipelineMultisampleStateCreateInfo>
{
public:
    MultisampleStateCIBuilder()
    {
        m_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        m_info.sampleShadingEnable = VK_FALSE;
        m_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        m_info.minSampleShading = 1.0f;
        m_info.pSampleMask = nullptr;
        m_info.alphaToCoverageEnable = VK_FALSE;
        m_info.alphaToOneEnable = VK_FALSE;
    }
    // TODO: Create setters when necesssary
};

class BlendStateCIBuilder
    : public InfoBuilder<VkPipelineColorBlendStateCreateInfo>
{
public:
    BlendStateCIBuilder()
    {
        m_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        m_info.logicOpEnable = VK_FALSE;
        m_info.logicOp = VK_LOGIC_OP_COPY;  // Optional
        m_info.attachmentCount = 0;
        m_info.pAttachments = nullptr;
        m_info.blendConstants[0] = 0.0f;  // Optional
        m_info.blendConstants[1] = 0.0f;  // Optional
        m_info.blendConstants[2] = 0.0f;  // Optional
        m_info.blendConstants[3] = 0.0f;  // Optional
    }
    BlendStateCIBuilder& setAttachments(uint32_t numAttachments, bool bEnabled = true)
    {
        blendAttachmentStates.resize(numAttachments, getAttachmentBlendState(bEnabled));
        m_info.attachmentCount = numAttachments;
        m_info.pAttachments = blendAttachmentStates.data();
        return *this;
    }

private:
    static VkPipelineColorBlendAttachmentState getAttachmentBlendState(bool bEnabled)
    {
        VkPipelineColorBlendAttachmentState defaultBlendState = {};
        if (bEnabled)
        {
            defaultBlendState.blendEnable = VK_TRUE;
            defaultBlendState.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            defaultBlendState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            defaultBlendState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            defaultBlendState.colorBlendOp = VK_BLEND_OP_ADD;
            defaultBlendState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            defaultBlendState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            defaultBlendState.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        else
        {
            defaultBlendState.blendEnable = VK_FALSE;
            defaultBlendState.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
        return defaultBlendState;
    }
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates;
};

class InputAssemblyStateCIBuilder
    : public InfoBuilder<VkPipelineInputAssemblyStateCreateInfo>
{
public:
    InputAssemblyStateCIBuilder()
    {
        m_info.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        m_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        m_info.primitiveRestartEnable = false;
    }
    InputAssemblyStateCIBuilder& setTopology(VkPrimitiveTopology topology)
    {
        m_info.topology = topology;
        return *this;
    }
    InputAssemblyStateCIBuilder& setPrimitiveRestartEnabled(bool isEnabled)
    {
        m_info.primitiveRestartEnable = isEnabled;
        return *this;
    }
};

class DepthStencilCIBuilder
    : public InfoBuilder<VkPipelineDepthStencilStateCreateInfo>
{
public:
    DepthStencilCIBuilder()
    {
        m_info.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        m_info.depthTestEnable = true;
        m_info.depthWriteEnable = true;
        m_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        // depth clamp?
        m_info.depthBoundsTestEnable = VK_FALSE;
        m_info.front = {};
        m_info.back = {};
    }
    DepthStencilCIBuilder& setDepthTestEnabled(bool isEnabled)
    {
        m_info.depthTestEnable = isEnabled;
        return *this;
    }
    DepthStencilCIBuilder& setDepthWriteEnabled(bool isEnabled)
    {
        m_info.depthWriteEnable = isEnabled;
        return *this;
    }
    DepthStencilCIBuilder& setDepthCompareOp(VkCompareOp compareOp)
    {
        m_info.depthCompareOp = compareOp;
        return *this;
    }
};

class RenderPassBeginInfoBuilder : public InfoBuilder<VkRenderPassBeginInfo>
{
public:
    RenderPassBeginInfoBuilder()
    {
        m_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    }
    RenderPassBeginInfoBuilder& setRenderPass(VkRenderPass renderPass)
    {
        m_info.renderPass = renderPass;
        return *this;
    }
    RenderPassBeginInfoBuilder& setRenderArea(VkRect2D renderArea)
    {
        m_info.renderArea = renderArea;
        return *this;
    }
    RenderPassBeginInfoBuilder& setRenderArea(VkExtent2D WH)
    {
        m_info.renderArea.offset.x = 0;
        m_info.renderArea.offset.y = 0;
        m_info.renderArea.extent = WH;
        return *this;
    }
    RenderPassBeginInfoBuilder& setClearValues(
        const std::vector<VkClearValue>& values)
    {
        m_info.clearValueCount = static_cast<uint32_t>(values.size());
        m_info.pClearValues = values.data();
        return *this;
    }
    RenderPassBeginInfoBuilder& setFramebuffer(VkFramebuffer fb)
    {
        m_info.framebuffer = fb;
        return *this;
    }
};

VkShaderModule CreateShaderModule(const std::vector<char>& code);
std::vector<char> ReadSpv(const std::string& fileName);

}  // namespace Muyo
