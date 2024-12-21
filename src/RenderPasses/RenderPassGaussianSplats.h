#pragma once
#include "RenderPass.h"
#include "GaussianSplattingSceneNode.h"
namespace Muyo
{
class RenderPassGaussianSplats : public RenderPass
{
public:
    explicit RenderPassGaussianSplats(const VkExtent2D& renderArea) : m_renderArea(renderArea) {}
    ~RenderPassGaussianSplats() override;
    void SetGaussainSplatsSceneNode(const GaussianSplatsSceneNode* pGaussianSplatsSceneNode)
    {
        m_pGaussianSplatsSceneNode = pGaussianSplatsSceneNode;
    }
    void PrepareRenderPass() override;
    void CreatePipeline() override;
    void RecordCommandBuffers();
    VkCommandBuffer GetCommandBuffer() const override { return m_commandBuffer; }

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {};
    const GaussianSplatsSceneNode* m_pGaussianSplatsSceneNode;
};
}  // namespace Muyo
