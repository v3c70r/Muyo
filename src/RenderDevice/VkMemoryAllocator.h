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
    void AllocateBuffer(size_t size, VkBufferUsageFlags nBufferUsageFlags, VmaMemoryUsage nMemoryUsageFlags,
                        VkBuffer &buffer, VmaAllocation &allocation);
    void AllocateBuffer(size_t size, VkBufferUsageFlags nBufferUsageFlags, VmaMemoryUsage nMemoryUsageFlags,
                        VkBuffer &buffer, VmaAllocation &allocation, std::string bufferName,
                        PoolType ePoolType = PoolType::Default);
    void FreeBuffer(VkBuffer &buffer, VmaAllocation &allocation);
    void MapBuffer(VmaAllocation &allocation, void **ppData);
    void UnmapBuffer(VmaAllocation &allocation);

    void AllocateImage(const VkImageCreateInfo *pImageInfo, VmaMemoryUsage nMemoryUsageFlags, VkImage &image,
                       VmaAllocation &allocation);
    void FreeImage(VkImage &image, VmaAllocation &allocation);

    // ── Aliasing ─────────────────────────────────────────────────────────────
    //
    // One block of memory, several resources bound into it at chosen offsets. This is what lets a graph
    // reuse memory across resources whose uses do not overlap in time, and it is deliberately the manual
    // path VMA documents for it: the *caller* creates the resources unbound, queries their memory
    // requirements, decides the offsets, and only then binds. VMA's usual helpers pick offsets for you,
    // which is exactly what aliasing cannot have.

    /// Allocate memory with no resource bound to it.
    ///
    /// @param requirements Combined requirements of everything that will alias this block: size and
    ///                     alignment are the maxima, `memoryTypeBits` the bitwise AND. That combination is
    ///                     VMA's own recipe, and the AND is what makes the block valid for all of them.
    /// @param nMemoryUsage Intended usage of the block.
    /// @return The block. Free it with `FreeAliasingBlock` once everything bound to it is destroyed.
    VmaAllocation AllocateAliasingBlock(const VkMemoryRequirements &requirements, VmaMemoryUsage nMemoryUsage);

    /// Bind an already-created buffer to a byte offset of an aliasing block.
    /// The offset must satisfy the buffer's own memory alignment, which is why the requirements are
    /// queried before the offsets are chosen.
    void BindBufferToBlock(VkBuffer buffer, VmaAllocation block, VkDeviceSize nOffset);

    /// Bind an already-created image to a byte offset of an aliasing block.
    void BindImageToBlock(VkImage image, VmaAllocation block, VkDeviceSize nOffset);

    /// Free an aliasing block. Every resource bound to it must already be destroyed: memory is not
    /// released while something is still bound, and the validation layer says so.
    void FreeAliasingBlock(VmaAllocation &block);

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VmaPool m_SBTPool = VK_NULL_HANDLE;  // SBT pool uses another aligment, need to be allocated in a separate pool
    VmaPool m_BVHPool =
        VK_NULL_HANDLE;  // VkPhysicalDeviceAccelerationStructurePropertiesKHR::minAccelerationStructureScratchOffsetAlignment
};

VkMemoryAllocator *GetMemoryAllocator();

}  // namespace Muyo
