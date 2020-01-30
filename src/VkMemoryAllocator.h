#pragma once
#include <vk_mem_alloc.h>
#include <memory>
class VkRenderDevice;

// Wrap allocator
class VkMemoryAllocator
{
public:
    void Initalize(VkRenderDevice *pDevice);
    VkMemoryAllocator();
    ~VkMemoryAllocator();
    void AllocateBuffer(size_t size, VkBufferUsageFlags nBufferUsageFlags,
                        VmaMemoryUsage nMemoryUsageFlags, VkBuffer &buffer,
                        VmaAllocation &allocation);
    void FreeBuffer(VkBuffer &buffer, VmaAllocation &allocation);
    void MapBuffer(VmaAllocation &allocation, void **ppData);
    void UnmapBuffer(VmaAllocation &allocation);

    void AllocateImage(const VkImageCreateInfo *pImageInfo,
                       VmaMemoryUsage nMemoryUsageFlags, VkImage &image,
                       VmaAllocation &allocation);
    void FreeImage(VkImage &image, VmaAllocation &allocation);

private:
    std::unique_ptr<VmaAllocator> m_pAllocator = nullptr;
};

VkMemoryAllocator *GetMemoryAllocator();

