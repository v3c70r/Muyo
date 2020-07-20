#pragma once
#include <array>
#include <memory>

#include "RenderPass.h"
#include "Geometry.h"

class RenderPassGBuffer : public RenderPass
{
public:
    struct LightingAttachments
    {
        enum LightingAttachmentNames
        {
            GBUFFER_POSITION,
            GBUFFER_ALBEDO,
            GBUFFER_NORMAL,
            GBUFFER_UV,
            LIGHTING_OUTPUT,
            GBUFFER_DEPTH,

            // Counters
            ATTACHMENTS_COUNT,
            COLOR_ATTACHMENTS_COUNT = GBUFFER_DEPTH
        };
        const std::array<const std::string, ATTACHMENTS_COUNT> aNames = {
            "GBUFFER_POSITION", "GBUFFER_ALBEDO",  "GBUFFER_NORMAL",
            "GBUFFER_UV",       "LIGHTING_OUTPUT", "GBUFFER_DEPTH"};

        static constexpr std::array<VkFormat, ATTACHMENTS_COUNT> aFormats = {
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D32_SFLOAT};

        static constexpr std::array<VkAttachmentReference, COLOR_ATTACHMENTS_COUNT>
            aColorAttachmentRef = {{
                {GBUFFER_POSITION, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {GBUFFER_ALBEDO, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {GBUFFER_NORMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {GBUFFER_UV, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {LIGHTING_OUTPUT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            }};

        static constexpr VkAttachmentReference m_depthAttachment = {
            GBUFFER_DEPTH, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        // TODO: This can be const as well
        std::array<VkAttachmentDescription, ATTACHMENTS_COUNT> aAttachmentDesc;

        std::array<VkImageView, ATTACHMENTS_COUNT> aViews;

        LightingAttachments();

    };
    RenderPassGBuffer();
    ~RenderPassGBuffer();
    void recordCommandBuffer(const PrimitiveList& primitives,
                             VkPipeline pipeline,
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
    LightingAttachments m_attachments;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkExtent2D mRenderArea = {0, 0};
};
