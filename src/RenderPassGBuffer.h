#pragma once
#include <array>

#include "RenderPass.h"

class RenderPassGBuffer : public RenderPass
{
public:
    struct GBufferAttachments
    {
        enum GBufferAttachmentNames
        {
            GBUFFER_POSITION,
            GBUFFER_ALBEDO,
            GBUFFER_NORMAL,
            GBUFFER_UV,
            GBUFFER_DEPTH,
            COLOR_ATTACHMENTS_COUNT = GBUFFER_DEPTH,
            ATTACHMENTS_COUNT
        };
        const std::array<std::string, ATTACHMENTS_COUNT> aNames = {
            "GBUFFER_POSITION", "GBUFFER_ALBEDO", "GBUFFER_NORMAL",
            "GBUFFER_UV",       "GBUFFER_DEPTH",
        };

        const std::array<VkFormat, ATTACHMENTS_COUNT> aFormats = {
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_D32_SFLOAT};
        std::array<VkImageView, ATTACHMENTS_COUNT> aViews;

        std::array<VkAttachmentDescription, ATTACHMENTS_COUNT> aAttachmentDesc;
        GBufferAttachments();
    };
    RenderPassGBuffer();
    ~RenderPassGBuffer();
    void recordCommandBuffer(VkBuffer vertexBuffer, VkBuffer indexBuffer,
                             uint32_t numIndices, VkPipeline pipeline,
                             VkPipelineLayout pipelineLayout,
                             VkDescriptorSet descriptorSet);
    void createFramebuffer();
    void destroyFramebuffer();
    void setGBufferImageViews(VkImageView positionView, VkImageView albedoView,
                              VkImageView normalView, VkImageView uvView,
                              VkImageView depthView, uint32_t nWidth,
                              uint32_t nHeight);
    void createGBufferViews(VkExtent2D size);
    void removeGBufferViews();
    VkCommandBuffer GetCommandBuffer() { return m_commandBuffer; }

private:
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    GBufferAttachments m_attachments;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkExtent2D mRenderArea = {0, 0};
};
