#pragma once
#include "ContextBase.h"
#include "memory"
// Forward declarations

class ImmediateContext : public ContextBase
{
public:
    ImmediateContext(VkDevice &device, VkCommandPool& commandPool, VkQueue& queue);
    ~ImmediateContext(){};
    void startRecording() override;
    void endRecording() override;
    bool isRecording() const override;
    void initialize(size_t numBuffers, VkDevice* pDevice,
                            VkCommandPool* pPool) override;
    void finalize() override;
    VkCommandBuffer& getCommandBuffer() override;
private:
    std::unique_ptr<VkCommandBuffer> m_commandBuffer;
    const VkDevice& m_device;
    const VkCommandPool& m_pool;
    const VkQueue& m_queue;
};
