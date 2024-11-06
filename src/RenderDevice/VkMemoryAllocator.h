#pragma once
#include <vk_mem_alloc.h>

#include <string>

namespace Muyo
{
class VkRenderDevice;

enum class PoolType
{
    Default,
    SBT,
    BVH
};

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
                        std::string bufferName, PoolType ePoolType = PoolType::Default);
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
    VmaPool m_BVHPool = VK_NULL_HANDLE; //VkPhysicalDeviceAccelerationStructurePropertiesKHR::minAccelerationStructureScratchOffsetAlignment

};

VkMemoryAllocator *GetMemoryAllocator();

}  // namespace Muyo
