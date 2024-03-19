#pragma once

#include "RenderPass.h"
#include "Scene.h"

namespace Muyo
{
class RenderPassTransparent : public RenderPass
{
public:
    explicit RenderPassTransparent(VkExtent2D renderArea);
    ~RenderPassTransparent() override;

    void PrepareRenderPass() override;
    void CreatePipeline() override;

    VkCommandBuffer GetCommandBuffer() const override { return m_commandBuffer; }
    void RecordCommandBuffers(const std::vector<const SceneNode*>& vpGeometryNodes);

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {0, 0};
};

}  // namespace Muyo
