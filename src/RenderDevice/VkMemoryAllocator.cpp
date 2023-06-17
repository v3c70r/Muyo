#include <cassert>
#include <string>

#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{

VkMemoryAllocator::VkMemoryAllocator() {}
void VkMemoryAllocator::Initalize(VkRenderDevice* pDevice)
{
    assert(pDevice && "Device is not yet initialized");
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = pDevice->GetPhysicalDevice();
    allocatorInfo.device = pDevice->GetDevice();
    allocatorInfo.instance = pDevice->GetInstance();
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &m_allocator);
    
    // Initialize SBT pool
    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.size = 4*1024*1024; // 4mb memory for SBT
    bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT;

    VmaAllocationCreateInfo sampleAllocCreateInfo = {};
    sampleAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    uint32_t memTypeIndex;
    VK_ASSERT(vmaFindMemoryTypeIndexForBufferInfo(m_allocator, &bufferCreateInfo, &sampleAllocCreateInfo, &memTypeIndex));

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracingProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR, nullptr};
    GetRenderDevice()->GetPhysicalDeviceProperties(raytracingProperties);

    VmaPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.memoryTypeIndex = memTypeIndex;
    poolCreateInfo.minAllocationAlignment = raytracingProperties.shaderGroupBaseAlignment;

    VK_ASSERT(vmaCreatePool(m_allocator, &poolCreateInfo, &m_SBTPool));
    
    // Initialize BVH Pool
    bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.size = 512*1024*1024; // 4mb memory for SBT
    bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    sampleAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Get BVH property
    VkPhysicalDeviceAccelerationStructurePropertiesKHR bvhProperty = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR, nullptr};
    GetRenderDevice()->GetPhysicalDeviceProperties(bvhProperty);

    VK_ASSERT(vmaFindMemoryTypeIndexForBufferInfo(m_allocator, &bufferCreateInfo, &sampleAllocCreateInfo, &memTypeIndex));
    poolCreateInfo.memoryTypeIndex = memTypeIndex;
    poolCreateInfo.minAllocationAlignment = bvhProperty.minAccelerationStructureScratchOffsetAlignment;
    VK_ASSERT(vmaCreatePool(m_allocator, &poolCreateInfo, &m_BVHPool));
}

void VkMemoryAllocator::Unintialize()
{
    vmaDestroyPool(m_allocator, m_SBTPool);
    vmaDestroyPool(m_allocator, m_BVHPool);
    vmaDestroyAllocator(m_allocator);
}

void VkMemoryAllocator::AllocateBuffer(VkDeviceSize nSize,
                                       VkBufferUsageFlags nBufferUsageFlags,
                                       VmaMemoryUsage nMemoryUsageFlags,
                                       VkBuffer& buffer,
                                       VmaAllocation& allocation)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = nSize;
    bufferInfo.usage = nBufferUsageFlags;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;

    vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer,
                    &allocation, nullptr);
}

void VkMemoryAllocator::AllocateBuffer(VkDeviceSize nSize,
                                       VkBufferUsageFlags nBufferUsageFlags,
                                       VmaMemoryUsage nMemoryUsageFlags,
                                       VkBuffer& buffer,
                                       VmaAllocation& allocation,
                                       std::string bufferName,
                                       PoolType ePoolType)
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
        break;
    default:
        break;
    }

    vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer,
                    &allocation, nullptr);
}

void VkMemoryAllocator::FreeBuffer(VkBuffer& buffer, VmaAllocation& allocation)
{
    vmaDestroyBuffer(m_allocator, buffer, allocation);
}

void VkMemoryAllocator::MapBuffer(VmaAllocation& allocation, void** ppData)
{
    vmaMapMemory(m_allocator, allocation, ppData);
}

void VkMemoryAllocator::UnmapBuffer(VmaAllocation& allocation)
{
    vmaUnmapMemory(m_allocator, allocation);
}

void VkMemoryAllocator::AllocateImage(const VkImageCreateInfo* pImageInfo,
                                      VmaMemoryUsage nMemoryUsageFlags,
                                      VkImage& image, VmaAllocation& allocation)
{
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;

    vmaCreateImage(m_allocator, pImageInfo, &allocInfo, &image, &allocation,
                   nullptr);
}
void VkMemoryAllocator::FreeImage(VkImage& image, VmaAllocation& allocation)
{
    vmaDestroyImage(m_allocator, image, allocation);
}

static VkMemoryAllocator allocator;
VkMemoryAllocator* GetMemoryAllocator() { return &allocator; }

}  // namespace Muyo
