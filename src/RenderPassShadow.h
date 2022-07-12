#pragma once

#include "RenderPass.h"

class RenderPassShadow : public IRenderPass
{
public:
    RenderPassShadow(VkExtent2D shadowMapSize) : m_shadowMapSize(shadowMapSize) {}
    ~RenderPassShadow() override;
    virtual void CreatePipeline() override;
    virtual void PrepareRenderPass() override;

private:
    VkFramebuffer m_frameBuffer = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkExtent2D m_shadowMapSize;
    RenderPassParameters m_renderPassParameters;
};
