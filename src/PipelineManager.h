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
    static constexpr size_t MAX_NUM_ATTACHMENTS = 4;
    std::array<VkPipelineColorBlendAttachmentState, MAX_NUM_ATTACHMENTS> m_aBlendModes;
    VkViewport mViewport = {};
    VkRect2D mScissorRect = {};
    void InitializeDefaultBlendStats();

    PipelineManager();
    ~PipelineManager();

    void CreateStaticObjectPipeline(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, RenderPass& pass);
    void DestroyStaticObjectPipeline();

    void CreateGBufferPipeline(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, RenderPass& pass);
    void DestroyGBufferPipeline();

    VkPipeline GetStaticObjectPipeline()
    {
        return maPipelines[PIPELINE_TYPE_STATIC_OBJECT];
    }
    VkPipelineLayout GetStaticObjectPipelineLayout()
    {
        return maPipelineLayouts[PIPELINE_TYPE_STATIC_OBJECT];
    }

    VkPipeline GetGBufferPipeline()
    {
        return maPipelines[PIPELINE_TYPE_GBUFFER];
    }
    VkPipelineLayout GetGBufferPipelineLayout()
    {
        return maPipelineLayouts[PIPELINE_TYPE_GBUFFER];
    }

    enum PipelineType
    {
        PIPELINE_TYPE_STATIC_OBJECT,
        PIPELINE_TYPE_GBUFFER,
        PIPELINE_TYPE_COUNT
    };
private:
    std::unordered_map<std::string, VkPipeline> m_vPipelines;
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
    std::vector<char> ReadSpv(const std::string& fileName);

    VkPipelineInputAssemblyStateCreateInfo GetIAInfo(
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        bool bEnablePrimtiveStart = false);

    VkPipelineViewportStateCreateInfo GetViewportState(uint32_t width, uint32_t height);
    VkPipelineRasterizationStateCreateInfo GetRasterInfo(bool bWireframe = false);
    VkPipelineMultisampleStateCreateInfo GetMultisampleState();
    VkPipelineColorBlendStateCreateInfo GetBlendState(size_t numAttachments);
    VkPipelineDepthStencilStateCreateInfo GetDepthStencilCreateinfo();

    VkPipelineLayout CreatePipelineLayout(VkDescriptorSetLayout descriptorLayout);

    std ::array<VkPipeline, PIPELINE_TYPE_COUNT> maPipelines = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std ::array<VkPipelineLayout, PIPELINE_TYPE_COUNT> maPipelineLayouts = {VK_NULL_HANDLE};
};
