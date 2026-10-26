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
    uint32_t index = 0;
    uint32_t generation = 0;
    uint32_t region = 0;
    uint32_t ring = 0;
    bool bRing = false;
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
        uint32_t imageSlots = 256;
        uint32_t bufferSlots = 256;
        uint32_t accelerationSlots = 16;
        uint32_t samplerSlots = 64;
        uint32_t ringSlots = 32;
        uint32_t inFlightCount = 2;
    };

    DescriptorHeapService() = default;
    ~DescriptorHeapService();

    DescriptorHeapService(const DescriptorHeapService&) = delete;
    DescriptorHeapService& operator=(const DescriptorHeapService&) = delete;

    /// Create the heaps. Requires a device with the descriptor-heap feature enabled.
    void Initialize(VkRenderDevice* device) { Initialize(device, Config{}); }
    void Initialize(VkRenderDevice* device, const Config& config);
    /// Destroy the heaps and any registered custom-border indices. Safe to call repeatedly.
    void Shutdown();
    bool IsInitialized() const { return m_device != nullptr; }

    DescriptorSlot AcquireImage(const VkImageViewCreateInfo& view, VkImageLayout layout,
                                VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bool bRing = false);
    DescriptorSlot AcquireTexelBuffer(VkFormat format, VkDeviceAddress address, VkDeviceSize size,
                                      VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                                      bool bRing = false);
    DescriptorSlot AcquireBuffer(VkDeviceAddress address, VkDeviceSize size,
                                 VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bool bRing = false);
    DescriptorSlot AcquireAccelerationStructure(VkDeviceAddress address, bool bRing = false);
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

    /// Select the execution slot that subsequent ring acquisitions use.
    void BeginExecution(uint32_t slot);
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

    const VkPhysicalDeviceDescriptorHeapPropertiesEXT& GetProperties() const { return m_properties; }
    VkDeviceSize GetResourceHeapSize() const { return m_resourceSize; }
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
