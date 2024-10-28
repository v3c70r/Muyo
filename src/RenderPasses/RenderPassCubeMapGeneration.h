#pragma once
#include "RenderPass.h"

namespace Muyo
{
class RenderPassCubeMapGeneration : public RenderPass
{
public:
    explicit RenderPassCubeMapGeneration(VkExtent2D sizePerFace) { m_sizePerFace = sizePerFace; }
    ~RenderPassCubeMapGeneration() override;
    void CreatePipeline() override;
    void PrepareRenderPass() override;
    void RecordCommandBuffers();
    VkCommandBuffer GetCommandBuffer() const override { return m_commandBuffer; }

private:
    VkPipeline m_pipeline           = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkExtent2D m_sizePerFace         = { 0, 0 };
    const VkFormat TEX_FORMAT        = VK_FORMAT_R32G32B32A32_SFLOAT;
};
}    // namespace Muyo
