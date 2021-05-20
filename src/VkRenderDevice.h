#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <memory>
#include <cstring>  // strcmp
#include "Swapchain.h"


class RenderResourceManager;
class Swapchain;
class VkRenderDevice
{
public:
    constexpr bool IsRayTracingSupported() const
    {
#ifdef FEATURE_RAY_TRACING
        return true;
#else
        return false;
#endif
    }
    virtual void Initialize(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers = std::vector<const char*>());

    virtual void Unintialize();

    virtual void CreateDevice(
        const std::vector<const char*>& extensions,
        const std::vector<const char*>& layers,
        const VkSurfaceKHR& surface,
        const std::vector<void*>& vpFeatures = {});


    void DestroyDevice();

    void TransitImageLayout(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t nMipCount = 1,
        uint32_t nLayerCount = 1);

    void CreateSwapchain(const VkSurfaceKHR& swapchainSurface);
    void DestroySwapchain();
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

    // Helper functions
    VkSampler CreateSampler();

    VkExtent2D GetViewportSize() const { return m_viewportSize; }
    void SetViewportSize(VkExtent2D vp);

    Swapchain* GetSwapchain() { return m_pSwapchain.get(); }

    void BeginFrame();
    void Present();
    void SubmitCommandBuffers(std::vector<VkCommandBuffer>& vCmdBuffers);
    void SubmitCommandBuffersAndWait(std::vector<VkCommandBuffer>& vCmdBuffers);
    uint32_t GetFrameIdx() const {return m_uImageIdx2Present;}

    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer) const;

    // Get physical device properties, overload different structures to get specific property
    // TODO: Possible cache all needed property at physical device creation.
    void GetPhysicalDeviceProperties(VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtProperties)
    {
        rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        rtProperties.pNext = nullptr;
        VkPhysicalDeviceProperties2 property2 =
            {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                (void*)&rtProperties,
                {}};
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &property2);
    }

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
        std::vector<VkLayerProperties> m_vEnabledLayers;

        std::vector<VkExtensionProperties> m_vSupportedInstanceExtensions;
        //std::vector<VkExtensionProperties> m_vEnabledInstanceExtensions;

        std::vector<VkExtensionProperties> m_vSupportedDeviceExtensions;
        //std::vector<VkExtensionProperties> m_vEnabledDeviceExtensions;

        // TODO: Handle layers

        HWInfo()
        {
            vkEnumerateInstanceVersion(&m_uVersion);
            uint32_t uLayerCount = 0;

            vkEnumerateInstanceLayerProperties(&uLayerCount, nullptr);
            m_vSupportedLayers.resize(uLayerCount);
            vkEnumerateInstanceLayerProperties(&uLayerCount, m_vSupportedLayers.data());

            uint32_t uExtensionCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &uExtensionCount, nullptr);
            m_vSupportedInstanceExtensions.resize(uExtensionCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &uExtensionCount, m_vSupportedInstanceExtensions.data());
        }

        bool IsInstanceExtensionSupported(const char* sInstanceExtensionName)
        {
            for (const auto& extensionProperty : m_vSupportedInstanceExtensions)
            {
                if (strcmp(extensionProperty.extensionName, sInstanceExtensionName))
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

    const VkSurfaceFormatKHR SWAPCHAIN_FORMAT = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    const VkPresentModeKHR PRESENT_MODE = VK_PRESENT_MODE_FIFO_KHR;

#ifdef __APPLE__
    static const uint32_t NUM_BUFFERS = 2;
#else
    static const uint32_t NUM_BUFFERS = 3;
#endif

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

    VkExtent2D m_viewportSize;

    // Presentation related members
    std::unique_ptr<Swapchain> m_pSwapchain = nullptr;
    VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;   // Semaphores to notify the frame when the current image is ready
    VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
    std::array<VkFence, NUM_BUFFERS> m_aGPUExecutionFence;
    uint32_t m_uImageIdx2Present = 0;
protected:
    VkInstance m_instance = VK_NULL_HANDLE;
};

// Debug device
#include "Debug.h"
class VkDebugRenderDevice : public VkRenderDevice
{
    virtual void Initialize(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers) override;
    virtual void Unintialize() override;
    virtual void CreateDevice(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers, const VkSurfaceKHR& surface, const std::vector<void*>& vpFeatures = {}) override;

private:
    DebugUtilsMessenger m_debugMessenger;
};

VkRenderDevice* GetRenderDevice();
