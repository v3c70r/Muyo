#pragma once
#include "VkRenderDevice.h"

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <array>


class RenderPass;
// Trying to manage all of the pipelines here
// Should be able to cache pipelines
class PipelineManager
{
public:
    enum BlendMode
    {
        eBLENDMODE_DEFAULT = 0,
        eBLENDMODE_COUNT
    };
    std::array<VkPipelineColorBlendAttachmentState, eBLENDMODE_COUNT> aBlendModes;
    VkViewport mViewport = {};
    VkRect2D mScissorRect = {};
    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;

    PipelineManager();
    ~PipelineManager();

    void CreateStaticObjectPipeline(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, RenderPass& pass);
    void DestroyStaticObjectPipeline();

    void CreateGBufferPipeline(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, RenderPass& pass);
    void DestroyGBufferPipeline();

    VkPipeline GetStaticObjectPipeline() {return maPipelines[0];}
    VkPipelineLayout GetStaticObjectPipelineLayout() { return maPipelineLayouts[0];}


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
    VkPipelineDepthStencilStateCreateInfo GetDepthStencilCreateinfo();

    void InitilaizePipelineLayout();

    std ::array<VkPipeline, 2> maPipelines = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std ::array<VkPipelineLayout, 1> maPipelineLayouts = {VK_NULL_HANDLE};
};
