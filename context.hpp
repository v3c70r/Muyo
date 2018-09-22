#pragma once
#include <vulkan/vulkan.h>
#include <vector>

thread_local static size_t s_currentContext;
class Context
{
public:
    static Context& getInstance()
    {
        static Context instance;
        return instance;
    }
    Context() : m_pCommandPool(nullptr){};
    void init(size_t numBuffers, VkDevice *pDevice, VkCommandPool* pPool);
    void finalize();
    void startRecording();
    void endRecording();

    // Move this to framebuffer?
    void beginPass(VkRenderPass& renderPass, VkFramebuffer& frameBuffer,
                   VkExtent2D& extent);
    void endPass();

    void swap();
    bool isRecording() const { return m_recording[s_currentContext]; }
    Context(Context const&) = delete;
    void operator=(Context const&) = delete;
    VkCommandBuffer& getCommandBuffer()
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
