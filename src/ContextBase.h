#pragma once
#include <cstddef>
#include <vulkan/vulkan.h>

class ContextBase
{
public:
    virtual void startRecording() = 0;
    virtual void endRecording() = 0;
    virtual bool isRecording() const = 0;
    virtual void initialize(size_t numBuffers, VkDevice* pDevice,
                            VkCommandPool* pPool) = 0;
    virtual void finalize() = 0;
    virtual VkCommandBuffer& getCommandBuffer() = 0;
    virtual ~ContextBase(){};
};

