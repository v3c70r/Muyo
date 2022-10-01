#pragma once

#include "Scene.h"
#include "UniformBuffer.h"
#include "RenderPassShadow.h"

class RenderPassShadow;
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
    std::vector<ShadowMapResources> GetShadowMaps();
private:
    std::vector<std::unique_ptr<RenderPassShadow>> m_vpShadowPasses;
};
