#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "RenderGraphResourceHandle.h"

namespace Muyo::RenderGraph
{
/// The compatibility class a transient's memory belongs to.
///
/// Two transients may share one block exactly when their classes compare equal, so the class *is* the
/// aliasing contract, stated once and checkable: a buffer and an image never share a block, and two
/// images share one only when their create info is identical.
///
/// Identical create info is deliberately stricter than Vulkan requires. Aliasing two images whose
/// formats differ is legal only with `VK_IMAGE_CREATE_ALIAS_BIT` and only when the formats are in the
/// same compatibility class, and those rules are a table to get wrong from memory. Requiring the create
/// info to match removes the question instead of answering it, and the cost - memory left on the table
/// when two images differ only in format - buys the invariant "same class, therefore safe", which is
/// true by construction rather than by consulting a table at every plan.
///
/// A buffer's class carries no shape fields, because every buffer may share memory with every other
/// buffer: the constraints that exist for images come from format compatibility and from tiling, and a
/// buffer has neither.
///
/// One field is not shape and belongs here anyway: `memoryTypeBits`. Two resources whose allowed memory
/// types are disjoint cannot share memory whatever their shape, and the case is reachable rather than
/// theoretical - a buffer created `CPU_ONLY` and one created `GPU_ONLY` have disjoint type sets on a
/// discrete device, and both are ordinary. Carrying the bits in the class keeps the planner total: such
/// a pair lands in two blocks instead of producing a plan the allocator cannot satisfy, and the
/// intersection check inside the planner becomes the safety net for a caller that built the class by
/// hand rather than the mechanism for telling them apart.
struct TransientMemoryClass
{
    /// Pixel format, for images. Unused for buffers.
    VkFormat format = VK_FORMAT_UNDEFINED;
    /// Image extent, for images. Unused for buffers.
    VkExtent2D extent = {0, 0};
    /// Mip level count, for images. Unused for buffers.
    uint32_t mips = 0;
    /// Array layer count, for images. Unused for buffers.
    uint32_t layers = 0;
    /// Image usage flags, for images. Unused for buffers.
    VkImageUsageFlags usage = 0;
    /// Memory type bits the device allows for this resource, from its memory requirements.
    uint32_t memoryTypeBits = 0;
    /// The memory usage its block will be allocated with. A block is one allocation, so everything in it
    /// is created with one usage - and two resources asking for different usages are asking for different
    /// contracts even when the device offers them the same memory types.
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_UNKNOWN;
    /// True for images, false for buffers. The discriminator: a buffer's other fields are all defaults.
    bool isImage = false;

    /// @param other Class to compare against.
    /// @return True when two transients may share a block.
    bool operator==(const TransientMemoryClass& other) const
    {
        return isImage == other.isImage && memoryTypeBits == other.memoryTypeBits && memoryUsage == other.memoryUsage &&
               (!isImage ||
                (format == other.format && extent.width == other.extent.width && extent.height == other.extent.height &&
                 mips == other.mips && layers == other.layers && usage == other.usage));
    }
    /// @param other Class to compare against.
    /// @return A strict weak ordering, so classes can key a map. It orders blocks in the plan
    ///         deterministically; it carries no meaning about memory.
    bool operator<(const TransientMemoryClass& other) const
    {
        if (isImage != other.isImage) return isImage < other.isImage;
        if (format != other.format) return format < other.format;
        if (extent.width != other.extent.width) return extent.width < other.extent.width;
        if (extent.height != other.extent.height) return extent.height < other.extent.height;
        if (mips != other.mips) return mips < other.mips;
        if (layers != other.layers) return layers < other.layers;
        if (usage != other.usage) return usage < other.usage;
        if (memoryTypeBits != other.memoryTypeBits) return memoryTypeBits < other.memoryTypeBits;
        return memoryUsage < other.memoryUsage;
    }
};

/// One offset inside a block, shared by every member whose live range does not overlap another's.
struct TransientMemorySlot
{
    /// Resources bound at this offset. Never two whose intervals overlap - that is the plan's promise.
    std::vector<ResourceHandle> members;
    /// Offset from the start of the block.
    VkDeviceSize offset = 0;
    /// Bytes reserved here: the largest member's size, aligned up to the widest member's alignment.
    VkDeviceSize size = 0;
    /// The widest member's alignment. The offset above is a multiple of it.
    VkDeviceSize alignment = 1;
};

/// One allocation, and the slots laid out inside it.
///
/// One block per compatibility class, because a block is one `VkDeviceMemory`-sized unit: everything in
/// it is bound to the same memory, which is what makes its class a contract rather than a label.
struct TransientMemoryBlock
{
    /// The class every member of every slot belongs to.
    TransientMemoryClass classKey;
    /// Bytes to allocate: the end of the last slot.
    VkDeviceSize size = 0;
    /// Alignment to allocate with: the maximum over all slots.
    VkDeviceSize alignment = 1;
    /// Memory type bits to allocate with: the intersection over all members. Zero is an error, reported
    /// by `PlanTransientMemory` rather than left for the allocator to fail on.
    uint32_t memoryTypeBits = 0;
    /// The slots, in offset order.
    std::vector<TransientMemorySlot> slots;
};

/// What a build's transients should do with memory: one block per class, one offset per non-overlapping
/// group.
struct TransientMemoryPlan
{
    /// One per class that has any transient. Empty when the graph has no transients.
    std::vector<TransientMemoryBlock> blocks;
    /// Bytes the graph will allocate: the sum of the blocks' sizes.
    VkDeviceSize reservedBytes = 0;
    /// Bytes it would allocate with one resource each: the sum of the requests' sizes. The two numbers
    /// together are the whole point - see `SharesMemory`.
    VkDeviceSize individualBytes = 0;

    /// @return How many distinct offsets the plan reserves across all blocks.
    std::size_t SlotCount() const
    {
        std::size_t count = 0;
        for (const TransientMemoryBlock& block : blocks) count += block.slots.size();
        return count;
    }
    /// @return True when the plan allocates less than one resource each, i.e. some slot is shared.
    ///         False is a legitimate answer for a graph whose transients all overlap; it is not a
    ///         failure, and a caller that reports it as one would be reporting on its own graph.
    bool SharesMemory() const { return reservedBytes < individualBytes; }
};

}  // namespace Muyo::RenderGraph
