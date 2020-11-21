#pragma once
#include <array>
#include <memory>

#include "Geometry.h"
#include "RenderPass.h"

class RenderPassGBuffer : public RenderPass
{
public:
    struct LightingAttachments
    {
        enum LightingAttachmentNames
        {
            GBUFFER_POSITION_AO,
            GBUFFER_ALBEDO_TRANSMITTANCE,
            GBUFFER_NORMAL_ROUGHNESS,
            GBUFFER_METALNESS_TRANSLUCENCY,
            LIGHTING_OUTPUT,
            GBUFFER_DEPTH,

            // Counters
            ATTACHMENTS_COUNT,
            COLOR_ATTACHMENTS_COUNT = GBUFFER_DEPTH,
            GBUFFER_ATTACHMENTS_COUNT = LIGHTING_OUTPUT
        };
        const std::array<const std::string, ATTACHMENTS_COUNT> aNames = {
            "GBUFFER_POSITION_AO", "GBUFFER_ALBEDO_TRANSMITTANCE",  "GBUFFER_NORMAL_ROUGHNESS",
            "GBUFFER_METALNESS_TRANSLUCENCY",       "LIGHTING_OUTPUT", "GBUFFER_DEPTH"};

        static constexpr std::array<VkFormat, ATTACHMENTS_COUNT> aFormats = {
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D32_SFLOAT};

        static constexpr std::array<VkAttachmentReference,
                                    GBUFFER_ATTACHMENTS_COUNT>
            aGBufferColorAttachmentRef = {{
                {GBUFFER_POSITION_AO, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {GBUFFER_ALBEDO_TRANSMITTANCE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {GBUFFER_NORMAL_ROUGHNESS, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {GBUFFER_METALNESS_TRANSLUCENCY, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            }};

        static constexpr std::array<VkAttachmentReference,
                                    GBUFFER_ATTACHMENTS_COUNT>
            aLightingInputRef = {{
                {GBUFFER_POSITION_AO, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {GBUFFER_ALBEDO_TRANSMITTANCE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {GBUFFER_NORMAL_ROUGHNESS, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {GBUFFER_METALNESS_TRANSLUCENCY, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            }};

        static constexpr std::array<VkAttachmentReference, 1>
            aLightingColorAttachmentRef = {{
                {LIGHTING_OUTPUT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            }};

        static constexpr VkAttachmentReference m_depthAttachment = {
            GBUFFER_DEPTH, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        static constexpr std::array<VkClearValue, ATTACHMENTS_COUNT>
            aClearValues = {{
                {.color = {0.0f, 0.0f, 0.0f, 0.0f}},  // Position
                {.color = {0.0f, 0.0f, 0.0f, 0.0f}},  // Albedo
                {.color = {0.0f, 0.0f, 0.0f, 0.0f}},  // Normal
                {.color = {0.0f, 0.0f, 0.0f, 0.0f}},  // UV
                {.color = {0.0f, 0.0f, 0.0f, 0.0f}},  // Lighting output
                {.depthStencil = {1.0f, 0}},          // Depth stencil
            }};

        // TODO: This can be const as well
        std::array<VkAttachmentDescription, ATTACHMENTS_COUNT> aAttachmentDesc;

        std::array<VkImageView, ATTACHMENTS_COUNT> aViews;

        LightingAttachments();
    };
    using GBufferViews =
        std::array<VkImageView, LightingAttachments::GBUFFER_ATTACHMENTS_COUNT>;

    RenderPassGBuffer();
    ~RenderPassGBuffer();
    void recordCommandBuffer(const std::vector<const Geometry*>& vpGeometries);
    void createFramebuffer();
    void destroyFramebuffer();
    void setGBufferImageViews(VkImageView positionView, VkImageView albedoView,
                              VkImageView normalView, VkImageView uvView,
                              VkImageView lightingOutput, VkImageView depthView,
                              uint32_t nWidth, uint32_t nHeight);
    void createGBufferViews(VkExtent2D size);
    void removeGBufferViews();
    VkCommandBuffer GetCommandBuffer() { return mCommandBuffer; }
    void createPipelines();

private:
private:
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
    LightingAttachments mAttachments;
    VkFramebuffer mFramebuffer = VK_NULL_HANDLE;
    VkExtent2D mRenderArea = {0, 0};

    VkPipeline mGBufferPipeline = VK_NULL_HANDLE;
    VkPipeline mLightingPipeline = VK_NULL_HANDLE;

    VkPipelineLayout mGBufferPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout mLightingPipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSet mPerViewDescSet;
    VkDescriptorSet mMaterialDescSet;
    Geometry* mpQuad;
};
