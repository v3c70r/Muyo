#pragma once
#include <memory>

#include "Geometry.h"
#include "RenderPass.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "Camera.h"

class RenderLayerIBL : public RenderPass
{
    struct IBLPassResource
    {
    };
    // Generate convoluted irradiance map
public:
    RenderLayerIBL();
    ~RenderLayerIBL();
    void DestroyFramebuffer();
    void ReloadEnvironmentMap(const std::string& sNewEnvMapPath);

    VkCommandBuffer GetCommandBuffer(size_t idx) const override
    {
        return m_commandBuffer;
    }

private:    // Methods
    void setupRenderPass();
    void setupFramebuffer();
    void CreatePipeline() override;
    void setupDescriptorSets();
    void RecordCommandBuffer();

private:
    const uint32_t ENV_CUBE_DIM = 128;
    const uint32_t IRR_CUBE_DIM = 32;
    const uint32_t PREFILTERED_CUBE_DIM = 512;
    const uint32_t SPECULAR_BRDF_LUT_DIM = 512;
    const uint32_t NUM_FACES = 6;
    const uint32_t NUM_IRR_CUBEMAP_MIP = (uint32_t)std::floor(std::log2(ENV_CUBE_DIM))+1;
    const uint32_t NUM_PREFILTERED_CUBEMAP_MIP = (uint32_t)std::floor(std::log2(PREFILTERED_CUBE_DIM))+1;
    const VkFormat TEX_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkExtent2D m_renderArea = {0, 0};
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    std::unique_ptr<Geometry> m_pSkybox = nullptr;

    Texture m_texEnvCupMap;

    VkPipeline m_envCubeMapPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_envCubeMapPipelineLayout = VK_NULL_HANDLE;

    VkPipeline m_irrCubeMapPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_irrCubeMapPipelineLayout = VK_NULL_HANDLE;

    VkPipeline m_prefilteredCubemapPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_prefilteredCubemapPipelineLayout = VK_NULL_HANDLE;

    VkPipeline m_specularBrdfLutPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_specularBrdfLutPipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSet m_perViewDataDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_envMapDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_irrMapDescriptorSet = VK_NULL_HANDLE;

    std::array<VkFramebuffer, 2> m_frameBuffers = {VK_NULL_HANDLE, VK_NULL_HANDLE} ;

    UniformBuffer<PerViewData> m_uniformBuffer;

    // Render pass constants
    enum RenderPasses
    {
        RENDERPASS_LOAD_ENV_MAP = 0,
        RENDERPASS_COMPUTE_IRR_CUBEMAP,
        RENDERPASS_COMPUTE_PRE_FILTERED_CUBEMAP,
        RENDERPASS_COMPUTE_SPECULAR_BRDF_LUT,
        RENDERPASS_COUNT
    };

    std::array<VkFramebuffer, RENDERPASS_COUNT> m_aFramebuffers = {
        VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};

    const std::array<uint32_t, RENDERPASS_COUNT> m_aCubemapSizes = {
        ENV_CUBE_DIM,
        IRR_CUBE_DIM,
        PREFILTERED_CUBE_DIM,
        SPECULAR_BRDF_LUT_DIM
    };

    const std::array<std::string, RENDERPASS_COUNT> m_aRenderPassNames = {
        "Load cubemap", "Compute irradiance cubemap", "Compute prefiltered cubmap", "Compute specular brdf lut"};
};

