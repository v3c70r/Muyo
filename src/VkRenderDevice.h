#pragma once
#include <vulkan/vulkan.h>
#include <vector>

class VkRenderDevice
{
public:
    void Initialize(const std::vector<const char*>& layers,
                    const std::vector<const char*>& extensions);
    void Unintialize();

    void createLogicalDevice(const std::vector<const char*>& layers,
                             const std::vector<const char*>& extensions,
                             VkSurfaceKHR surface);

    void destroyLogicalDevice();
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
    // helper functions to create render device
    void createPhysicalDevice();

    VkDevice mDevice = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    VkQueue mPresentQueue = VK_NULL_HANDLE;
    VkInstance mInstance = VK_NULL_HANDLE;
    int mGraphicsQueueFamilyIndex = -1;
    int mPresentQueueFamilyIndex= -1;
    int mComputeQueueFamilyIndex = -1;
    bool mbIsValidationEnabled = false;
    std::vector<const char*> mLayers;
    std::vector<const char*> mExtensions;

};

VkRenderDevice* GetRenderDevice();
