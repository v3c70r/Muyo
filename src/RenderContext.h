#pragma once
#include "ContextBase.h"
#include <vulkan/vulkan.h>
#include <vector>

thread_local static size_t s_currentContext;
class RenderContext: public ContextBase
{
public:
    RenderContext() : m_pCommandPool(nullptr){};
    void initialize(size_t numBuffers, VkDevice *pDevice, VkCommandPool* pPool) override;
    void finalize() override;
    void startRecording() override;
    void endRecording() override;

    // Move this to framebuffer?
    void beginPass(VkRenderPass& renderPass, VkFramebuffer& frameBuffer,
                   VkExtent2D& extent);
    void endPass();

    void swap();
    bool isRecording() const override { return m_recording[s_currentContext]; }
    VkCommandBuffer& getCommandBuffer() override
    {
        return m_commandBuffers[s_currentContext];
    }

private:
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<bool> m_recording;
    VkCommandPool* m_pCommandPool;
    VkDevice* m_pDevice;
    VkCommandBufferBeginInfo m_cmdBufferBeginInfo = {};
};
