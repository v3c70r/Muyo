#pragma once
#include "RenderPass.h"

class RenderPassSkybox : public RenderPass
{
public:
    void SetCubemap();
    RenderPassSkybox();
    ~RenderPassSkybox() override;
    VkCommandBuffer GetCommandBuffer(size_t idx = 0) const override;
    void CreateFramebuffer(uint32_t uWidth, uint32_t uHeight);
    void CreatePipeline();
    void RecordCommandBuffers();

private:
    void CreateRenderPass();
    void DestroyRenderPass();
    void DestroyFramebuffer();
    void DestroyPipeline();
    void SetupDescriptorSets();
    void recordCommandBuffer();
private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {0, 0};
    VkFramebuffer m_frameBuffer = VK_NULL_HANDLE;
};
