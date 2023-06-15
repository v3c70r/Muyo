#include "VkMemoryAllocator.h"

#include <cassert>
#include <string>

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
    m_pAllocator = std::make_unique<VmaAllocator>();
    vmaCreateAllocator(&allocatorInfo, m_pAllocator.get());
    
    // Initialize SBT pool
    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.size = 4*1024*1024; // 4mb memory for SBT
    bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT;

    VmaAllocationCreateInfo sampleAllocCreateInfo = {};
    sampleAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    uint32_t memTypeIndex;
    VK_ASSERT(vmaFindMemoryTypeIndexForBufferInfo(*m_pAllocator, &bufferCreateInfo, &sampleAllocCreateInfo, &memTypeIndex));

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracingProperties = {};
    GetRenderDevice()->GetPhysicalDeviceProperties(raytracingProperties);

    VmaPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.minAllocationAlignment = 
    poolCreateInfo.memoryTypeIndex = memTypeIndex;
    poolCreateInfo.minAllocationAlignment = raytracingProperties.shaderGroupBaseAlignment;

    VK_ASSERT(vmaCreatePool(*m_pAllocator, &poolCreateInfo, &m_SBTPool));
}

void VkMemoryAllocator::Unintialize()
{
    vmaDestroyPool(*m_pAllocator, m_SBTPool);
    vmaDestroyAllocator(*m_pAllocator);
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

    vmaCreateBuffer(*m_pAllocator, &bufferInfo, &allocInfo, &buffer,
                    &allocation, nullptr);
}

void VkMemoryAllocator::AllocateBuffer(VkDeviceSize nSize,
                                       VkBufferUsageFlags nBufferUsageFlags,
                                       VmaMemoryUsage nMemoryUsageFlags,
                                       VkBuffer& buffer,
                                       VmaAllocation& allocation,
                                       std::string bufferName,
                                       bool bIsSBTBuffer)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = nSize;
    bufferInfo.usage = nBufferUsageFlags;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;
    allocInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    allocInfo.pUserData = bufferName.data();

    if (bIsSBTBuffer)
    {
        allocInfo.pool = m_SBTPool;
    }

    vmaCreateBuffer(*m_pAllocator, &bufferInfo, &allocInfo, &buffer,
                    &allocation, nullptr);
}

void VkMemoryAllocator::FreeBuffer(VkBuffer& buffer, VmaAllocation& allocation)
{
    vmaDestroyBuffer(*m_pAllocator, buffer, allocation);
}

void VkMemoryAllocator::MapBuffer(VmaAllocation& allocation, void** ppData)
{
    vmaMapMemory(*m_pAllocator, allocation, ppData);
}

void VkMemoryAllocator::UnmapBuffer(VmaAllocation& allocation)
{
    vmaUnmapMemory(*m_pAllocator, allocation);
}

void VkMemoryAllocator::AllocateImage(const VkImageCreateInfo* pImageInfo,
                                      VmaMemoryUsage nMemoryUsageFlags,
                                      VkImage& image, VmaAllocation& allocation)
{
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;

    vmaCreateImage(*m_pAllocator, pImageInfo, &allocInfo, &image, &allocation,
                   nullptr);
}
void VkMemoryAllocator::FreeImage(VkImage& image, VmaAllocation& allocation)
{
    vmaDestroyImage(*m_pAllocator, image, allocation);
}

static VkMemoryAllocator allocator;
VkMemoryAllocator* GetMemoryAllocator() { return &allocator; }

}  // namespace Muyo
