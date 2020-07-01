#pragma once
#include "VkRenderDevice.h"

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <array>


// Trying to manage all of the pipelines here
// Should be able to cache pipelines
class PipelineManager
{
public:
    static constexpr size_t MAX_NUM_ATTACHMENTS = 4;
    std::array<VkPipelineColorBlendAttachmentState, MAX_NUM_ATTACHMENTS> m_aBlendModes;
    //VkViewport mViewport = {};
    //VkRect2D mScissorRect = {};
    void InitializeDefaultBlendStats();

    PipelineManager();
    ~PipelineManager();

    void CreateStaticObjectPipeline(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, VkRenderPass pass);
    void DestroyStaticObjectPipeline();

    void CreateGBufferPipeline(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, VkRenderPass pass);
    void DestroyGBufferPipeline();

    void CreateImGuiPipeline(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, VkRenderPass pass);
    void DestroyImGuiPipeline();

// IBL pipelines
    void CreateIBLPipelines(uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout, VkRenderPass pass);
    void DestroyIBLPipelines();

    VkPipeline GetStaticObjectPipeline() const
    {
        return maPipelines[PIPELINE_TYPE_STATIC_OBJECT];
    }
    VkPipelineLayout GetStaticObjectPipelineLayout() const
    {
        return maPipelineLayouts[PIPELINE_TYPE_STATIC_OBJECT];
    }

    VkPipeline GetGBufferPipeline() const
    {
        return maPipelines[PIPELINE_TYPE_GBUFFER];
    }
    VkPipelineLayout GetGBufferPipelineLayout() const
    {
        return maPipelineLayouts[PIPELINE_TYPE_GBUFFER];
    }

    VkPipeline GetImGuiPipeline() const
    {
        return maPipelines[PIPELINE_TYPE_IMGUI];
    }

    VkPipelineLayout GetImGuiPipelineLayout() const
    {
        return maPipelineLayouts[PIPELINE_TYPE_IMGUI];
    }

    VkPipeline GetIBLIrradiancePipeline() const
    {
        return maPipelines[PIPELINE_TYPE_IBL_IRRADIANCE];
    }

    VkPipelineLayout GetIBLIrradiancePipelineLayout() const
    {
        return maPipelineLayouts[PIPELINE_TYPE_IBL_IRRADIANCE];
    }

    enum PipelineType
    {
        PIPELINE_TYPE_STATIC_OBJECT,
        PIPELINE_TYPE_GBUFFER,
        PIPELINE_TYPE_IMGUI,
        PIPELINE_TYPE_IBL_BRDF_LUT,     // Generate speclular lookup lut
        PIPELINE_TYPE_IBL_IRRADIANCE,   // generate irradiance map
        PIPELINE_TYPE_IBL_PF_ENV,       // generate pre-filtered environment map
        PIPELINE_TYPE_COUNT
    };
    static VkPipelineLayout CreatePipelineLayout(
        VkDescriptorSetLayout descriptorLayout,
        VkPushConstantRange pushConstantRange = {0, 0, 0});

    static VkPipelineLayout CreatePipelineLayout(
        const std::vector<VkDescriptorSetLayout> &descriptorLayouts,
        const std::vector<VkPushConstantRange>& pushConstantRanges);

        //VkPushConstantRange pushConstantRange = {0, 0, 0});

private:
    std::unordered_map<std::string, VkPipeline> m_vPipelines;

    VkPipelineInputAssemblyStateCreateInfo GetIAInfo(
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        bool bEnablePrimtiveStart = false);

    VkPipelineRasterizationStateCreateInfo GetRasterInfo(bool bWireframe = false);
    VkPipelineMultisampleStateCreateInfo GetMultisampleState();
    VkPipelineColorBlendStateCreateInfo GetBlendState(size_t numAttachments);
    VkPipelineDepthStencilStateCreateInfo GetDepthStencilCreateinfo(
        VkBool32 bEnableDepthTest = VK_TRUE, VkBool32 bEnableStencilTest = VK_TRUE,
        VkCompareOp compareOp = VK_COMPARE_OP_LESS);


    std ::array<VkPipeline, PIPELINE_TYPE_COUNT> maPipelines = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std ::array<VkPipelineLayout, PIPELINE_TYPE_COUNT> maPipelineLayouts = {VK_NULL_HANDLE};
};

// Singletone 
PipelineManager* GetPipelineManager();
