#pragma once

#include "RenderPass.h"

namespace Muyo
{
class RenderPassGBufferMeshShader : public RenderPass
{
public:
    explicit RenderPassGBufferMeshShader(const VkExtent2D& renderArea)
        : m_renderArea(renderArea)
    {
    }
    ~RenderPassGBufferMeshShader() override;
    void PrepareRenderPass() override;
    void CreatePipeline() override;
    void RecordCommandBuffers();
    VkCommandBuffer GetCommandBuffer() const override
    {
        return m_commandBuffer;
    }

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {};
};
}  // namespace Muyo
