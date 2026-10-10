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

/// @param vSupportedExtensions Extensions enumerated from the instance.
/// @param sName Extension name to look for.
/// @return True when an entry's name equals `sName`.
///
/// Free rather than a member of the private `HWInfo` so it can be exercised against a synthetic
/// list. The inverted `strcmp` this replaced was invisible precisely because nothing could test the
/// negative case, so the asserts guarding startup could never fire.
bool HasInstanceExtension(const std::vector<VkExtensionProperties>& vSupportedExtensions, const char* sName);

/// @param vSupportedLayers Layers enumerated from the instance.
/// @param sName Layer name to look for.
/// @return True when an entry's name equals `sName`.
bool HasLayer(const std::vector<VkLayerProperties>& vSupportedLayers, const char* sName);

/// Whether a physical device reports a device extension.
/// @param vSupportedExtensions Extensions enumerated from the physical device.
/// @param sName Extension name to look for.
/// @return True when an entry's name equals `sName`.
bool HasDeviceExtension(const std::vector<VkExtensionProperties>& vSupportedExtensions, const char* sName);

/// The first requested device extension the physical device does not report, or nullptr when all are.
///
/// Requested device extensions were previously enabled blindly, so asking for one the device lacks
/// surfaced as `VK_ERROR_EXTENSION_NOT_PRESENT` from `vkCreateDevice` - a bare result naming neither the
/// extension nor the device. Free and pure so both branches are testable on any machine, including one
/// whose device lacks nothing.
/// @param vRequestedExtensions Extensions the caller asked to enable.
/// @param vSupportedExtensions Extensions the device reports.
/// @return The name of the first unsupported request, or nullptr when every request is supported.
const char* FindUnsupportedDeviceExtension(const std::vector<const char*>& vRequestedExtensions,
                                           const std::vector<VkExtensionProperties>& vSupportedExtensions);

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

    virtual void Initialize(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers);

    virtual void Unintialize();

    virtual void CreateDevice(const std::vector<const char*>& extensions, const std::vector<const char*>& layers,
                              const VkSurfaceKHR* pSurface, const std::vector<void*>& vpFeatures);

    void DestroyDevice();

    void TransitImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout,
                            VkImageLayout newLayout, uint32_t nMipCount = 1, uint32_t nLayerCount = 1);

    void CreateCommandPools();
    void DestroyCommandPools();
    VkDevice& GetDevice() { return m_device; }
    VkPhysicalDevice& GetPhysicalDevice() { return m_physicalDevice; }
    VkQueue& GetGraphicsQueue() { return m_graphicsQueue; }
    VkQueue& GetImmediateQueue() { return m_graphicsQueue; }  // TODO: Handle copy queue
    VkQueue& GetPresentQueue() { return m_presentQueue; }
    VkQueue& GetComputeQueue() { return m_computeQueue; }
    // Queue family indices, needed for cross-queue resource ownership transfers.
    uint32_t GetGraphicsQueueFamily() const { return static_cast<uint32_t>(m_queueFamilyIndices.nGraphicsQueueFamily); }
    uint32_t GetComputeQueueFamily() const { return static_cast<uint32_t>(m_queueFamilyIndices.nComputeQueueFamily); }
    bool IsComputeQueueDedicated() const
    {
        return m_queueFamilyIndices.nComputeQueueFamily >= 0 &&
               m_queueFamilyIndices.nComputeQueueFamily != m_queueFamilyIndices.nGraphicsQueueFamily;
    }
    /// @return True when the selected physical device exposes `VK_KHR_cooperative_matrix` *and* the
    ///         feature that gates it. Queried at device creation; false before `CreateDevice` runs.
    bool IsCooperativeMatrixSupported() const { return m_bCooperativeMatrixSupported; }

    /// @return The cooperative-matrix property sets - which shapes and component types the device can
    ///         actually compute - or empty when the extension is unsupported **or was not requested**.
    ///
    /// Fetched only for a caller that asked for the extension. Enumerating the sets dereferences a driver
    /// dispatch entry that some driver + validation-layer combinations leave NULL (#72), and that cannot be
    /// probed for safely because the crash *is* the probe. So a renderer that never asks pays nothing, and a
    /// consumer that asks gets the shapes it needs to pick a kernel.
    ///
    /// Read them rather than assuming a shape: the sets differ by device. On this machine AMDVLK advertises
    /// `F16/F16->F32` at 16x16x16 among eleven sets while llvmpipe advertises 8x8x8, so a constant in the
    /// code would be testing the driver rather than the kernel (AGENTS.md section 8).
    const std::vector<VkCooperativeMatrixPropertiesKHR>& GetCooperativeMatrixProperties() const
    {
        return m_vCooperativeMatrixProperties;
    }

    VkInstance& GetInstance() { return m_instance; }

    void SetDevice(VkDevice device) { m_device = device; }
    void SetPhysicalDevice(VkPhysicalDevice physicalDevice) { m_physicalDevice = physicalDevice; }

    void SetInstance(VkInstance instance) { m_instance = instance; }

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

        VkCommandBufferSubmitInfo commandBufferInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandBufferInfo.commandBuffer = immediateCmdBuf;
        commandBufferInfo.deviceMask = 0;
        VkSubmitInfo2 submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;
        vkQueueSubmit2(GetImmediateQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(GetImmediateQueue());  // wait for it to finish

        FreeImmediateCommandBuffer(immediateCmdBuf);
    }

    // Helper functions
    VkSampler CreateSampler();

    void AddResourceBarrier(VkCommandBuffer cmdBuf, IResourceBarrier& resourceBarrier);

    void SubmitCommandBuffers(std::vector<VkCommandBuffer>& vCmdBuffers, VkQueue queue,
                              std::vector<VkSemaphore>& waitSemaphores, std::vector<VkSemaphore>& signalSemaphores,
                              std::vector<VkPipelineStageFlags2> stageFlags, VkFence signalFence = VK_NULL_HANDLE);
    void SubmitCommandBuffersAndWait(std::vector<VkCommandBuffer>& vCmdBuffers);

    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer) const;

    // Get physical device properties, neet to manually fill the sType before passing into to this function template
    template <typename VkPropertyType>
    void GetPhysicalDeviceProperties(VkPropertyType& property)
    {
        assert(property.sType != 0);
        VkPhysicalDeviceProperties2 property2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, (void*)&property, {}};
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &property2);
    }

    VkPipelineLayout CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
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
            return HasInstanceExtension(m_vSupportedInstanceExtensions, sInstanceExtensionName);
        }

        bool IsLayerSupported(const char* sLayerName) { return HasLayer(m_vSupportedLayers, sLayerName); }
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

    /// Cooperative matrix, queried at device creation whether or not it is requested, so a consumer can
    /// ask before deciding and the answer appears in the log.
    bool m_bCooperativeMatrixSupported = false;
    std::vector<VkCooperativeMatrixPropertiesKHR> m_vCooperativeMatrixProperties;
    /// Owned by the device rather than by a caller, because it must outlive `vkCreateDevice`, and chained
    /// only when the caller requests the extension - the renderer does not use cooperative matrix and must
    /// not start requiring it (AGENTS.md section 3: verify, never enable blindly).
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR m_cooperativeMatrixFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};

protected:
    VkInstance m_instance = VK_NULL_HANDLE;
};

// Debug device

class VkDebugRenderDevice : public VkRenderDevice
{
    virtual void Initialize(const std::vector<const char*>& vExtensions,
                            const std::vector<const char*>& vLayers) override;
    virtual void Unintialize() override;
    virtual void CreateDevice(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers,
                              const VkSurfaceKHR* pSurface = nullptr,
                              const std::vector<void*>& vpFeatures = {}) override;

private:
    DebugUtilsMessenger m_debugMessenger;
};

VkRenderDevice* GetRenderDevice();
}  // namespace Muyo
