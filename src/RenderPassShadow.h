#pragma once

#include "RenderPass.h"

class RenderTarget;
class RenderPassShadow : public RenderPass
{
public:
    RenderPassShadow(const std::string& sCasterName, VkExtent2D shadowMapSize, uint32_t nLightIndex) : m_shadowMapSize(shadowMapSize), m_shadowCasterName(sCasterName), m_nLightIndex(nLightIndex) {}
    ~RenderPassShadow() override;
    virtual void CreatePipeline() override;
    virtual void PrepareRenderPass() override;
    void RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries);
    VkCommandBuffer GetCommandBuffer() const override  {return m_commandBuffer;}

    RenderTarget* GetShadowMap();

private:
    struct PushConstant
    {
        uint32_t nLightIndex;
        uint32_t nShadowMapSize;
    };

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkExtent2D m_shadowMapSize = {0, 0};
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    std::string m_shadowCasterName = "ShadowMap";
    uint32_t m_nLightIndex = 0;
};
