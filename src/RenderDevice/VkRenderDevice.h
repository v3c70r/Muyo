#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstring>  // strcmp
#include <memory>
#include <vector>

#include "Debug.h"

namespace Muyo
{
class IResourceBarrier;
class RenderResourceManager;
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

    void CreateCommandPools();
    void DestroyCommandPools();
    VkDevice& GetDevice() { return m_device; }
    VkPhysicalDevice& GetPhysicalDevice() { return m_physicalDevice; }
    VkQueue& GetGraphicsQueue() { return m_graphicsQueue; }
    VkQueue& GetImmediateQueue() { return m_graphicsQueue; }  // TODO: Handle copy queue
    VkQueue& GetPresentQueue() { return m_presentQueue; }
    VkQueue& GetComputeQueue() { return m_computeQueue; }
    VkInstance& GetInstance() { return m_instance; }

    void SetDevice(VkDevice device) { m_device = device; }
    void SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        m_physicalDevice = physicalDevice;
    }

    void SetInstance(VkInstance instance)
    {
        m_instance = instance;
    }

    // Command buffer allocations
    VkCommandBuffer AllocateComputeCommandBuffer();
    void FreeComputeCommandBuffer(VkCommandBuffer& commandBuffer);

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

    void AddResourceBarrier(VkCommandBuffer cmdBuf, IResourceBarrier& resourceBarrier);

    void SubmitCommandBuffers(std::vector<VkCommandBuffer>& vCmdBuffers, VkQueue queue, std::vector<VkSemaphore>& waitSemaphores, std::vector<VkSemaphore>& signalSemaphores, std::vector<VkPipelineStageFlags> stageFlags, VkFence signalFence = VK_NULL_HANDLE);
    void SubmitCommandBuffersAndWait(std::vector<VkCommandBuffer>& vCmdBuffers);

    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer) const;

    // Get physical device properties, neet to manually fill the sType before passing into to this function template
    template<typename VkPropertyType>
    void GetPhysicalDeviceProperties(VkPropertyType& property)
    {
        assert(property.sType != 0);
        VkPhysicalDeviceProperties2 property2 =
            {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                (void *)&property,
            {}};
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &property2);
    }

    VkPipelineLayout CreatePipelineLayout(
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
        const std::vector<VkPushConstantRange>& pushConstantRanges);

private:  // Private structures
    enum CommandPools
    {
        MAIN_CMD_POOL,
        IMMEDIATE_CMD_POOL,
        PER_FRAME_CMD_POOL,
        COMPUTE_CMD_POOL,
        NUM_CMD_POOLS
    };

    struct HWInfo
    {
        uint32_t m_uVersion = 0;

        std::vector<VkLayerProperties> m_vSupportedLayers;
        std::vector<VkLayerProperties> m_vEnabledLayers;

        std::vector<VkExtensionProperties> m_vSupportedInstanceExtensions;
        // std::vector<VkExtensionProperties> m_vEnabledInstanceExtensions;

        std::vector<VkExtensionProperties> m_vSupportedDeviceExtensions;
        // std::vector<VkExtensionProperties> m_vEnabledDeviceExtensions;

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

private:  // helper functions to create render device
    void PickPhysicalDevice();
    VkCommandBuffer AllocatePrimaryCommandbuffer(CommandPools pool);
    void FreePrimaryCommandbuffer(VkCommandBuffer& commandBuffer, CommandPools pool);

private:  // Members
    struct QueueFamilyIndice
    {
        int nGraphicsQueueFamily = -1;
        int nPresentQueneFamily = -1;
        int nComputeQueueFamily = -1;
        bool isComplete() { return nGraphicsQueueFamily >= 0 && nPresentQueneFamily >= 0 && nComputeQueueFamily >= 0; }
    } m_queueFamilyIndices;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    std::array<VkCommandPool, NUM_CMD_POOLS> m_aCommandPools;

    bool m_bIsValidationEnabled = false;
    std::vector<const char*> m_vLayers;

protected:
    VkInstance m_instance = VK_NULL_HANDLE;
};

// Debug device

class VkDebugRenderDevice : public VkRenderDevice
{
    virtual void Initialize(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers) override;
    virtual void Unintialize() override;
    virtual void CreateDevice(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers, const VkSurfaceKHR& surface, const std::vector<void*>& vpFeatures = {}) override;

private:
    DebugUtilsMessenger m_debugMessenger;
};

VkRenderDevice* GetRenderDevice();
}  // namespace Muyo
