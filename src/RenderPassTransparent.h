#pragma once
#include "RenderPass.h"

class RenderPassTransparent : public RenderPass
{
public:
    RenderPassTransparent();
    virtual ~RenderPassTransparent() override;
    VkCommandBuffer GetCommandBuffer(size_t) const override { return m_vCommandBuffers[0]; }
    void RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries);
    void CreatePipeline();
    void DestroyPipeline();
    void CreateFramebuffer(uint32_t uWidth, uint32_t uHeight);
    void DestroyFramebuffer();
private:
    void CreateRenderPasses();
    VkFramebuffer m_frameBuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {0, 0};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

