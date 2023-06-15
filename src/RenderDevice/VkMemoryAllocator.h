#pragma once
#include <vk_mem_alloc.h>

#include <string>

namespace Muyo
{
class VkRenderDevice;

// Wrap allocator
class VkMemoryAllocator
{
public:
    void Initalize(VkRenderDevice *pDevice);
    void Unintialize();
    VkMemoryAllocator();
    void AllocateBuffer(size_t size, VkBufferUsageFlags nBufferUsageFlags,
                        VmaMemoryUsage nMemoryUsageFlags, VkBuffer &buffer,
                        VmaAllocation &allocation);
    void AllocateBuffer(size_t size, VkBufferUsageFlags nBufferUsageFlags,
                        VmaMemoryUsage nMemoryUsageFlags, VkBuffer &buffer,
                        VmaAllocation &allocation,
                        std::string bufferName, bool bIsSBTBuffer = false);
    void FreeBuffer(VkBuffer &buffer, VmaAllocation &allocation);
    void MapBuffer(VmaAllocation &allocation, void **ppData);
    void UnmapBuffer(VmaAllocation &allocation);

    void AllocateImage(const VkImageCreateInfo *pImageInfo,
                       VmaMemoryUsage nMemoryUsageFlags, VkImage &image,
                       VmaAllocation &allocation);
    void FreeImage(VkImage &image, VmaAllocation &allocation);

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VmaPool m_SBTPool = VK_NULL_HANDLE; // SBT pool uses another aligment, need to be allocated in a separate pool
};

VkMemoryAllocator *GetMemoryAllocator();

}  // namespace Muyo
