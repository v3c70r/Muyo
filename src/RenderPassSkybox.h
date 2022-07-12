#pragma once
#include "RenderPass.h"
#include "RenderPassParameters.h"

class RenderPassSkybox : public IRenderPass
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
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    RenderPassParameters m_renderPassParameters;

};
