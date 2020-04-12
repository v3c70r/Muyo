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
    int GetGraphicsQueueFamilyIndex() const { return mGraphicsQueueFamilyIndex; }
    int GetPresentQueueFamilyIndex() const { return mPresentQueueFamilyIndex; }

    void SetDevice(VkDevice device) { mDevice = device; }
    void SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        mPhysicalDevice = physicalDevice;
    }
    void SetGraphicsQueue(VkQueue graphicsQueue, int queueFamilyIndex)
    {
        mGraphicsQueue = graphicsQueue;
        mGraphicsQueueFamilyIndex = queueFamilyIndex;
    }

    void SetPresentQueue(VkQueue presentQueue, int queueFamilyIndex)
    {
        mPresentQueue = presentQueue;
        mPresentQueueFamilyIndex = queueFamilyIndex;
    }

    // TODO: Add compute queue if needed

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
    int mGraphicsQueueFamilyIndex = -1;
    int mPresentQueueFamilyIndex= -1;
    int mComputeQueueFamilyIndex = -1;
};

VkRenderDevice* GetRenderDevice();
