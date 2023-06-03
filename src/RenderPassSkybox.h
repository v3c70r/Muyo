#pragma once
#include "RenderPass.h"
#include "RenderPassParameters.h"

namespace Muyo
{
class RenderPassSkybox : public RenderPass
{
public:
    RenderPassSkybox(VkExtent2D imageSize)
    {
        m_renderPassParameters.SetRenderArea(imageSize);
        PrepareRenderPass();
    }
    void PrepareRenderPass() override;

    RenderPassSkybox();
    ~RenderPassSkybox() override;
    VkCommandBuffer GetCommandBuffer() const override { return m_commandBuffer; }
    void CreatePipeline() override;
    void RecordCommandBuffers();

private:
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};
}  // namespace Muyo
