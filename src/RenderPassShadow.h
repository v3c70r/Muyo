#pragma once

#include "RenderPass.h"

class RenderPassShadow : public RenderPass
{
public:
    RenderPassShadow(VkExtent2D shadowMapSize) : m_shadowMapSize(shadowMapSize) {}
    ~RenderPassShadow() override;
    virtual void CreatePipeline() override;
    virtual void PrepareRenderPass() override;
    void RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries);
    VkCommandBuffer GetCommandBuffer() const override  {return m_commandBuffer;}

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkExtent2D m_shadowMapSize = {0, 0};
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    std::string m_shadowCasterName = "";
};
