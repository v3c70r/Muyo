#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
#include <cassert>
#include <string>
VkMemoryAllocator::VkMemoryAllocator() {}
void VkMemoryAllocator::Initalize(VkRenderDevice* pDevice)
{
    assert(pDevice && "Device is not yet initialized");
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = pDevice->GetPhysicalDevice();
    allocatorInfo.device = pDevice->GetDevice();
    allocatorInfo.instance = pDevice->GetInstance();
    m_pAllocator = std::make_unique<VmaAllocator>();
    vmaCreateAllocator(&allocatorInfo, m_pAllocator.get());
}

void VkMemoryAllocator::Unintialize() { vmaDestroyAllocator(*m_pAllocator); }


void VkMemoryAllocator::AllocateBuffer(size_t nSize,
                                       VkBufferUsageFlags nBufferUsageFlags,
                                       VmaMemoryUsage nMemoryUsageFlags,
                                       VkBuffer& buffer,
                                       VmaAllocation& allocation)
{
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = nSize;
    bufferInfo.usage = nBufferUsageFlags;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;

    vmaCreateBuffer(*m_pAllocator, &bufferInfo, &allocInfo, &buffer,
                    &allocation, nullptr);
}

void VkMemoryAllocator::AllocateBuffer(size_t nSize,
                                       VkBufferUsageFlags nBufferUsageFlags,
                                       VmaMemoryUsage nMemoryUsageFlags,
                                       VkBuffer& buffer,
                                       VmaAllocation& allocation,
                                       std::string bufferName)
{
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = nSize;
    bufferInfo.usage = nBufferUsageFlags;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = nMemoryUsageFlags;
    allocInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    allocInfo.pUserData = bufferName.data();

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
void VkMemoryAllocator::FreeImage(VkImage& image, VmaAllocation &allocation)
{
    vmaDestroyImage(*m_pAllocator, image, allocation);
}

static VkMemoryAllocator allocator;
VkMemoryAllocator* GetMemoryAllocator() { return &allocator; }
