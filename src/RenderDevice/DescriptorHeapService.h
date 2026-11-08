#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Muyo
{
class VkRenderDevice;

/// Identifies one descriptor in a heap region.
///
/// The generation is checked by `IndexOf` so a slot retired and reused cannot be read through a stale
/// handle in debug (and, cheaply enough, in release too). `index` is the value a shader uses with
/// `.Handle(uint2(index, 0))`; it is the byte offset divided by the region's shader-visible stride.
struct DescriptorSlot
{
    uint32_t index = 0;       ///< Shader-visible descriptor index within `region`.
    uint32_t generation = 0;  ///< Bumped whenever the slot is reused; `IndexOf` rejects a stale value.
    uint32_t region = 0;      ///< Region id the index belongs to; a slot is only valid in its own region.
    uint32_t ring = 0;        ///< Execution slot that owns a ring allocation; unused for static slots.
    bool bRing = false;       ///< True when the slot is released by the execution reset rather than `Release`.
};

/// Descriptor heap allocation, writes and lifetime (B1a).
///
/// One resource heap and one sampler heap, split into typed regions: images and texel buffers in the
/// image-sized region, buffers in the buffer-sized region, acceleration structures in their own
/// per-type region, and samplers in the sampler heap. Each region has a static free-list and a ring per
/// in-flight execution. The service writes through host staging and copies the staging buffer into the
/// device-local heaps in `Upload`, then binds both heaps in `Bind`.
///
/// It deliberately knows nothing about RenderGraph: B1b injects it through `RenderGraphServices`.
class DescriptorHeapService
{
public:
    struct Config
    {
        uint32_t imageSlots = 256;        ///< Static slots for images and texel buffers.
        uint32_t bufferSlots = 256;       ///< Static slots for uniform and storage buffers.
        uint32_t accelerationSlots = 16;  ///< Static slots for acceleration structures.
        uint32_t samplerSlots = 64;       ///< Slots in the sampler heap.
        uint32_t ringSlots = 32;          ///< Slots reserved per execution slot for ring allocations.
        uint32_t inFlightCount = 2;       ///< Execution slots; ring cursors and retires are per slot.
    };

    DescriptorHeapService() = default;

    /// Shuts the heaps down, releasing any descriptors still registered.
    ~DescriptorHeapService();

    /// Copying would alias the heap handles and the staging buffers.
    DescriptorHeapService(const DescriptorHeapService&) = delete;
    /// Copying would alias the heap handles and the staging buffers.
    DescriptorHeapService& operator=(const DescriptorHeapService&) = delete;

    /// Create the heaps with the default capacities.
    /// @param device Device with `VK_EXT_descriptor_heap` and `descriptorHeap` enabled; must outlive this
    ///        service.
    /// @note Aborts via `VK_ASSERT` / throws when the device lacks the feature; see `Initialize(device,
    ///       config)`.
    void Initialize(VkRenderDevice* device) { Initialize(device, Config{}); }

    /// Create the heaps with explicit capacities.
    /// @param device Device with `VK_EXT_descriptor_heap` and `descriptorHeap` enabled; must outlive this
    ///        service.
    /// @param config Region and ring sizes. `inFlightCount` must be at least 1.
    /// @throws std::runtime_error if the device is null, the feature is not enabled, or the requested
    ///         sizes exceed the implementation's heap maxima.
    /// @note Aborts via `VK_ASSERT` if a Vulkan allocation fails.
    void Initialize(VkRenderDevice* device, const Config& config);

    /// Destroy the heaps and any registered custom-border indices. Safe to call repeatedly.
    void Shutdown();

    /// @return True after a successful `Initialize` and before `Shutdown`.
    bool IsInitialized() const { return m_device != nullptr; }
    /// @return How many execution slots the rings and staging layout were configured for.
    uint32_t GetInFlightCount() const { return m_inFlightCount; }

    /// Register an image view descriptor.
    /// @param view View description; its image view must outlive the slot.
    /// @param layout Layout the image is in when the shader reads it.
    /// @param type Sampled or storage image.
    /// @param bRing Allocate from the region's ring instead of the static free-list.
    /// @return The slot. `IndexOf` gives the shader-visible index.
    /// @throws std::runtime_error if no slot is free or the view info is unsupported.
    DescriptorSlot AcquireImage(const VkImageViewCreateInfo& view, VkImageLayout layout,
                                VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bool bRing = false);

    /// Register a texel-buffer descriptor by device address.
    /// @param format Texel format.
    /// @param address Buffer device address.
    /// @param size Buffer size in bytes.
    /// @param type Uniform or storage texel buffer.
    /// @param bRing Allocate from the ring.
    /// @return The slot.
    DescriptorSlot AcquireTexelBuffer(VkFormat format, VkDeviceAddress address, VkDeviceSize size,
                                      VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                                      bool bRing = false);

    /// Register a buffer descriptor by device address.
    /// @param address Buffer device address.
    /// @param size Buffer size in bytes.
    /// @param type Uniform or storage buffer.
    /// @param bRing Allocate from the ring.
    /// @return The slot.
    DescriptorSlot AcquireBuffer(VkDeviceAddress address, VkDeviceSize size,
                                 VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bool bRing = false);

    /// Register an acceleration-structure descriptor.
    /// @param address Acceleration-structure device address.
    /// @param bRing Allocate from the ring.
    /// @return The slot.
    DescriptorSlot AcquireAccelerationStructure(VkDeviceAddress address, bool bRing = false);

    /// Register a sampler descriptor.
    /// @param createInfo Sampler description, applied when the slot is written.
    /// @param bRing Allocate from the ring.
    /// @return The slot.
    DescriptorSlot AcquireSampler(const VkSamplerCreateInfo& createInfo, bool bRing = false);

    /// Raw form used by the B0 probe: the type selects the region and the host write size, and `info`
    /// carries the already-built image/texel/buffer/AS payload.
    DescriptorSlot AcquireResource(VkDescriptorType type, const VkResourceDescriptorInfoEXT& info, bool bRing = false);

    /// @return The shader-visible heap index for `slot`.
    /// @throws std::runtime_error if the slot is stale, out of range or from another region.
    uint32_t IndexOf(DescriptorSlot slot) const;

    /// Retire a static slot after `timelineValue`; the slot returns to the free-list when
    /// `ResetExecution` observes a completed value at least this large.
    void Retire(DescriptorSlot slot, uint64_t timelineValue);
    /// Release a static slot immediately. Only safe once every execution that recorded with it has
    /// completed; a slot whose content is still needed by in-flight work must be `Retire`d instead.
    /// Ring slots are released by the execution reset and are ignored here.
    /// @param slot Slot returned by an Acquire* call.
    void Release(DescriptorSlot slot)
    {
        if (!slot.bRing) ReleaseStaticSlot(slot);
    }

    /// Select the execution slot that subsequent ring acquisitions use.
    void BeginExecution(uint32_t slot);
    /// Reset `slot`'s ring cursors without processing retires. Only safe once every execution that used
    /// the slot has completed (the graph calls it after AcquireSlot waited for that slot).
    /// @param slot Execution slot whose rings are recycled.
    void ResetRing(uint32_t slot);
    /// Reset the ring for `slot` and release retired static slots whose timeline has completed.
    void ResetExecution(uint32_t slot, uint64_t completedTimeline);

    /// Copy host-written descriptors into the device-local heaps, make them visible to heap reads and
    /// bind both heaps. Safe to call once per command buffer; a later call re-copies the staging buffers.
    void Upload(VkCommandBuffer commandBuffer);
    /// Bind both heaps without uploading (used after a previous `Upload` on the same command buffer).
    void Bind(VkCommandBuffer commandBuffer) const;

    /// @return True when the implementation's AS descriptor is 8 bytes, matching Slang's direct
    ///         `RaytracingAccelerationStructure.Handle` representation. False means B1b must map or
    ///         reject AS on this implementation.
    bool CanUseDirectAccelerationStructure() const { return m_accelerationStride == 8; }

    /// @return The queried heap properties. Read sizes from the implementation rather than assuming
    ///         them; a zeroed struct means `Initialize` has not run or the extension is absent.
    const VkPhysicalDeviceDescriptorHeapPropertiesEXT& GetProperties() const { return m_properties; }
    /// @return The allocated resource heap size in bytes.
    VkDeviceSize GetResourceHeapSize() const { return m_resourceSize; }
    /// @return The allocated sampler heap size in bytes.
    VkDeviceSize GetSamplerHeapSize() const { return m_samplerSize; }

private:
    enum RegionId : uint32_t
    {
        REGION_IMAGE = 0,
        REGION_BUFFER = 1,
        REGION_ACCELERATION = 2,
        REGION_SAMPLER = 3,
        REGION_COUNT = 4,
    };

    struct Region
    {
        VkDeviceSize offset = 0;
        VkDeviceSize stride = 0;
        uint32_t staticSlots = 0;
        uint32_t ringSlots = 0;
        uint32_t cursor = 0;
        std::vector<uint32_t> freeSlots;
        std::vector<uint32_t> generations;
        std::vector<uint32_t> ringCursor;
    };

    struct Heap
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void* stagingMap = nullptr;
        VkDeviceAddress base = 0;
        VkDeviceSize baseOffset = 0;
        VkDeviceSize size = 0;
        VkDeviceSize reserved = 0;
        VkDeviceSize alignment = 1;
    };

    struct SamplerEntry
    {
        VkSamplerCreateInfo createInfo{};
        DescriptorSlot slot{};
        uint32_t customBorderIndex = 0;
        bool bHasCustomBorder = false;
    };

    struct PendingRetire
    {
        DescriptorSlot slot{};
        uint64_t timeline = 0;
    };

    DescriptorSlot Acquire(VkDescriptorType type, const VkResourceDescriptorInfoEXT& info, bool bRing);
    DescriptorSlot AllocateSlot(RegionId region, bool bRing);
    VkDeviceSize HostDescriptorSize(VkDescriptorType type) const;
    VkDeviceSize RegionStride(RegionId region) const;
    Region& GetRegion(RegionId region);
    const Region& GetRegion(RegionId region) const;
    void WriteResource(DescriptorSlot slot, VkDescriptorType type, const VkResourceDescriptorInfoEXT& info);
    void WriteSampler(DescriptorSlot slot, const VkSamplerCreateInfo& createInfo);
    void CreateHeap(Heap& heap, VkDeviceSize size, VkDeviceSize alignment, VkBufferUsageFlags usage);
    void DestroyHeap(Heap& heap);
    bool SamplerMatches(const SamplerEntry& entry, const VkSamplerCreateInfo& createInfo) const;
    bool IsCustomBorderColor(VkBorderColor color) const;
    void ReleaseStaticSlot(DescriptorSlot slot);

    VkRenderDevice* m_device = nullptr;
    VkPhysicalDeviceDescriptorHeapPropertiesEXT m_properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT};
    Region m_regions[REGION_COUNT];
    Heap m_resourceHeap;
    Heap m_samplerHeap;
    VkDeviceSize m_resourceSize = 0;
    VkDeviceSize m_samplerSize = 0;
    VkDeviceSize m_imageStride = 0;
    VkDeviceSize m_bufferStride = 0;
    VkDeviceSize m_accelerationStride = 0;
    VkDeviceSize m_samplerStride = 0;
    uint32_t m_inFlightCount = 0;
    uint32_t m_currentExecution = 0;
    bool m_bHasExecution = false;
    std::vector<SamplerEntry> m_samplers;
    std::vector<PendingRetire> m_pendingRetires;
    std::vector<uint32_t> m_customBorderIndices;
};
}  // namespace Muyo
