#pragma once

#include "RenderPass.h"

namespace Muyo
{
class SceneNode;
class RenderPassGBuffer : public RenderPass
{
    public:
        explicit RenderPassGBuffer(const VkExtent2D& renderArea):m_renderArea(renderArea) {}
        ~RenderPassGBuffer();
        void PrepareRenderPass() override;
        void CreatePipeline() override;

        void RecordCommandBuffers(const std::vector<const SceneNode*>& vpGeometryNodes);
        VkCommandBuffer GetCommandBuffer() const override { return m_commandBuffer; }

    private:
        VkPipeline m_pipeline = VK_NULL_HANDLE;
        VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
        VkExtent2D m_renderArea = {0, 0};

    public:
        static const int ATTACHMENT_COUNT = 5;
        static const int COLOR_ATTACHMENT_COUNT = ATTACHMENT_COUNT - 1;
        struct GBufferAttachment
        {
            std::string sName;
            VkFormat format;
            VkClearValue clearValue;
        };
        static const GBufferAttachment attachments[ATTACHMENT_COUNT];
};

}  // namespace Muyo
