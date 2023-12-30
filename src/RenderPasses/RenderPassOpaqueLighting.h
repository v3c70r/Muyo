#pragma once
#include "RenderPass.h"

namespace Muyo
{
class RenderPassOpaqueLighting : public RenderPass
{
    public:
        explicit RenderPassOpaqueLighting(const VkExtent2D renderArea): m_renderArea(renderArea){};
        ~RenderPassOpaqueLighting()
        {
            vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        }

        void PrepareRenderPass() override;
        void CreatePipeline() override;

        void RecordCommandBuffers();

        VkCommandBuffer GetCommandBuffer() const override
        {
            return m_commandBuffer;
        }

    private:
        VkExtent2D m_renderArea = {0, 0};
        VkPipeline m_pipeline = VK_NULL_HANDLE;
        VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};
}
