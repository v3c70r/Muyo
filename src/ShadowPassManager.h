#pragma once

#include "Scene.h"
#include "UniformBuffer.h"
#include "RenderPassRSM.h"

class RenderPassRSM;
class RenderTarget;
class ShadowPassManager
{
public:
    ShadowPassManager() = default;
    ShadowPassManager(const ShadowPassManager&) = delete;
    void SetLights(const DrawList& lightList);
    void PrepareRenderPasses();
    void RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries);
    std::vector<VkCommandBuffer> GetCommandBuffers() const;
    std::vector<RSMResources> GetShadowMaps();
private:
    std::vector<std::unique_ptr<RenderPassRSM>> m_vpShadowPasses;
};
