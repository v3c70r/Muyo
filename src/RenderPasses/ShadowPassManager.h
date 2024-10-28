#pragma once

#include "RenderPassRSM.h"
#include "Scene.h"
#include "UniformBuffer.h"

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

private:
    std::vector<std::unique_ptr<RenderPassRSM>> m_vpShadowPasses;
};
}  // namespace Muyo
