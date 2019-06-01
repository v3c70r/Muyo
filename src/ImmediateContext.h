#pragma once
#include "ContextBase.h"
#include "memory"
// Forward declarations


class ImmediateContext : public ContextBase
{
public:
    ImmediateContext(VkDevice &device, VkCommandPool& commandPool, VkQueue& queue);
    ~ImmediateContext();
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


class ImmediateContextFactory
{
public:
    ImmediateContextFactory(VkDevice& device, VkCommandPool& pool,
                           VkQueue& queue)
        : m_device(device), m_pool(pool), m_queue(queue)
    {
    }
    ImmediateContext getImmediateContext()
    {
        //return ImmediateContext(m_device, m_pool, m_queue);
    }
private:
    VkDevice m_device;
    VkCommandPool m_pool;
    VkQueue m_queue;
};
