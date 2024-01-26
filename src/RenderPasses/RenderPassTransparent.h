#pragma once

#include "RenderPass.h"

namespace Muyo
{
class RenderPassTransparent : public RenderPass
{
public:
    RenderPassTransparent(VkExtent2D renderArea);
    virtual ~RenderPassTransparent() override;

    virtual void PrepareRenderPass() override;
    virtual void CreatePipeline() override;

    VkCommandBuffer GetCommandBuffer() const override { return m_commandBuffer; }
    void RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries);
    void DestroyPipeline();
    void CreateFramebuffer(uint32_t uWidth, uint32_t uHeight);
    void DestroyFramebuffer();

private:
    void CreateRenderPasses();
    VkFramebuffer m_frameBuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {0, 0};
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    // TODO: move these to renderpass parameter
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};

}  // namespace Muyo
