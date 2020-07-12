#pragma once
#include <vulkan/vulkan.h>

#include <cassert>
#include <fstream>
#include <string>
#include <vector>

#include "VkRenderDevice.h"

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
    PipelineStateBuilder& setRasterizer(
        const VkPipelineRasterizationStateCreateInfo& rasterizerInfo)
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
    PipelineStateBuilder& setColorBlending(
        const VkPipelineColorBlendStateCreateInfo& colorBlendState)
    {
        mColorBlendStateInfo = colorBlendState;
        return *this;
    }
    PipelineStateBuilder& setDynamicStates(
        const std::vector<VkDynamicState>& dynamicStates)
    {
        mDynamicStatesInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        mDynamicStatesInfo.dynamicStateCount =
            static_cast<uint32_t>(dynamicStates.size());
        mDynamicStatesInfo.pDynamicStates = dynamicStates.data();
        return *this;
    }

    PipelineStateBuilder& setPipelineLayout(VkPipelineLayout& pipelineLayout)
    {
        mPipelineLayout = pipelineLayout;
        return *this;
    }

    PipelineStateBuilder& setDepthStencil(
        const VkPipelineDepthStencilStateCreateInfo& depthStencil)
    {
        mDepthStencilInfo = depthStencil;
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
    VkPipelineDynamicStateCreateInfo mDynamicStatesInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0};
    VkRenderPass mRenderPass = {};
    uint32_t mSubpassIndex = 0;
};

// TODO: Finish other builders
// Builders should contains resonable default values, so it can be
// used without specifying default values
// Should I use static builder or non static builder?
// Viewport state builder
// Raster
template<class T>
class IBuilder
{
public:
    virtual T build() const = 0;
};

template <class T>
class CIBuilder : public IBuilder<T>
{
public:
    T build() const override { return m_info; }

protected:
    T m_info = {};
};

class ViewportBuilder : public IBuilder<VkViewport>
{
public:
    ViewportBuilder()
    {
        m_viewport.x = 0.0;
        m_viewport.y = 0.0;
        m_viewport.width = 0.0;
        m_viewport.height = 0.0;
        m_viewport.minDepth = 0.0;
        m_viewport.maxDepth = 1.0;
    }
    ViewportBuilder& setXY(float x, float y)
    {
        m_viewport.x = x;
        m_viewport.y = y;
        return *this;
    }
    ViewportBuilder& setWH(float w, float h)
    {
        m_viewport.width = w;
        m_viewport.height = h;
        return *this;
    }

    ViewportBuilder& setMinMaxDepth(float minDepth, float maxDepth)
    {
        m_viewport.minDepth = minDepth;
        m_viewport.maxDepth = maxDepth;
        return *this;
    }
    VkViewport build() const override
    {
        return m_viewport;
    }
private:
    VkViewport m_viewport = {};
};

class RasterizationStateCIBuilder : public CIBuilder<VkPipelineRasterizationStateCreateInfo>
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
    RasterizationStateCIBuilder& setDepthClampEnable(bool isEnabled)
    {
        m_info.depthClampEnable = isEnabled;
        return *this;
    }
    RasterizationStateCIBuilder& setPolygonMode(VkPolygonMode polygonMode)
    {
        m_info.polygonMode = polygonMode;
        return *this;
    }

    RasterizationStateCIBuilder& setFrontFace(VkFrontFace frontFace)
    {
        m_info.frontFace = frontFace;
        return *this;
    }
    // TODO: Add more builders if necessary
};

class MultisampleStateCIBuilder : public CIBuilder<VkPipelineMultisampleStateCreateInfo>
{
public:
    MultisampleStateCIBuilder() {
        m_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        m_info.sampleShadingEnable = VK_FALSE;
        m_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        m_info.minSampleShading = 1.0f;
        m_info.pSampleMask = nullptr;
        m_info.alphaToCoverageEnable = VK_FALSE;
        m_info.alphaToOneEnable = VK_FALSE;
    }
    //TODO: Create setters when necesssary
};

class BlendStateCIBuilder : public CIBuilder<VkPipelineColorBlendStateCreateInfo>
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
    BlendStateCIBuilder& setAttachments(uint32_t numAttachments)
    {
        blendAttachmentStates.resize(numAttachments, getAttachmentBlendState());
        m_info.attachmentCount = numAttachments;
        m_info.pAttachments = blendAttachmentStates.data();
        return *this;
    }
private:
    VkPipelineColorBlendAttachmentState getAttachmentBlendState()
    {
        VkPipelineColorBlendAttachmentState defaultBlendState = {};
        defaultBlendState.blendEnable = VK_TRUE;
        defaultBlendState.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        defaultBlendState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        defaultBlendState.dstColorBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        defaultBlendState.colorBlendOp = VK_BLEND_OP_ADD;
        defaultBlendState.srcAlphaBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        defaultBlendState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        defaultBlendState.alphaBlendOp = VK_BLEND_OP_ADD;
        return defaultBlendState;
    }
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates;
};

class InputAssemblyStateCIBuilder : public CIBuilder<VkPipelineInputAssemblyStateCreateInfo>
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

class DepthStencilCIBuilder : public CIBuilder<VkPipelineDepthStencilStateCreateInfo>
{
public:
    DepthStencilCIBuilder()
    {
        m_info.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        m_info.depthTestEnable = true;
        m_info.depthWriteEnable = true;
        m_info.depthCompareOp = VK_COMPARE_OP_LESS;
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



VkShaderModule CreateShaderModule(const std::vector<char>& code);
std::vector<char> ReadSpv(const std::string& fileName);
