#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstring>  // strcmp
#include <memory>
#include <string>
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
    /// @return True when this build was compiled with `FEATURE_RAY_TRACING`.
    ///
    /// A compile-time answer, not a device query: it says the ray-tracing code path exists, not that the
    /// selected device supports it. Device support is checked by `CreateDevice` when the extensions are
    /// requested.
    constexpr bool IsRayTracingSupported() const
    {
#ifdef FEATURE_RAY_TRACING
        return true;
#else
        return false;
#endif
    }

    /// @return True when the selected physical device exposes `VK_EXT_descriptor_heap` *and* reports
    ///         `VkPhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap`. Queried at device creation.
    bool IsDescriptorHeapSupported() const { return m_bDescriptorHeapSupported; }

    /// @return True when the descriptor heap extension and feature were requested and enabled on the
    ///         device. False before `CreateDevice`, and false on a device that does not support it.
    bool IsDescriptorHeapEnabled() const { return m_bDescriptorHeapEnabled; }

    /// @return True when the device exposes `VK_KHR_shader_untyped_pointers` and reports
    ///         `shaderUntypedPointers`. Queried at device creation.
    bool IsShaderUntypedPointersSupported() const { return m_bShaderUntypedPointersSupported; }

    /// @return True when untyped pointers were requested and enabled on the device.
    bool IsShaderUntypedPointersEnabled() const { return m_bShaderUntypedPointersEnabled; }

    /// @return The queried descriptor-heap properties, or a zeroed struct before `CreateDevice` or on a
    ///         device without the extension. Read sizes from `GetDescriptorSize` rather than assuming
    ///         the per-region maxima are the per-type strides.
    const VkPhysicalDeviceDescriptorHeapPropertiesEXT& GetDescriptorHeapProperties() const
    {
        return m_descriptorHeapProperties;
    }

    /// @param type A descriptor type written to a descriptor heap.
    /// @return The implementation's per-type descriptor size, or 0 when the extension is unavailable.
    VkDeviceSize GetDescriptorSize(VkDescriptorType type) const;

    /// @return The loader's supported instance version, recorded by `Initialize`.
    uint32_t GetInstanceVersion() const { return m_uInstanceVersion; }

    /// @return True when `Initialize` was asked for `VK_LAYER_KHRONOS_validation`.
    ///
    /// This reflects the caller's request, not the layer's behaviour: the device appends neither the
    /// layer nor its extension. A host that wants validation asks for it explicitly; see
    /// `Muyo::InitializeDeviceWithValidation` for this repository's hosts' default.
    bool IsValidationLayerEnabled() const { return m_bIsValidationLayerEnabled; }

    /// @return True when a debug-utils messenger is currently installed by `InitializeDebugMessenger`.
    ///
    /// A device can be fully initialized and usable with this false; tests and the engine hosts select
    /// validation at the call site (#58).
    bool HasDebugMessenger() const { return m_debugMessenger.IsCreated(); }

    /// @return The instance layers this device was initialized with, in request order.
    ///
    /// Recorded rather than inferred so a host can report what it actually enabled, and so the
    /// validation-policy test can assert that the minimal initialization enables none.
    const std::vector<std::string>& GetEnabledInstanceLayers() const { return m_vInstanceLayers; }

    /// @return The instance extensions this device was initialized with, in request order.
    const std::vector<std::string>& GetEnabledInstanceExtensions() const { return m_vInstanceExtensions; }

    /// @return The selected device's name. Stable across instance re-creations because `PickPhysicalDevice`
    ///         pins the device by name (#141).
    const std::string& GetPhysicalDeviceName() const { return m_pinnedPhysicalDeviceName; }

    /// Pipeline stages this device's enabled features make legal in a barrier mask.
    ///
    /// A stage bit whose feature is not enabled may not appear in a mask at all: the ray tracing stage is
    /// `VUID-VkImageMemoryBarrier2-dstStageMask-07946`, and the geometry, tessellation and mesh shader bits
    /// are the same rule with a different feature. The render graph derives its barrier masks from a table of
    /// "every stage that could touch this resource" and filters the result through this.
    ///
    /// The value comes from the create-info chain the caller passed, **not** from a compile-time guess about
    /// what this engine uses: the ray-tracing host and the RT fixture pass the ray-tracing feature structs
    /// while a raster-only host passes none, and a caller may enable another feature itself - the
    /// mesh-shader branch in `RecordEnabledStages` exists for exactly that caller (#265). A constant would be
    /// wrong for one of them - and wrong in the dangerous direction, reporting a stage unusable on a device
    /// that enabled it.
    ///
    /// @return The stage bits a barrier mask may name on this device.
    VkPipelineStageFlags2 EnabledPipelineStages() const { return m_enabledStages; }

    /// Creates the Vulkan instance and selects a physical device.
    ///
    /// Nothing is appended to either list. Validation is enabled only when `vLayers` names
    /// `VK_LAYER_KHRONOS_validation`, and no debug messenger is created - call
    /// `InitializeDebugMessenger` for that. This is deliberate: a library client must be able to link
    /// `muyo_device` without paying for a validation layer it did not ask for (#58). This repository's
    /// hosts select validation through `Muyo::InitializeDeviceWithValidation`.
    ///
    /// @param vExtensions Instance extensions to enable. Each must be reported by the loader.
    /// @param vLayers Instance layers to enable. Each must be reported by the loader.
    /// @throws std::runtime_error if a requested layer or extension is unavailable, the loader is older
    ///         than Vulkan 1.4, or no physical device is enumerated. Thrown by `FatalError`, so it is
    ///         reachable in every build configuration.
    /// @note Must be called before `CreateDevice`. Calling it on an initialized device throws rather than
    ///       replacing the instance: the caller may own instance children this class cannot see (a
    ///       surface, for example), so destroying the parent would be invalid usage. Call `Unintialize`
    ///       first.
    virtual void Initialize(const std::vector<const char*>& vExtensions, const std::vector<const char*>& vLayers);

    /// Destroys the debug messenger, if one was created, and then the instance. Idempotent.
    ///
    /// @pre `DestroyDevice` has run: a live logical device is a child of the instance and destroying the
    ///      parent first is invalid usage.
    /// @throws std::runtime_error if called while `GetDevice()` is non-null.
    /// @note A host that called `InitializeDebugMessenger` does not have to call
    ///       `DestroyDebugMessenger` separately: this does it before destroying the instance.
    virtual void Unintialize();

    /// Creates the debug-utils messenger on this device's instance.
    ///
    /// The device never creates one by itself. This is the hook a host calls after opting in to
    /// `VK_EXT_debug_utils` at `Initialize`, so diagnostics - including the callback that aborts on a
    /// validation error - are the caller's choice rather than a cost imposed by linking the library.
    ///
    /// Preconditions: `Initialize` succeeded, and `VK_EXT_debug_utils` was in the extension list.
    /// Idempotent: a second call returns without creating a second messenger.
    /// @throws std::runtime_error if called before `Initialize`, or if the extension was not enabled.
    /// @note Aborts via `VK_ASSERT` if the loader does not provide `vkCreateDebugUtilsMessengerEXT` or
    ///       the call fails; a missing messenger would silently discard validation messages.
    void InitializeDebugMessenger();

    /// Destroys the messenger installed by `InitializeDebugMessenger`. Idempotent.
    void DestroyDebugMessenger();

    /// Creates the logical device, its queues, and records which pipeline stages the feature chain enables.
    ///
    /// Every requested device extension and every required core feature is verified before
    /// `vkCreateDevice`, so a missing one fails here naming it instead of surfacing as a bare
    /// `VkResult` later. The device-level extension entry points are then resolved against the new
    /// device.
    ///
    /// @param extensions Device extensions to enable; each is checked against the physical device.
    /// @param layers Ignored, and a warning is printed when non-empty: device layers were removed in
    ///        Vulkan 1.0. Validation is an instance layer, handled by `Initialize`.
    /// @param pSurface Optional. When null, no present-capable queue family is selected and
    ///        `GetPresentQueue()` stays `VK_NULL_HANDLE`; a headless client does not need a surface.
    /// @param vpFeatures Optional feature structs appended to the chain the device assembles. The chain
    ///        is owned by the caller and must outlive the call; the device owns the heap, cooperative
    ///        matrix and untyped-pointer structs it appends.
    /// @throws std::runtime_error if an extension is unsupported, a required feature is absent, the
    ///         descriptor-heap extensions are requested without each other, or the caller's feature
    ///         chain duplicates a struct the device owns.
    /// @note Aborts via `VK_ASSERT` if `vkCreateDevice` itself returns an error.
    virtual void CreateDevice(const std::vector<const char*>& extensions, const std::vector<const char*>& layers,
                              const VkSurfaceKHR* pSurface = nullptr, const std::vector<void*>& vpFeatures = {});

    /// Waits for the device to drain and destroys it. Idempotent.
    ///
    /// @note The wait is best-effort: a lost device returns an error, which is not asserted, and the
    ///       device is destroyed anyway.
    void DestroyDevice();

    /// Records an image layout transition into `commandBuffer`, or executes it immediately when null.
    ///
    /// @param commandBuffer Destination command buffer, or `VK_NULL_HANDLE` to submit on the immediate
    ///        queue and wait.
    /// @param image Image to transition. Must not be `VK_NULL_HANDLE`.
    /// @param oldLayout Current layout; `VK_IMAGE_LAYOUT_UNDEFINED` discards the contents.
    /// @param newLayout Target layout.
    /// @param nMipCount Mip levels covered by the barrier.
    /// @param nLayerCount Array layers covered by the barrier.
    /// @throws std::invalid_argument for a layout pair this helper does not implement; use
    ///         `ImageResourceBarrier` for anything not in its table.
    void TransitImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout,
                            VkImageLayout newLayout, uint32_t nMipCount = 1, uint32_t nLayerCount = 1);

    /// Creates the command pools `Allocate*CommandBuffer` use. Call after `CreateDevice`.
    /// @note Also creates the compute pool when a compute family exists. Aborts via `VK_ASSERT` on
    ///       pool-creation failure.
    void CreateCommandPools();

    /// Destroys every command pool. Call before `DestroyDevice`; command buffers must be freed first.
    void DestroyCommandPools();

    /// @return The logical device, or `VK_NULL_HANDLE` before `CreateDevice`.
    VkDevice& GetDevice() { return m_device; }
    /// @return The selected physical device, or `VK_NULL_HANDLE` before `Initialize` or after
    ///         `Unintialize` (the instance's child handles are invalid once it is destroyed).
    VkPhysicalDevice& GetPhysicalDevice() { return m_physicalDevice; }
    /// @return The graphics queue, or `VK_NULL_HANDLE` before `CreateDevice`.
    VkQueue& GetGraphicsQueue() { return m_graphicsQueue; }
    /// @return The queue immediate commands are submitted on; currently the graphics queue.
    VkQueue& GetImmediateQueue() { return m_graphicsQueue; }
    /// @return The present queue, or `VK_NULL_HANDLE` when no surface was supplied to `CreateDevice` or the
    ///         device has no present-capable family.
    VkQueue& GetPresentQueue() { return m_presentQueue; }
    /// @return The compute queue, or `VK_NULL_HANDLE` when no compute family exists.
    VkQueue& GetComputeQueue() { return m_computeQueue; }

    /// @return The graphics family index selected by `CreateDevice`.
    uint32_t GetGraphicsQueueFamily() const { return static_cast<uint32_t>(m_queueFamilyIndices.nGraphicsQueueFamily); }
    /// @return The compute family index, or `UINT32_MAX` when none was found: the member is `-1` and is
    ///         cast here. Call `IsComputeQueueDedicated` before treating it as an independent family.
    uint32_t GetComputeQueueFamily() const { return static_cast<uint32_t>(m_queueFamilyIndices.nComputeQueueFamily); }
    /// @return True when a compute-capable family exists and differs from the graphics family.
    ///
    /// False covers both "no compute-capable family" (`GetComputeQueueFamily()` is `UINT32_MAX`) and
    /// "compute shares the graphics family". In either case the async-compute path is not exercised; a
    /// run must state that rather than treat it as a failure (AGENTS.md section 8).
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

    /// @return The Vulkan instance, or `VK_NULL_HANDLE` before `Initialize`.
    VkInstance& GetInstance() { return m_instance; }

    /// Overrides the logical-device handle. For test injection; `CreateDevice` is the normal path.
    void SetDevice(VkDevice device) { m_device = device; }
    /// Overrides the physical-device handle. For test injection; `Initialize` is the normal path.
    void SetPhysicalDevice(VkPhysicalDevice physicalDevice) { m_physicalDevice = physicalDevice; }
    /// Overrides the instance handle. For test injection; `Initialize` is the normal path.
    void SetInstance(VkInstance instance) { m_instance = instance; }

    /// @name Command buffer allocations
    /// Each returns a primary command buffer from the named pool. `CreateCommandPools` must have run.
    /// @note Aborts via `VK_ASSERT` if Vulkan fails to allocate. The matching `Free*` must be called
    ///       before `DestroyCommandPools`.
    ///@{
    /// @return A command buffer from the compute pool.
    VkCommandBuffer AllocateComputeCommandBuffer();
    /// @param[in,out] commandBuffer Buffer to free; set to a released handle on return.
    void FreeComputeCommandBuffer(VkCommandBuffer& commandBuffer);

    /// @return A command buffer for resources that live for the process, from the main pool.
    VkCommandBuffer AllocateStaticPrimaryCommandbuffer();

    /// @param[in,out] commandBuffer Buffer to free; set to a released handle on return.
    void FreeStaticPrimaryCommandbuffer(VkCommandBuffer& commandBuffer);

    /// @return A per-frame command buffer from the reusable pool.
    VkCommandBuffer AllocateReusablePrimaryCommandbuffer();
    /// @param[in,out] commandBuffer Buffer to free; set to a released handle on return.
    void FreeReusablePrimaryCommandbuffer(VkCommandBuffer& commandBuffer);

    /// @return A one-shot command buffer from the transient pool.
    VkCommandBuffer AllocateImmediateCommandBuffer();
    /// @param[in,out] commandBuffer Buffer to free; set to a released handle on return.
    void FreeImmediateCommandBuffer(VkCommandBuffer& commandBuffer);
    ///@}

    /// Records `fImmediateGPUTask` into a one-shot command buffer, submits it and waits for completion.
    ///
    /// @param fImmediateGPUTask Callable taking a `VkCommandBuffer`; it records into the buffer the
    ///        helper begins and ends, and must not begin or end that buffer itself.
    /// @note This is synchronous by design: it calls `vkQueueWaitIdle`, so it is for setup/teardown work,
    ///       not per-frame submission. Submission results are not checked here; use `SubmitCommandBuffers`
    ///       when a failure must be reported.
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

    /// Creates a repeat-addressed, linearly filtered sampler with the engine's defaults.
    /// @return The sampler handle.
    /// @note Aborts via `VK_ASSERT` if `vkCreateSampler` fails.
    VkSampler CreateSampler();

    /// Adds a barrier recorded by `IResourceBarrier::AddToCommandBuffer` to `cmdBuf`.
    /// @param cmdBuf Command buffer being recorded; must be in the recording state.
    /// @param resourceBarrier Barrier implementation to record.
    void AddResourceBarrier(VkCommandBuffer cmdBuf, IResourceBarrier& resourceBarrier);

    /// Submits command buffers with binary semaphores and an optional fence, through sync2.
    ///
    /// @param vCmdBuffers Buffers to submit; may be empty when the submission only chains semaphores.
    /// @param queue Queue to submit on. Must belong to this device.
    /// @param waitSemaphores Binary semaphores to wait on.
    /// @param signalSemaphores Binary semaphores to signal.
    /// @param stageFlags Wait stage per wait semaphore; a missing entry falls back to
    ///        `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT`.
    /// @param signalFence Optional fence signalled by the submission.
    /// @note Aborts via `VK_ASSERT` if `vkQueueSubmit2` fails. The caller owns every handle.
    void SubmitCommandBuffers(std::vector<VkCommandBuffer>& vCmdBuffers, VkQueue queue,
                              std::vector<VkSemaphore>& waitSemaphores, std::vector<VkSemaphore>& signalSemaphores,
                              std::vector<VkPipelineStageFlags2> stageFlags, VkFence signalFence = VK_NULL_HANDLE);

    /// Submits `vCmdBuffers` on the graphics queue, then waits for that queue to go idle.
    /// @param vCmdBuffers Buffers to submit.
    /// @note Synchronous; aborts via `VK_ASSERT` on submission or wait failure.
    void SubmitCommandBuffersAndWait(std::vector<VkCommandBuffer>& vCmdBuffers);

    /// @param buffer Buffer created with `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`.
    /// @return The buffer's device address. A buffer without the usage bit is invalid usage; this
    ///         helper does not verify it.
    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer) const;

    /// Queries chained physical-device properties into `property`.
    ///
    /// @param property A property struct with `sType` set; it is chained into a
    ///        `VkPhysicalDeviceProperties2` and overwritten with the device's values.
    /// @note The `sType != 0` check is an `assert`: it catches a programmer error only reachable by
    ///       editing the call site, and a release build's `vkGetPhysicalDeviceProperties2` would reject
    ///       the chain anyway.
    template <typename VkPropertyType>
    void GetPhysicalDeviceProperties(VkPropertyType& property)
    {
        assert(property.sType != 0);
        VkPhysicalDeviceProperties2 property2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, (void*)&property, {}};
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &property2);
    }

    /// Creates a pipeline layout from explicit set layouts and push-constant ranges.
    /// @param descriptorSetLayouts Set layouts the layout is compatible with; may be empty.
    /// @param pushConstantRanges Push-constant ranges exposed to the pipeline; may be empty.
    /// @return The layout. Destroy it with `vkDestroyPipelineLayout` once no pipeline uses it.
    /// @note Aborts via `VK_ASSERT` if `vkCreatePipelineLayout` fails.
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
    /// Walk the create-info feature chain and record which pipeline stages it enables.
    ///
    /// @param pFeatureChain The `VkPhysicalDeviceFeatures2` chain `CreateDevice` assembled, or null.
    void RecordEnabledStages(const void* pFeatureChain);
    /// @param sName Instance extension name.
    /// @return True when `Initialize` was asked to enable it.
    bool IsInstanceExtensionEnabled(const char* sName) const;
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
    /// Name of the device this process first picked. A re-created instance re-finds it by name, so a
    /// re-initialisation cannot silently move to a different device mid-process (#141).
    std::string m_pinnedPhysicalDeviceName;

    std::array<VkCommandPool, NUM_CMD_POOLS> m_aCommandPools;

    /// The instance layers and extensions this device was asked for, kept as owned strings so a host's
    /// temporary list cannot dangle. `m_bIsValidationLayerEnabled` is derived once, at `Initialize`.
    std::vector<std::string> m_vInstanceLayers;
    std::vector<std::string> m_vInstanceExtensions;
    bool m_bIsValidationLayerEnabled = false;

    /// Installed only when the host calls `InitializeDebugMessenger`; the device never creates one itself.
    DebugUtilsMessenger m_debugMessenger;

    /// Cooperative matrix, queried at device creation whether or not it is requested, so a consumer can
    /// ask before deciding and the answer appears in the log.
    bool m_bCooperativeMatrixSupported = false;
    std::vector<VkCooperativeMatrixPropertiesKHR> m_vCooperativeMatrixProperties;
    /// Owned by the device rather than by a caller, because it must outlive `vkCreateDevice`, and chained
    /// only when the caller requests the extension - the renderer does not use cooperative matrix and must
    /// not start requiring it (AGENTS.md section 3: verify, never enable blindly).
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR m_cooperativeMatrixFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    /// Stage bits the assembled feature chain enables; see `EnabledPipelineStages()`.
    VkPipelineStageFlags2 m_enabledStages = ~static_cast<VkPipelineStageFlags2>(0);

    /// Descriptor-heap support, queried at device creation whether or not the caller requests it, so the
    /// `[test device]` line can record an unsupported row instead of leaving it implied. The feature
    /// structs are owned by the device because they must outlive `vkCreateDevice`, and they are appended
    /// to the chain only when the caller requested both extensions.
    bool m_bDescriptorHeapSupported = false;
    bool m_bDescriptorHeapEnabled = false;
    bool m_bShaderUntypedPointersSupported = false;
    bool m_bShaderUntypedPointersEnabled = false;
    VkPhysicalDeviceDescriptorHeapPropertiesEXT m_descriptorHeapProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT};
    VkPhysicalDeviceDescriptorHeapFeaturesEXT m_descriptorHeapFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT};
    VkPhysicalDeviceShaderUntypedPointersFeaturesKHR m_shaderUntypedPointersFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR};

    /// The loader's supported instance version, recorded by `Initialize`.
    uint32_t m_uInstanceVersion = 0;

protected:
    VkInstance m_instance = VK_NULL_HANDLE;
};

/// @return The process-wide device singleton. It applies no validation or debug-messenger policy: call
///         `Initialize` with the layers and extensions the host wants, and `InitializeDebugMessenger` if
///         it wants the callback (#58).
///
/// A library client that needs more than one device, or wants ownership of the lifetime, constructs its
/// own `VkRenderDevice`; the singleton is a convenience for the engine's hosts and must not be required
/// by the device layer's own logic.
VkRenderDevice* GetRenderDevice();
}  // namespace Muyo
