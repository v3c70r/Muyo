#include "ShadowPassManager.h"

#include <algorithm>

#include "LightSceneNode.h"

namespace Muyo
{

void ShadowPassManager::SetLights(const DrawList& lightList)
{
    for (size_t i = 0; i < lightList.size(); ++i)
    {
        const SpotLightNode* pLight = dynamic_cast<const SpotLightNode*>(lightList[i]);
        if (pLight)
        {
            // Shadow map index is set when gethring the light from draw list.
            m_vpShadowPasses.emplace_back(std::make_unique<RenderPassRSM>(pLight->GetName(), VkExtent2D{1024, 1024}, static_cast<uint32_t>(i)));
        }
    }
}

void ShadowPassManager::PrepareRenderPasses()
{
    std::for_each(m_vpShadowPasses.begin(), m_vpShadowPasses.end(), [](auto& pShadowPass)
                  { pShadowPass->PrepareRenderPass(); });
}

void ShadowPassManager::RecordCommandBuffers(const std::vector<const SceneNode*>& vpGeometryNodes)
{
    std::for_each(m_vpShadowPasses.begin(), m_vpShadowPasses.end(), [&vpGeometryNodes](auto& pShadowPass)
                  { pShadowPass->RecordCommandBuffers(vpGeometryNodes); });
}

std::vector<VkCommandBuffer> ShadowPassManager::GetCommandBuffers() const
{
    std::vector<VkCommandBuffer> vCommandBuffers;
    std::for_each(m_vpShadowPasses.begin(), m_vpShadowPasses.end(), [&vCommandBuffers](const auto& pShadowPass)
                  { vCommandBuffers.push_back(pShadowPass->GetCommandBuffer()); });
    return vCommandBuffers;
}

std::vector<RSMResources> ShadowPassManager::GetShadowMaps() const
{
    std::vector<RSMResources> vpShadowMaps;

    std::for_each(m_vpShadowPasses.begin(), m_vpShadowPasses.end(), [&vpShadowMaps](const std::unique_ptr<RenderPassRSM>& pShadowPass)
                  { vpShadowMaps.push_back(pShadowPass->GetRSM()); });
    return vpShadowMaps;
}

}  // namespace Muyo
