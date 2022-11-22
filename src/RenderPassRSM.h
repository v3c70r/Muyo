#pragma once

#include "RenderPass.h"

class RenderTarget;
struct RSMResources
{
    const RenderTarget* pDepth;
    const RenderTarget* pNormal;
    const RenderTarget* pPosition;
    const RenderTarget* pFlux;
};
class RenderPassRSM : public RenderPass
{
public:
    RenderPassRSM(const std::string& sCasterName, VkExtent2D shadowMapSize, uint32_t nLightIndex) : m_shadowMapSize(shadowMapSize), m_shadowCasterName(sCasterName), m_nLightIndex(nLightIndex), m_aRSMNames(
    {
        sCasterName + "_depth",
        sCasterName + "_normal",
        sCasterName + "_position",
        sCasterName + "_flux",
    }){}
    ~RenderPassRSM() override;
    virtual void CreatePipeline() override;
    virtual void PrepareRenderPass() override;
    void RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries);
    VkCommandBuffer GetCommandBuffer() const override  {return m_commandBuffer;}

    RSMResources GetRSM();

private:
    struct PushConstant
    {
        uint32_t nLightIndex;
        uint32_t nRSMSize;
    };

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkExtent2D m_shadowMapSize = {0, 0};
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    const std::string m_shadowCasterName = "RSM";
    uint32_t m_nLightIndex = 0;

    // Store shadow map names
    enum RSMNameIdx
    {
        SHADOW_MAP_DEPTH,
        SHADOW_MAP_NORMAL,
        SHADOW_MAP_POSITION,
        SHADOW_MAP_FLUX
    };
    const std::array<std::string, 4> m_aRSMNames;
};
