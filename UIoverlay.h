// Wrapper for Imgui overlay of vulkan
#pragma once
#include <vulkan/vulkan.hpp>
#include "VertexBuffer.h"

struct UIOverlayCreateInfo {
    VkDevice* device;
    VkQueue copyQueue;
    VkRenderPass renderPass;
    std::vector<VkFramebuffer> framebuffers;
    VkFormat colorformat;
    VkFormat depthformat;
    uint32_t width;
    uint32_t height;
    std::vector<VkPipelineShaderStageCreateInfo> shaders;
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t subpassCount = 1;
    std::vector<VkClearValue> clearValues = {};
    uint32_t attachmentCount = 1;
};

class UIoverlay {
public:
    UIoverlay(UIOverlayCreateInfo createInfo);
    ~UIoverlay();

private:
    VertexBuffer* mpVertexBuffer;
    IndexBuffer* indexBuffer;
    int32_t mVertexCount = 0;
    int32_t mIndexCount = 0;

    VkDescriptorPool mDescriptorPool;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkDescriptorSet mDescriptorSet;
    VkPipelineLayout mPipelineLayout;
    VkPipelineCache mPipelineCache;
    VkPipeline mPipeline;
    VkRenderPass mRenderPass;
    VkCommandPool mCommandPool;
    VkFence mFence;

    VkDeviceMemory mFontMemory = VK_NULL_HANDLE;
    VkImage mFontImage = VK_NULL_HANDLE;
    VkImageView mFontView = VK_NULL_HANDLE;
    VkSampler mSampler;

    struct PushConstBlock {
        glm::vec2 scale;
        glm::vec2 translate;
    } mPushConstBlock;

    UIOverlayCreateInfo mCreateInfo = {};
};
