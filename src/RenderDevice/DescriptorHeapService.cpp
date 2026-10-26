#include "DescriptorHeapService.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "Debug.h"
#include "VkExtFuncsLoader.h"
#include "VkRenderDevice.h"

namespace Muyo
{
namespace
{
VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    if (alignment == 0) return value;
    return (value + alignment - 1) / alignment * alignment;
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeBits & (1U << i)) != 0 && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("DescriptorHeapService: no memory type satisfies the requested properties");
}

bool IsImageRegionType(VkDescriptorType type)
{
    return type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
           type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
}

bool IsBufferRegionType(VkDescriptorType type)
{
    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
}

bool IsCustomBorder(VkBorderColor color)
{
    return color == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT || color == VK_BORDER_COLOR_INT_CUSTOM_EXT;
}
}  // namespace

DescriptorHeapService::~DescriptorHeapService() { Shutdown(); }

void DescriptorHeapService::Initialize(VkRenderDevice* device, const Config& config)
{
    Shutdown();
    if (device == nullptr) throw std::invalid_argument("DescriptorHeapService: device is null");
    if (!device->IsDescriptorHeapEnabled())
    {
        throw std::runtime_error("DescriptorHeapService: the device was not created with the descriptor heap");
    }
    if (VkExt::vkWriteResourceDescriptorsEXT == nullptr || VkExt::vkWriteSamplerDescriptorsEXT == nullptr ||
        VkExt::vkCmdBindResourceHeapEXT == nullptr || VkExt::vkCmdBindSamplerHeapEXT == nullptr)
    {
        throw std::runtime_error("DescriptorHeapService: descriptor heap entry points are not loaded");
    }

    m_device = device;
    m_properties = device->GetDescriptorHeapProperties();
    m_inFlightCount = std::max(config.inFlightCount, 1U);

    m_imageStride = std::max(m_properties.imageDescriptorSize, m_properties.imageDescriptorAlignment);
    m_bufferStride = std::max(m_properties.bufferDescriptorSize, m_properties.bufferDescriptorAlignment);
    m_samplerStride = std::max(m_properties.samplerDescriptorSize, m_properties.samplerDescriptorAlignment);
    const VkDeviceSize accelerationSize = device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
    m_accelerationStride = std::max(accelerationSize, m_properties.bufferDescriptorAlignment);
    for (VkDeviceSize stride : {m_imageStride, m_bufferStride, m_samplerStride, m_accelerationStride})
    {
        if (stride == 0)
        {
            throw std::runtime_error("DescriptorHeapService: an implementation descriptor size is zero");
        }
    }

    const auto configureRegion =
        [](Region& region, VkDeviceSize stride, uint32_t staticSlots, uint32_t ringSlots, uint32_t inFlight)
    {
        region.stride = stride;
        region.staticSlots = staticSlots;
        region.ringSlots = ringSlots;
        region.cursor = 0;
        region.freeSlots.clear();
        region.generations.assign(staticSlots, 0);
        region.ringCursor.assign(inFlight, 0);
    };
    configureRegion(m_regions[REGION_IMAGE], m_imageStride, config.imageSlots, config.ringSlots, m_inFlightCount);
    configureRegion(m_regions[REGION_BUFFER], m_bufferStride, config.bufferSlots, config.ringSlots, m_inFlightCount);
    configureRegion(m_regions[REGION_ACCELERATION], m_accelerationStride, config.accelerationSlots, config.ringSlots,
                    m_inFlightCount);
    configureRegion(m_regions[REGION_SAMPLER], m_samplerStride, config.samplerSlots, config.ringSlots, m_inFlightCount);

    const auto bytesFor = [this](const Region& region)
    { return region.stride * (region.staticSlots + region.ringSlots * m_inFlightCount); };
    VkDeviceSize resourceUsed = 0;
    for (RegionId id : {REGION_IMAGE, REGION_BUFFER, REGION_ACCELERATION})
    {
        Region& region = m_regions[id];
        resourceUsed = AlignUp(resourceUsed, region.stride);
        region.offset = resourceUsed;
        resourceUsed += bytesFor(region);
    }
    m_resourceHeap.reserved = m_properties.minResourceHeapReservedRange;
    m_resourceHeap.alignment = std::max(m_properties.resourceHeapAlignment, static_cast<VkDeviceSize>(1));
    m_resourceSize = AlignUp(resourceUsed + m_resourceHeap.reserved, m_resourceHeap.alignment);
    if (m_properties.maxResourceHeapSize != 0 && m_resourceSize > m_properties.maxResourceHeapSize)
    {
        throw std::runtime_error("DescriptorHeapService: resource heap requirement exceeds maxResourceHeapSize");
    }

    Region& samplerRegion = m_regions[REGION_SAMPLER];
    samplerRegion.offset = 0;
    m_samplerHeap.reserved = m_properties.minSamplerHeapReservedRange;
    m_samplerHeap.alignment = std::max(m_properties.samplerHeapAlignment, static_cast<VkDeviceSize>(1));
    m_samplerSize = AlignUp(bytesFor(samplerRegion) + m_samplerHeap.reserved, m_samplerHeap.alignment);
    if (m_properties.maxSamplerHeapSize != 0 && m_samplerSize > m_properties.maxSamplerHeapSize)
    {
        throw std::runtime_error("DescriptorHeapService: sampler heap requirement exceeds maxSamplerHeapSize");
    }

    CreateHeap(m_resourceHeap, m_resourceSize, m_resourceHeap.alignment, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT);
    CreateHeap(m_samplerHeap, m_samplerSize, m_samplerHeap.alignment, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT);
}

void DescriptorHeapService::Shutdown()
{
    if (m_device != nullptr)
    {
        for (uint32_t index : m_customBorderIndices)
        {
            if (VkExt::vkUnregisterCustomBorderColorEXT != nullptr)
            {
                VkExt::vkUnregisterCustomBorderColorEXT(m_device->GetDevice(), index);
            }
        }
    }
    m_customBorderIndices.clear();
    m_samplers.clear();
    m_pendingRetires.clear();
    m_currentExecution = 0;
    m_bHasExecution = false;
    DestroyHeap(m_resourceHeap);
    DestroyHeap(m_samplerHeap);
    m_device = nullptr;
    m_resourceSize = 0;
    m_samplerSize = 0;
}

void DescriptorHeapService::CreateHeap(Heap& heap, VkDeviceSize size, VkDeviceSize alignment, VkBufferUsageFlags usage)
{
    heap.size = size;
    heap.alignment = alignment;
    const VkDeviceSize allocationSize = size + alignment;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = allocationSize;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_ASSERT(vkCreateBuffer(m_device->GetDevice(), &bufferInfo, nullptr, &heap.buffer));

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device->GetDevice(), heap.buffer, &requirements);
    VkMemoryAllocateFlagsInfo allocateFlags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.pNext = &allocateFlags;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        FindMemoryType(m_device->GetPhysicalDevice(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_ASSERT(vkAllocateMemory(m_device->GetDevice(), &allocateInfo, nullptr, &heap.memory));
    VK_ASSERT(vkBindBufferMemory(m_device->GetDevice(), heap.buffer, heap.memory, 0));
    heap.base = AlignUp(m_device->GetBufferDeviceAddress(heap.buffer), alignment);
    heap.baseOffset = heap.base - m_device->GetBufferDeviceAddress(heap.buffer);
    if (heap.baseOffset + size > allocationSize)
    {
        throw std::runtime_error("DescriptorHeapService: aligned heap range does not fit its allocation");
    }

    VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingInfo.size = allocationSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_ASSERT(vkCreateBuffer(m_device->GetDevice(), &stagingInfo, nullptr, &heap.staging));
    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(m_device->GetDevice(), heap.staging, &stagingRequirements);
    VkMemoryAllocateInfo stagingAllocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    stagingAllocate.allocationSize = stagingRequirements.size;
    stagingAllocate.memoryTypeIndex =
        FindMemoryType(m_device->GetPhysicalDevice(), stagingRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_ASSERT(vkAllocateMemory(m_device->GetDevice(), &stagingAllocate, nullptr, &heap.stagingMemory));
    VK_ASSERT(vkBindBufferMemory(m_device->GetDevice(), heap.staging, heap.stagingMemory, 0));
    VK_ASSERT(vkMapMemory(m_device->GetDevice(), heap.stagingMemory, 0, VK_WHOLE_SIZE, 0, &heap.stagingMap));
    std::memset(heap.stagingMap, 0, static_cast<size_t>(allocationSize));
}

void DescriptorHeapService::DestroyHeap(Heap& heap)
{
    if (m_device != nullptr)
    {
        if (heap.stagingMap != nullptr) vkUnmapMemory(m_device->GetDevice(), heap.stagingMemory);
        if (heap.staging != VK_NULL_HANDLE) vkDestroyBuffer(m_device->GetDevice(), heap.staging, nullptr);
        if (heap.stagingMemory != VK_NULL_HANDLE) vkFreeMemory(m_device->GetDevice(), heap.stagingMemory, nullptr);
        if (heap.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device->GetDevice(), heap.buffer, nullptr);
        if (heap.memory != VK_NULL_HANDLE) vkFreeMemory(m_device->GetDevice(), heap.memory, nullptr);
    }
    heap = {};
}

DescriptorHeapService::Region& DescriptorHeapService::GetRegion(RegionId region) { return m_regions[region]; }
const DescriptorHeapService::Region& DescriptorHeapService::GetRegion(RegionId region) const
{
    return m_regions[region];
}

VkDeviceSize DescriptorHeapService::RegionStride(RegionId region) const { return m_regions[region].stride; }

VkDeviceSize DescriptorHeapService::HostDescriptorSize(VkDescriptorType type) const
{
    const VkDeviceSize size = m_device->GetDescriptorSize(type);
    if (size == 0) throw std::runtime_error("DescriptorHeapService: descriptor size is zero for the requested type");
    return size;
}

DescriptorSlot DescriptorHeapService::AllocateSlot(RegionId regionId, bool bRing)
{
    Region& region = GetRegion(regionId);
    DescriptorSlot slot;
    slot.region = regionId;
    slot.bRing = bRing;
    VkDeviceSize offset = 0;
    if (bRing)
    {
        if (!m_bHasExecution || m_currentExecution >= m_inFlightCount)
        {
            throw std::runtime_error("DescriptorHeapService: ring acquisition without BeginExecution(slot)");
        }
        uint32_t& cursor = region.ringCursor[m_currentExecution];
        if (cursor >= region.ringSlots)
        {
            throw std::runtime_error("DescriptorHeapService: execution ring exhausted");
        }
        slot.ring = m_currentExecution;
        offset = region.offset + (region.staticSlots + m_currentExecution * region.ringSlots + cursor) * region.stride;
        ++cursor;
        slot.generation = 0;
    }
    else
    {
        uint32_t local = 0;
        if (!region.freeSlots.empty())
        {
            local = region.freeSlots.back();
            region.freeSlots.pop_back();
        }
        else if (region.cursor < region.staticSlots)
        {
            local = region.cursor++;
        }
        else
        {
            throw std::runtime_error("DescriptorHeapService: static descriptor region exhausted");
        }
        slot.generation = region.generations[local];
        offset = region.offset + local * region.stride;
    }
    slot.index = static_cast<uint32_t>(offset / region.stride);
    return slot;
}

void DescriptorHeapService::WriteResource(DescriptorSlot slot, VkDescriptorType type,
                                          const VkResourceDescriptorInfoEXT& info)
{
    const VkDeviceSize hostSize = HostDescriptorSize(type);
    const VkDeviceSize offset =
        static_cast<VkDeviceSize>(slot.index) * RegionStride(static_cast<RegionId>(slot.region));
    auto* address = static_cast<uint8_t*>(m_resourceHeap.stagingMap) + m_resourceHeap.baseOffset + offset;
    VkHostAddressRangeEXT range{};
    range.address = address;
    range.size = static_cast<size_t>(hostSize);
    VK_ASSERT(VkExt::vkWriteResourceDescriptorsEXT(m_device->GetDevice(), 1, &info, &range));
}

DescriptorSlot DescriptorHeapService::AcquireResource(VkDescriptorType type, const VkResourceDescriptorInfoEXT& info,
                                                      bool bRing)
{
    if (!IsInitialized()) throw std::runtime_error("DescriptorHeapService: not initialized");
    RegionId region = REGION_BUFFER;
    if (IsImageRegionType(type))
        region = REGION_IMAGE;
    else if (type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
        region = REGION_ACCELERATION;
    else if (!IsBufferRegionType(type))
        throw std::runtime_error("DescriptorHeapService: unsupported descriptor type");
    const DescriptorSlot slot = AllocateSlot(region, bRing);
    WriteResource(slot, type, info);
    return slot;
}

DescriptorSlot DescriptorHeapService::AcquireImage(const VkImageViewCreateInfo& view, VkImageLayout layout,
                                                   VkDescriptorType type, bool bRing)
{
    VkImageDescriptorInfoEXT imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
    imageInfo.pView = &view;
    imageInfo.layout = layout;
    VkResourceDescriptorInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    info.type = type;
    info.data.pImage = &imageInfo;
    return AcquireResource(type, info, bRing);
}

DescriptorSlot DescriptorHeapService::AcquireTexelBuffer(VkFormat format, VkDeviceAddress address, VkDeviceSize size,
                                                         VkDescriptorType type, bool bRing)
{
    VkTexelBufferDescriptorInfoEXT texelInfo{};
    texelInfo.sType = VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT;
    texelInfo.format = format;
    texelInfo.addressRange.address = address;
    texelInfo.addressRange.size = size;
    VkResourceDescriptorInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    info.type = type;
    info.data.pTexelBuffer = &texelInfo;
    return AcquireResource(type, info, bRing);
}

DescriptorSlot DescriptorHeapService::AcquireBuffer(VkDeviceAddress address, VkDeviceSize size, VkDescriptorType type,
                                                    bool bRing)
{
    VkDeviceAddressRangeEXT range{};
    range.address = address;
    range.size = size;
    VkResourceDescriptorInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    info.type = type;
    info.data.pAddressRange = &range;
    return AcquireResource(type, info, bRing);
}

DescriptorSlot DescriptorHeapService::AcquireAccelerationStructure(VkDeviceAddress address, bool bRing)
{
    VkDeviceAddressRangeEXT range{};
    range.address = address;
    range.size = 0;
    VkResourceDescriptorInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    info.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    info.data.pAddressRange = &range;
    return AcquireResource(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, info, bRing);
}

bool DescriptorHeapService::IsCustomBorderColor(VkBorderColor color) const { return IsCustomBorder(color); }

bool DescriptorHeapService::SamplerMatches(const SamplerEntry& entry, const VkSamplerCreateInfo& createInfo) const
{
    return entry.createInfo.magFilter == createInfo.magFilter && entry.createInfo.minFilter == createInfo.minFilter &&
           entry.createInfo.mipmapMode == createInfo.mipmapMode &&
           entry.createInfo.addressModeU == createInfo.addressModeU &&
           entry.createInfo.addressModeV == createInfo.addressModeV &&
           entry.createInfo.addressModeW == createInfo.addressModeW &&
           entry.createInfo.mipLodBias == createInfo.mipLodBias &&
           entry.createInfo.anisotropyEnable == createInfo.anisotropyEnable &&
           entry.createInfo.maxAnisotropy == createInfo.maxAnisotropy &&
           entry.createInfo.compareEnable == createInfo.compareEnable &&
           entry.createInfo.compareOp == createInfo.compareOp && entry.createInfo.minLod == createInfo.minLod &&
           entry.createInfo.maxLod == createInfo.maxLod && entry.createInfo.borderColor == createInfo.borderColor &&
           entry.createInfo.unnormalizedCoordinates == createInfo.unnormalizedCoordinates;
}

DescriptorSlot DescriptorHeapService::AcquireSampler(const VkSamplerCreateInfo& createInfo, bool bRing)
{
    if (!IsInitialized()) throw std::runtime_error("DescriptorHeapService: not initialized");
    for (const auto* pHeader = static_cast<const VkBaseInStructure*>(createInfo.pNext); pHeader != nullptr;
         pHeader = pHeader->pNext)
    {
        if (pHeader->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO)
        {
            throw std::runtime_error("DescriptorHeapService: YCbCr/subsampled samplers require the mapping path");
        }
    }
    // Only a sampler with no pNext is cacheable: the field compare cannot see extension create-infos,
    // and storing the caller's pNext would leave a dangling pointer in the entry. Custom-border values
    // also cannot be compared here, so that path is outside the cache.
    const bool bCacheable = !bRing && createInfo.pNext == nullptr && !IsCustomBorderColor(createInfo.borderColor);
    if (bCacheable)
    {
        for (const SamplerEntry& entry : m_samplers)
        {
            if (SamplerMatches(entry, createInfo)) return entry.slot;
        }
    }

    VkSamplerCustomBorderColorIndexCreateInfoEXT indexInfo{
        VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_INDEX_CREATE_INFO_EXT};
    VkSamplerCreateInfo effective = createInfo;
    uint32_t customBorderIndex = 0;
    const bool bCustom = IsCustomBorderColor(createInfo.borderColor);
    if (bCustom && bRing)
    {
        throw std::runtime_error(
            "DescriptorHeapService: ring custom-border samplers are deferred to the B1b sampler cache (#222)");
    }
    if (bCustom)
    {
        if (VkExt::vkRegisterCustomBorderColorEXT == nullptr || VkExt::vkUnregisterCustomBorderColorEXT == nullptr)
        {
            throw std::runtime_error(
                "DescriptorHeapService: custom-border samplers require VK_EXT_custom_border_color");
        }
        const VkSamplerCustomBorderColorCreateInfoEXT* pCustom = nullptr;
        for (const auto* pHeader = static_cast<const VkBaseInStructure*>(createInfo.pNext); pHeader != nullptr;
             pHeader = pHeader->pNext)
        {
            if (pHeader->sType == VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT)
            {
                pCustom = reinterpret_cast<const VkSamplerCustomBorderColorCreateInfoEXT*>(pHeader);
                break;
            }
        }
        if (pCustom == nullptr)
        {
            throw std::runtime_error("DescriptorHeapService: custom-border sampler has no custom-border create info");
        }
        VK_ASSERT(VkExt::vkRegisterCustomBorderColorEXT(m_device->GetDevice(), pCustom, VK_TRUE, &customBorderIndex));
        m_customBorderIndices.push_back(customBorderIndex);
        indexInfo.index = customBorderIndex;
        effective.pNext = &indexInfo;
    }

    const DescriptorSlot slot = AllocateSlot(REGION_SAMPLER, bRing);
    const VkDeviceSize hostSize = HostDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER);
    const VkDeviceSize offset = static_cast<VkDeviceSize>(slot.index) * m_samplerStride;
    auto* address = static_cast<uint8_t*>(m_samplerHeap.stagingMap) + m_samplerHeap.baseOffset + offset;
    VkHostAddressRangeEXT range{};
    range.address = address;
    range.size = static_cast<size_t>(hostSize);
    VK_ASSERT(VkExt::vkWriteSamplerDescriptorsEXT(m_device->GetDevice(), 1, &effective, &range));
    if (bCacheable)
    {
        SamplerEntry entry;
        entry.createInfo = createInfo;
        entry.createInfo.pNext = nullptr;
        entry.slot = slot;
        entry.customBorderIndex = customBorderIndex;
        entry.bHasCustomBorder = bCustom;
        m_samplers.push_back(entry);
    }
    return slot;
}

uint32_t DescriptorHeapService::IndexOf(DescriptorSlot slot) const
{
    if (slot.region >= REGION_COUNT) throw std::runtime_error("DescriptorHeapService: slot region is invalid");
    const Region& region = GetRegion(static_cast<RegionId>(slot.region));
    const VkDeviceSize baseIndex = region.offset / region.stride;
    if (slot.index < baseIndex) throw std::runtime_error("DescriptorHeapService: slot index is below its region");
    const VkDeviceSize local = slot.index - baseIndex;
    const VkDeviceSize total = region.staticSlots + region.ringSlots * m_inFlightCount;
    if (local >= total) throw std::runtime_error("DescriptorHeapService: slot index is out of region");
    if (!slot.bRing)
    {
        if (local >= region.staticSlots)
        {
            throw std::runtime_error("DescriptorHeapService: slot is not a static slot");
        }
        if (region.generations[local] != slot.generation)
        {
            throw std::runtime_error("DescriptorHeapService: stale descriptor slot generation");
        }
    }
    else if (slot.ring >= m_inFlightCount || local < region.staticSlots + slot.ring * region.ringSlots ||
             local >= region.staticSlots + (slot.ring + 1) * region.ringSlots)
    {
        throw std::runtime_error("DescriptorHeapService: ring slot is out of its execution range");
    }
    return slot.index;
}

void DescriptorHeapService::Retire(DescriptorSlot slot, uint64_t timelineValue)
{
    if (!slot.bRing) m_pendingRetires.push_back({slot, timelineValue});
}

void DescriptorHeapService::BeginExecution(uint32_t slot)
{
    if (slot >= m_inFlightCount) throw std::runtime_error("DescriptorHeapService: execution slot out of range");
    m_currentExecution = slot;
    m_bHasExecution = true;
}

void DescriptorHeapService::ReleaseStaticSlot(DescriptorSlot slot)
{
    Region& region = GetRegion(static_cast<RegionId>(slot.region));
    const VkDeviceSize baseIndex = region.offset / region.stride;
    const VkDeviceSize local = slot.index - baseIndex;
    if (local >= region.staticSlots) return;
    region.generations[local]++;
    region.freeSlots.push_back(static_cast<uint32_t>(local));
    if (slot.region == REGION_SAMPLER)
    {
        m_samplers.erase(std::remove_if(m_samplers.begin(), m_samplers.end(),
                                        [&slot](const SamplerEntry& entry) { return entry.slot.index == slot.index; }),
                         m_samplers.end());
    }
}

void DescriptorHeapService::ResetExecution(uint32_t slot, uint64_t completedTimeline)
{
    if (slot >= m_inFlightCount) throw std::runtime_error("DescriptorHeapService: execution slot out of range");
    m_regions[REGION_IMAGE].ringCursor[slot] = 0;
    m_regions[REGION_BUFFER].ringCursor[slot] = 0;
    m_regions[REGION_ACCELERATION].ringCursor[slot] = 0;
    m_regions[REGION_SAMPLER].ringCursor[slot] = 0;
    for (auto it = m_pendingRetires.begin(); it != m_pendingRetires.end();)
    {
        if (it->timeline <= completedTimeline)
        {
            ReleaseStaticSlot(it->slot);
            it = m_pendingRetires.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void DescriptorHeapService::Upload(VkCommandBuffer commandBuffer)
{
    if (!IsInitialized()) throw std::runtime_error("DescriptorHeapService: not initialized");
    const auto copyHeap = [this, commandBuffer](const Heap& heap)
    {
        VkBufferCopy copy{};
        copy.size = heap.size + heap.alignment;
        vkCmdCopyBuffer(commandBuffer, heap.staging, heap.buffer, 1, &copy);
    };
    copyHeap(m_resourceHeap);
    copyHeap(m_samplerHeap);

    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_RESOURCE_HEAP_READ_BIT_EXT | VK_ACCESS_2_SAMPLER_HEAP_READ_BIT_EXT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    Bind(commandBuffer);
}

void DescriptorHeapService::Bind(VkCommandBuffer commandBuffer) const
{
    if (!IsInitialized()) throw std::runtime_error("DescriptorHeapService: not initialized");
    VkBindHeapInfoEXT resourceBind{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
    resourceBind.heapRange.address = m_resourceHeap.base;
    resourceBind.heapRange.size = m_resourceHeap.size;
    resourceBind.reservedRangeOffset = m_resourceHeap.size - m_resourceHeap.reserved;
    resourceBind.reservedRangeSize = m_resourceHeap.reserved;
    VkExt::vkCmdBindResourceHeapEXT(commandBuffer, &resourceBind);

    VkBindHeapInfoEXT samplerBind{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
    samplerBind.heapRange.address = m_samplerHeap.base;
    samplerBind.heapRange.size = m_samplerHeap.size;
    samplerBind.reservedRangeOffset = m_samplerHeap.size - m_samplerHeap.reserved;
    samplerBind.reservedRangeSize = m_samplerHeap.reserved;
    VkExt::vkCmdBindSamplerHeapEXT(commandBuffer, &samplerBind);
}
}  // namespace Muyo
