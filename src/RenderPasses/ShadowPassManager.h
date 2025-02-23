#pragma once

#include "RenderPassRSM.h"
#include "Scene.h"

namespace Muyo
{
class RenderPassRSM;
class RenderTarget;
class ShadowPassManager
{
public:
    ShadowPassManager() = default;
    ShadowPassManager(const ShadowPassManager&) = delete;
    void SetLights(const DrawList& lightList);
    void PrepareRenderPasses();
    void RecordCommandBuffers(const std::vector<const SceneNode*>& geometryNodes);
    std::vector<VkCommandBuffer> GetCommandBuffers() const;
    std::vector<RSMResources> GetShadowMaps() const;
    const std::vector<std::unique_ptr<RenderPassRSM>>& GetShadowPasses() const { return m_vpShadowPasses; }
    ~ShadowPassManager() = default;
private:
    std::vector<std::unique_ptr<RenderPassRSM>> m_vpShadowPasses;
};
}  // namespace Muyo
