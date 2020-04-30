#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <memory>

class RenderResourceManager;
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
    void createCommandPools();
    void destroyCommandPools();
    VkDevice& GetDevice() { return mDevice; }
    VkPhysicalDevice& GetPhysicalDevice() { return mPhysicalDevice; }
    VkQueue& GetGraphicsQueue() { return mGraphicsQueue; }
    VkQueue& GetImmediateQueue() { return mGraphicsQueue;} // TODO: Handle copy queue
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

    // Command buffer allocations
    VkCommandBuffer allocatePrimaryCommandbuffer();
    void freePrimaryCommandbuffer(VkCommandBuffer& commandBuffer);

    VkCommandBuffer allocateSecondaryCommandBuffer();
    void freeSecondaryCommandBuffer(VkCommandBuffer& commandBuffer);

    VkCommandBuffer allocateImmediateCommandBuffer();
    void freeImmediateCommandBuffer(VkCommandBuffer& commandBuffer);

    // Comamnd buffer executions
    template <typename Func>
    void executeImmediateCommand(Func fImmediateGPUTask)
    {
        VkCommandBuffer immediateCmdBuf = allocateImmediateCommandBuffer();

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(immediateCmdBuf, &beginInfo);
        fImmediateGPUTask(immediateCmdBuf);
        vkEndCommandBuffer(immediateCmdBuf);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &immediateCmdBuf;
        vkQueueSubmit(GetImmediateQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(GetImmediateQueue());  // wait for it to finish

        freeImmediateCommandBuffer(immediateCmdBuf);
    }

    void executeCommandbuffer(VkCommandBuffer commandBuffer)
    {
    }

private:
    // helper functions to create render device
    void createPhysicalDevice();

    VkDevice mDevice = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    VkQueue mPresentQueue = VK_NULL_HANDLE;
    VkInstance mInstance = VK_NULL_HANDLE;

    enum CommandPools
    {
        MAIN_CMD_POOL,
        IMMEDIATE_CMD_POOL,
        NUM_CMD_POOLS
    };
    std::array<VkCommandPool, NUM_CMD_POOLS> maCommandPools = {VK_NULL_HANDLE,
                                                               VK_NULL_HANDLE};

    int mGraphicsQueueFamilyIndex = -1;
    int mPresentQueueFamilyIndex = -1;
    int mComputeQueueFamilyIndex = -1;
    bool mbIsValidationEnabled = false;
    std::vector<const char*> mLayers;
    std::vector<const char*> mExtensions;
};

VkRenderDevice* GetRenderDevice();
