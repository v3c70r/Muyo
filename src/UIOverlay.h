#pragma once
#include "../thirdparty/imgui/imgui.h"
#include "VertexBuffer.h"
#include "VertexBuffer.h"
#include "Texture.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
// ImGui integration
class RenderContext;
class UIOverlay
{
public:
    UIOverlay(VkDevice& device): mDevice(device){};
    bool initialize(RenderContext& context, uint32_t numBuffers,
                    VkPhysicalDevice physicalDevices);
    bool initializeFontTexture(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue);
    bool finalize();
    void renderDrawData(ImDrawData* drawData, RenderContext renderContext, VkCommandPool commandPool, VkQueue graphicsQueue);

private:
    bool mCreateFontSampler();
    bool mCreateDescriptorSetLayout();
    bool mAllcoateDescriptorSets(VkDescriptorPool &descriptorPool);
    bool mCreatePipeline(VkDevice& device);

    VkSampler mFontSampler = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> mDescriptorLayouts;
    std::vector<VkDescriptorSet> mDescriptorSets;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkPipeline mPipeline = VK_NULL_HANDLE;

    std::vector<std::unique_ptr<VertexBuffer>> mpVertexBuffers;
    std::vector<std::unique_ptr<IndexBuffer>> mpIndexBuffers;
    std::unique_ptr<Texture> mpFontTexture;
    //VkDescriptorSetLayout descr
};
