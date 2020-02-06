#pragma once
#include <vulkan/vulkan.h>

class VkRenderDevice
{
public:
    VkDevice& GetDevice() { return mDevice; }
    VkPhysicalDevice& GetPhysicalDevice() { return mPhysicalDevice; }
    VkQueue& GetGraphicsQueue() { return mGraphicsQueue; }
    VkQueue& GetPresentQueue() { return mPresentQueue; }
    VkInstance& GetInstance() { return mInstance; }

    void SetDevice(VkDevice device) { mDevice = device; }
    void SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        mPhysicalDevice = physicalDevice;
    }
    void SetGraphicsQueue(VkQueue graphicsQueue)
    {
        mGraphicsQueue = graphicsQueue;
    }

    void SetPresentQueue(VkQueue presentQueue)
    {
        mPresentQueue = presentQueue;
    }

    void SetInstance(VkInstance instance)
    {
        mInstance = instance;
    }

    VkCommandBuffer CreateCommandBuffer()
    {
        return VkCommandBuffer();
    }

private:
    VkDevice mDevice = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    VkQueue mPresentQueue = VK_NULL_HANDLE;
    VkInstance mInstance = VK_NULL_HANDLE;
};

VkRenderDevice* GetRenderDevice();
