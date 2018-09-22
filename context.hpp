#pragma once
#include <vulkan/vulkan.h>
#include <vector>
class Context
{
public:
    static Context& getInstance()
    {
        static Context instance;
        return instance;
    }
    Context() : m_pCommandPool(nullptr){};
    void init(size_t numBuffers, VkCommandPool* pPool);
    void startRecording();
    void endRecording();
    void beginPass();
    void endPass();
    void swap();
    void finalize();
    bool isRecording() const;
    Context(Context const&) = delete;
    void operator=(Context const&) = delete;
    VkCommandBuffer& getCommandBuffer(size_t i)
    {
        return m_commandBuffers[i];
    }

private:
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<bool> m_recording;
    VkCommandPool* m_pCommandPool;
};
