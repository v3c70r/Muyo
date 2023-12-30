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

        struct GBufferAttachment
        {
            std::string sName;
            VkFormat format;
            VkClearValue clearValue;
        };

        static const int ATTACHMENT_COUNT = 5;
        static const int COLOR_ATTACHMENT_COUNT = ATTACHMENT_COUNT - 1;
        static const GBufferAttachment m_attachments[ATTACHMENT_COUNT] =
            {
                {"GBufferPositionAO_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
                {"GBufferAlbedoTransmittance_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
                {"GBufferNormalRoughness_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
                {"GbufferMetalnessTranslucency_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
                {"GBuffferDepth_", VK_FORMAT_D32_SFLOAT, {.depthStencil = {1.0f, 0}}}};
};
}  // namespace Muyo
