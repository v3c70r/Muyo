#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <memory>
#include <cstring>  // strcmp



class RenderResourceManager;
class Swapchain;
class VkRenderDevice
{
public:
    virtual void Initialize(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers = std::vector<const char*>());

    virtual void Unintialize();

    virtual void CreateDevice(
        const std::vector<const char*>& extensions,
        const std::vector<const char*>& layers,
        VkSurfaceKHR surface);

    void DestroyDevice();
    void CreateCommandPools();
    void DestroyCommandPools();
    VkDevice& GetDevice() { return m_device; }
    VkPhysicalDevice& GetPhysicalDevice() { return m_physicalDevice; }
    VkQueue& GetGraphicsQueue() { return m_graphicsQueue; }
    VkQueue& GetImmediateQueue() { return m_graphicsQueue;} // TODO: Handle copy queue
    VkQueue& GetPresentQueue() { return m_presentQueue; }
    VkInstance& GetInstance() { return m_instance; }
    int GetGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
    int GetPresentQueueFamilyIndex() const { return m_presentQueueFamilyIndex; }

    void SetDevice(VkDevice device) { m_device = device; }
    void SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        m_physicalDevice = physicalDevice;
    }
    void SetGraphicsQueue(VkQueue graphicsQueue, int queueFamilyIndex)
    {
        m_graphicsQueue = graphicsQueue;
        m_graphicsQueueFamilyIndex = queueFamilyIndex;
    }

    void SetPresentQueue(VkQueue presentQueue, int queueFamilyIndex)
    {
        m_presentQueue = presentQueue;
        m_presentQueueFamilyIndex = queueFamilyIndex;
    }

    // TODO: Add compute queue if needed

    void SetInstance(VkInstance instance)
    {
        m_instance = instance;
    }

    // Command buffer allocations
    VkCommandBuffer AllocateStaticPrimaryCommandbuffer();

    void FreeStaticPrimaryCommandbuffer(VkCommandBuffer& commandBuffer);

    VkCommandBuffer AllocateReusablePrimaryCommandbuffer();
    void FreeReusablePrimaryCommandbuffer(VkCommandBuffer& commandBuffer);

    VkCommandBuffer AllocateSecondaryCommandBuffer();
    void FreeSecondaryCommandBuffer(VkCommandBuffer& commandBuffer);

    VkCommandBuffer AllocateImmediateCommandBuffer();
    void FreeImmediateCommandBuffer(VkCommandBuffer& commandBuffer);

    // Comamnd buffer executions
    template <typename Func>
    void ExecuteImmediateCommand(Func fImmediateGPUTask)
    {
        VkCommandBuffer immediateCmdBuf = AllocateImmediateCommandBuffer();

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

        FreeImmediateCommandBuffer(immediateCmdBuf);
    }

    void ExecuteCommandbuffer(VkCommandBuffer commandBuffer)
    {
    }

    // Helper functions
    VkSampler CreateSampler();

    VkExtent2D GetViewportSize() const { return m_viewportSize; }
    void SetViewportSize(VkExtent2D vp) { m_viewportSize = vp; }

private: // Private structures
    enum CommandPools
    {
        MAIN_CMD_POOL,
        IMMEDIATE_CMD_POOL,
        PER_FRAME_CMD_POOL,
        NUM_CMD_POOLS
    };

    struct HWInfo
    {
        uint32_t m_uVersion = 0;
        std::vector<VkLayerProperties> m_vSupportedLayers;
        std::vector<VkExtensionProperties> m_vSupportedExtensions;
        HWInfo()
        {
            vkEnumerateInstanceVersion(&m_uVersion);
            uint32_t uLayerCount = 0;

            vkEnumerateInstanceLayerProperties(&uLayerCount, nullptr);
            m_vSupportedLayers.resize(uLayerCount);
            vkEnumerateInstanceLayerProperties(&uLayerCount, m_vSupportedLayers.data());

            uint32_t uExtensionCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &uExtensionCount, nullptr);
            m_vSupportedExtensions.resize(uExtensionCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &uExtensionCount, m_vSupportedExtensions.data());
        }

        bool IsExtensionSupported(const char* sExtensionName)
        {
            for (const auto& extensionProperty : m_vSupportedExtensions)
            {
                if (strcmp(extensionProperty.extensionName, sExtensionName))
                {
                    return true;
                }
            }
            return false;
        }

        bool IsLayerSupported(const char* sLayerName)
        {
            for (const auto& layerProperty : m_vSupportedLayers)
            {
                if (strcmp(layerProperty.layerName, sLayerName))
                {
                    return true;
                }
            }
            return false;
        }
    };

private:  // helper functions to create render device
    void PickPhysicalDevice();
    VkCommandBuffer AllocatePrimaryCommandbuffer(CommandPools pool);
    void FreePrimaryCommandbuffer(VkCommandBuffer& commandBuffer, CommandPools pool);

private:  // Members
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    std::array<VkCommandPool, NUM_CMD_POOLS> m_aCommandPools = {VK_NULL_HANDLE,
                                                               VK_NULL_HANDLE};

    int m_graphicsQueueFamilyIndex = -1;
    int m_presentQueueFamilyIndex = -1;
    int m_computeQueueFamilyIndex = -1;
    bool m_bIsValidationEnabled = false;
    std::vector<const char*> m_vLayers;
    std::unique_ptr<Swapchain> m_pSwapchain = nullptr;
    VkExtent2D m_viewportSize;
protected:
    VkInstance m_instance = VK_NULL_HANDLE;
};

// Debug device
#include "Debug.h"
class VkDebugRenderDevice : public VkRenderDevice
{
    virtual void Initialize(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers) override;
    virtual void Unintialize() override;
    virtual void CreateDevice(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers, VkSurfaceKHR surface) override;

private:
    DebugUtilsMessenger m_debugMessenger;
};

VkRenderDevice* GetRenderDevice();
