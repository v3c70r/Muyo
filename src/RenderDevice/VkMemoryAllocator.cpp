#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>

#define VMA_IMPLEMENTATION
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{

VkMemoryAllocator::VkMemoryAllocator() {}
void VkMemoryAllocator::Initalize(VkRenderDevice* pDevice)
{
    // A null device is fatal, and a plain assert is compiled out in Release.
    if (pDevice == nullptr)
    {
        throw std::runtime_error("VkMemoryAllocator::Initalize: the render device is null.");
    }
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = pDevice->GetPhysicalDevice();
    allocatorInfo.device = pDevice->GetDevice();
    allocatorInfo.instance = pDevice->GetInstance();
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_ASSERT(vmaCreateAllocator(&allocatorInfo, &m_allocator));

#ifdef FEATURE_RAY_TRACING
    // Initialize SBT pool
    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCreateInfo.size = 4 * 1024 * 1024;  // 4mb memory for SBT
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT;

    VmaAllocationCreateInfo sampleAllocCreateInfo = {};
    sampleAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    uint32_t memTypeIndex;
    VK_ASSERT(
        vmaFindMemoryTypeIndexForBufferInfo(m_allocator, &bufferCreateInfo, &sampleAllocCreateInfo, &memTypeIndex));

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracingProperties = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR, nullptr};
    GetRenderDevice()->GetPhysicalDeviceProperties(raytracingProperties);

    VmaPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.memoryTypeIndex = memTypeIndex;
    poolCreateInfo.minAllocationAlignment = raytracingProperties.shaderGroupBaseAlignment;

    VK_ASSERT(vmaCreatePool(m_allocator, &poolCreateInfo, &m_SBTPool));

    // Initialize BVH Pool
    bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCreateInfo.size = 512 * 1024 * 1024;  // 4mb memory for SBT
    bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    sampleAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Get BVH property
    VkPhysicalDeviceAccelerationStructurePropertiesKHR bvhProperty = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR, nullptr};
    GetRenderDevice()->GetPhysicalDeviceProperties(bvhProperty);

    VK_ASSERT(
        vmaFindMemoryTypeIndexForBufferInfo(m_allocator, &bufferCreateInfo, &sampleAllocCreateInfo, &memTypeIndex));
    poolCreateInfo.memoryTypeIndex = memTypeIndex;
    // A heap acceleration-structure descriptor requires a 256-byte-aligned address, while this property
    // can be smaller (llvmpipe reports 8); the pool and its allocations must meet the stricter bound.
    poolCreateInfo.minAllocationAlignment =
        std::max<uint32_t>(bvhProperty.minAccelerationStructureScratchOffsetAlignment, 256);
    VK_ASSERT(vmaCreatePool(m_allocator, &poolCreateInfo, &m_BVHPool));
#endif
}

void VkMemoryAllocator::Unintialize()
{
#ifdef FEATURE_RAY_TRACING
    vmaDestroyPool(m_allocator, m_SBTPool);
    vmaDestroyPool(m_allocator, m_BVHPool);
#endif
    vmaDestroyAllocator(m_allocator);
}

void VkMemoryAllocator::AllocateBuffer(VkDeviceSize nSize, VkBufferUsageFlags nBufferUsageFlags,
                                       VmaMemoryUsage nMemoryUsageFlags, VkBuffer& buffer, VmaAllocation& allocation)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = nSize;
    bufferInfo.usage = nBufferUsageFlags;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;

    VK_ASSERT(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr));
}

void VkMemoryAllocator::AllocateBuffer(VkDeviceSize nSize, VkBufferUsageFlags nBufferUsageFlags,
                                       VmaMemoryUsage nMemoryUsageFlags, VkBuffer& buffer, VmaAllocation& allocation,
                                       std::string bufferName, PoolType ePoolType)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = nSize;
    bufferInfo.usage = nBufferUsageFlags;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;
    allocInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    allocInfo.pUserData = bufferName.data();

    switch (ePoolType)
    {
        case PoolType::SBT:
            allocInfo.pool = m_SBTPool;
            break;
        case PoolType::BVH:
            allocInfo.pool = m_BVHPool;
            // A heap acceleration-structure descriptor requires a 256-byte-aligned structure
            // address even where the scratch bound is smaller (see AccelerationStructure).
            allocInfo.minAlignment = 256;
            break;
        default:
            break;
    }

    VK_ASSERT(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr));
}

void VkMemoryAllocator::FreeBuffer(VkBuffer& buffer, VmaAllocation& allocation)
{
    vmaDestroyBuffer(m_allocator, buffer, allocation);
}

void VkMemoryAllocator::MapBuffer(VmaAllocation& allocation, void** ppData)
{
    VK_ASSERT(vmaMapMemory(m_allocator, allocation, ppData));
}

void VkMemoryAllocator::UnmapBuffer(VmaAllocation& allocation) { vmaUnmapMemory(m_allocator, allocation); }

void VkMemoryAllocator::AllocateImage(const VkImageCreateInfo* pImageInfo, VmaMemoryUsage nMemoryUsageFlags,
                                      VkImage& image, VmaAllocation& allocation)
{
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;

    VK_ASSERT(vmaCreateImage(m_allocator, pImageInfo, &allocInfo, &image, &allocation, nullptr));
}
void VkMemoryAllocator::FreeImage(VkImage& image, VmaAllocation& allocation)
{
    vmaDestroyImage(m_allocator, image, allocation);
}

VmaAllocation VkMemoryAllocator::AllocateAliasingBlock(const VkMemoryRequirements& requirements,
                                                       VmaMemoryUsage nMemoryUsage)
{
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsage;
    // VMA's documented requirement for memory that will hold aliasing resources: without it a dedicated
    // allocation may be handed out, and a dedicated allocation carries VkMemoryDedicatedAllocateInfo -
    // which by its own contract belongs to a single resource and cannot be shared with another.
    allocInfo.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;

    VmaAllocation block = nullptr;
    VK_ASSERT(vmaAllocateMemory(m_allocator, &requirements, &allocInfo, &block, nullptr));
    return block;
}

void VkMemoryAllocator::BindBufferToBlock(VkBuffer buffer, VmaAllocation block, VkDeviceSize nOffset)
{
    VK_ASSERT(vmaBindBufferMemory2(m_allocator, block, nOffset, buffer, nullptr));
}

void VkMemoryAllocator::BindImageToBlock(VkImage image, VmaAllocation block, VkDeviceSize nOffset)
{
    VK_ASSERT(vmaBindImageMemory2(m_allocator, block, nOffset, image, nullptr));
}

void VkMemoryAllocator::FreeAliasingBlock(VmaAllocation& block)
{
    if (block == nullptr) return;
    vmaFreeMemory(m_allocator, block);
    block = nullptr;
}

void VkMemoryAllocator::CreateUnboundBuffer(VkDeviceSize nSize, VkBufferUsageFlags nBufferUsageFlags, VkBuffer& buffer)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = nSize;
    bufferInfo.usage = nBufferUsageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // The device comes from VMA rather than from the engine's global accessor: this file owns the
    // allocator, and a buffer created on a device the allocator does not use is the wrong kind of
    // failure to debug.
    VmaAllocatorInfo allocatorInfo = {};
    vmaGetAllocatorInfo(m_allocator, &allocatorInfo);
    VK_ASSERT(vkCreateBuffer(allocatorInfo.device, &bufferInfo, nullptr, &buffer));
}

VkMemoryRequirements VkMemoryAllocator::QueryBufferRequirements(VkBuffer buffer) const
{
    VmaAllocatorInfo allocatorInfo = {};
    vmaGetAllocatorInfo(m_allocator, &allocatorInfo);
    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(allocatorInfo.device, buffer, &requirements);
    return requirements;
}

void VkMemoryAllocator::CreateUnboundImage(const VkImageCreateInfo& imageInfo, VkImage& image)
{
    VmaAllocatorInfo allocatorInfo = {};
    vmaGetAllocatorInfo(m_allocator, &allocatorInfo);
    VK_ASSERT(vkCreateImage(allocatorInfo.device, &imageInfo, nullptr, &image));
}

VkMemoryRequirements VkMemoryAllocator::QueryImageRequirements(VkImage image) const
{
    VmaAllocatorInfo allocatorInfo = {};
    vmaGetAllocatorInfo(m_allocator, &allocatorInfo);
    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(allocatorInfo.device, image, &requirements);
    return requirements;
}

static VkMemoryAllocator allocator;
VkMemoryAllocator* GetMemoryAllocator() { return &allocator; }

}  // namespace Muyo
