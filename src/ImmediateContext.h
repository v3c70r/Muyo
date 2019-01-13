#pragma once
#include "ContextBase.h"
#include "memory"
// Forward declarations
class VkDevice;
class VkCommandPool;
class VkCommandBuffer;
class VkQueue;
class ImmediateContext : public ContextBase
{
public:
    ImmediateContext(const VkDevice &device, const VkCommandPool& commandPool, const VkQueue& queue);
    ~ImmediateContext();
    void startRecording() override;
    void endRecording() override;
    bool isRecording() const override;
private:
    std::unique_ptr<VkCommandBuffer> m_commandBuffer;
    const VkQueue& m_queue;
};
