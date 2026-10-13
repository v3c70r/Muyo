#pragma once
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "RenderGraphResourceDesc.h"
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

/// @param desc           A resource declaration.
/// @param memoryTypeBits The memory type bits that declaration's requirements allow, from
///                       `vkGetBufferMemoryRequirements` / `vkGetImageMemoryRequirements`.
/// @return The class that declaration belongs to. See `TransientMemoryClass` for why images are keyed on
///         their whole create info and why the memory types are part of the key.
inline TransientMemoryClass ClassOf(const ResourceDesc& desc, uint32_t memoryTypeBits)
{
    return std::visit(
        [memoryTypeBits](const auto& d) -> TransientMemoryClass
        {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, BufferResourceDesc>)
            {
                TransientMemoryClass cls;
                cls.memoryTypeBits = memoryTypeBits;
                cls.memoryUsage = d.memoryProperties;
                return cls;
            }
            else
            {
                TransientMemoryClass cls;
                cls.isImage = true;
                cls.format = d.format;
                cls.extent = d.extent;
                cls.mips = d.mips;
                cls.layers = d.layers;
                cls.usage = d.usage;
                cls.memoryTypeBits = memoryTypeBits;
                return cls;
            }
        },
        desc);
}

/// One transient's demand on memory, in the terms the planner can reason about.
///
/// The device-facing numbers (`size`, `alignment`, `memoryTypeBits`) come from
/// `vkGetBufferMemoryRequirements` / `vkGetImageMemoryRequirements` on the created-but-unbound
/// resource, and the interval comes from the graph's live ranges. Nothing here needs a device, which is
/// what lets the packing be tested on hand-built requests.
struct TransientMemoryRequest
{
    /// The resource this request is for. Carried through to the plan for diagnostics and binding.
    ResourceHandle handle;
    /// Which resources this may share a block with. See `TransientMemoryClass`.
    TransientMemoryClass classKey;
    /// Bytes the resource requires, from its memory requirements.
    VkDeviceSize size = 0;
    /// Alignment the resource requires, from the same call. Must be non-zero.
    VkDeviceSize alignment = 1;
    /// Memory type bits the resource allows, from the same call. A block's are the intersection of its
    /// members'.
    uint32_t memoryTypeBits = 0;
    /// Execution-order index of the first node that uses this resource.
    std::size_t firstUse = 0;
    /// Execution-order index of the last node that uses this resource. Equal to `firstUse` for a
    /// resource touched by one node.
    std::size_t lastUse = 0;
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

/// Pack a build's transients into as few blocks as their live ranges allow.
///
/// Two transients share a slot when their intervals do not overlap and their classes are equal, and the
/// packing is the classic interval greedy: process a class's requests in order of first use, and give
/// each the first slot whose last use has already ended. Processing in order of start is what makes
/// first-fit place every request that could possibly be placed, so the number of slots is the smallest
/// the intervals permit - the same count a graph-colouring formulation would need.
///
/// A slot's alignment is the widest of its members' and its offset is a multiple of it, so every member
/// bound there satisfies its own alignment: the block's address is aligned to the block's maximum, and
/// each offset within it to its own.
///
/// Deterministic: requests are grouped by class (ordered by `TransientMemoryClass::operator<`) and
/// within a class by `(firstUse, handle)`, so the same graph always produces the same plan and the same
/// memory layout.
///
/// @param requests One per graph-owned transient. Order is irrelevant; the plan sorts.
/// @return The blocks and slots, with the reserved and individual byte totals.
/// @throws std::runtime_error When an alignment is zero, when an interval is inverted (`firstUse >
///         lastUse`), or when a block's members have no memory type in common - each of which is
///         input-falsifiable, so each is a thrown error rather than an assertion that `NDEBUG` removes.
inline TransientMemoryPlan PlanTransientMemory(const std::vector<TransientMemoryRequest>& requests)
{
    TransientMemoryPlan plan;
    for (const TransientMemoryRequest& request : requests)
    {
        if (request.alignment == 0)
        {
            throw std::runtime_error("Transient memory: resource '" + request.handle +
                                     "' reports an alignment of 0, which cannot be satisfied.");
        }
        if (request.firstUse > request.lastUse)
        {
            throw std::runtime_error("Transient memory: resource '" + request.handle + "' is used at node " +
                                     std::to_string(request.firstUse) + " and last at node " +
                                     std::to_string(request.lastUse) + ", so its live range is inverted.");
        }
        plan.individualBytes += request.size;
    }

    std::map<TransientMemoryClass, std::vector<const TransientMemoryRequest*>> byClass;
    for (const TransientMemoryRequest& request : requests) byClass[request.classKey].push_back(&request);

    // A slot while it is being filled: its members' union interval, and the widest/largest of them.
    struct OpenSlot
    {
        std::vector<const TransientMemoryRequest*> members;
        std::size_t lastUse = 0;
        VkDeviceSize size = 0;
        VkDeviceSize alignment = 1;
    };

    for (const auto& [classKey, members] : byClass)
    {
        std::vector<const TransientMemoryRequest*> ordered = members;
        std::sort(ordered.begin(), ordered.end(),
                  [](const TransientMemoryRequest* a, const TransientMemoryRequest* b)
                  {
                      if (a->firstUse != b->firstUse) return a->firstUse < b->firstUse;
                      return a->handle < b->handle;
                  });

        std::vector<OpenSlot> openSlots;
        for (const TransientMemoryRequest* request : ordered)
        {
            const auto slot = std::find_if(openSlots.begin(), openSlots.end(), [request](const OpenSlot& candidate)
                                           { return candidate.lastUse < request->firstUse; });
            if (slot == openSlots.end())
            {
                openSlots.push_back(OpenSlot{});
                openSlots.back().members.push_back(request);
                openSlots.back().lastUse = request->lastUse;
                openSlots.back().size = request->size;
                openSlots.back().alignment = request->alignment;
                continue;
            }
            slot->members.push_back(request);
            slot->lastUse = std::max(slot->lastUse, request->lastUse);
            slot->size = std::max(slot->size, request->size);
            slot->alignment = std::max(slot->alignment, request->alignment);
        }

        TransientMemoryBlock block;
        block.classKey = classKey;
        VkDeviceSize offset = 0;
        bool haveTypeBits = false;
        for (const OpenSlot& open : openSlots)
        {
            const VkDeviceSize remainder = offset % open.alignment;
            if (remainder != 0) offset += open.alignment - remainder;

            TransientMemorySlot slot;
            slot.offset = offset;
            slot.size = open.size;
            slot.alignment = open.alignment;
            for (const TransientMemoryRequest* member : open.members)
            {
                slot.members.push_back(member->handle);
                block.memoryTypeBits =
                    haveTypeBits ? (block.memoryTypeBits & member->memoryTypeBits) : member->memoryTypeBits;
                haveTypeBits = true;
            }
            offset += open.size;
            block.alignment = std::max(block.alignment, open.alignment);
            block.slots.push_back(std::move(slot));
        }
        block.size = offset;

        if (block.memoryTypeBits == 0)
        {
            std::string names;
            for (const TransientMemorySlot& slot : block.slots)
            {
                for (const ResourceHandle& handle : slot.members)
                {
                    if (!names.empty()) names += ", ";
                    names += "'" + handle + "'";
                }
            }
            throw std::runtime_error("Transient memory: " + names +
                                     " have no memory type in common, so no block can serve them. A class "
                                     "carries its members' memory types, so this is either a resource that "
                                     "allows none at all or a class built by hand whose members disagree - "
                                     "check the resources' create info, usage flags and memory requirements.");
        }

        plan.reservedBytes += block.size;
        plan.blocks.push_back(std::move(block));
    }

    return plan;
}

/// Check that a plan keeps the promise its shape makes.
///
/// The plan's whole claim is "same slot, therefore never live at the same time", and every other
/// property follows from the same arithmetic: offsets do not overlap, offsets honour alignments, each
/// member fits, a block's memory types are valid for all of its members, and the byte totals are the
/// sums they say they are. A plan that violates any of those is a bug in the planner, and this is how a
/// test states that without a device.
///
/// @param plan     Plan to check, as returned by `PlanTransientMemory`.
/// @param requests The requests it was planned from.
/// @return An empty string when the plan is consistent, otherwise a description of the first violation
///         found, naming the resources involved.
inline std::string VerifyTransientMemoryPlan(const TransientMemoryPlan& plan,
                                             const std::vector<TransientMemoryRequest>& requests)
{
    std::map<ResourceHandle, const TransientMemoryRequest*> byHandle;
    for (const TransientMemoryRequest& request : requests)
    {
        if (!byHandle.emplace(request.handle, &request).second)
        {
            return "duplicate request for resource '" + request.handle + "'";
        }
    }

    VkDeviceSize individual = 0;
    for (const TransientMemoryRequest& request : requests) individual += request.size;
    if (plan.individualBytes != individual)
    {
        return "individualBytes is " + std::to_string(plan.individualBytes) + ", expected " +
               std::to_string(individual);
    }

    std::map<ResourceHandle, std::size_t> placements;
    VkDeviceSize reserved = 0;
    for (const TransientMemoryBlock& block : plan.blocks)
    {
        reserved += block.size;
        VkDeviceSize previousEnd = 0;
        for (const TransientMemorySlot& slot : block.slots)
        {
            if (slot.offset < previousEnd)
            {
                return "slots overlap in a block: an offset of " + std::to_string(slot.offset) +
                       " starts before the previous slot ends at " + std::to_string(previousEnd);
            }
            if (slot.alignment == 0 || slot.offset % slot.alignment != 0)
            {
                return "a slot at offset " + std::to_string(slot.offset) + " is not aligned to its " +
                       std::to_string(slot.alignment) + " byte alignment";
            }
            previousEnd = slot.offset + slot.size;

            for (const ResourceHandle& handle : slot.members)
            {
                const auto it = byHandle.find(handle);
                if (it == byHandle.end()) return "slot member '" + handle + "' was never requested";
                const TransientMemoryRequest& request = *it->second;
                if (!(request.classKey == block.classKey))
                {
                    return "resource '" + handle + "' is in a block of a different class";
                }
                if (request.alignment == 0 || slot.offset % request.alignment != 0)
                {
                    return "resource '" + handle + "' needs " + std::to_string(request.alignment) +
                           " byte alignment and sits at " + std::to_string(slot.offset);
                }
                if (request.size > slot.size)
                {
                    return "resource '" + handle + "' needs " + std::to_string(request.size) +
                           " bytes but its slot holds " + std::to_string(slot.size);
                }
                if ((block.memoryTypeBits & request.memoryTypeBits) != block.memoryTypeBits)
                {
                    return "resource '" + handle + "' does not allow every memory type its block allows";
                }
                if (placements[handle]++ > 0) return "resource '" + handle + "' appears in more than one slot";

                for (const ResourceHandle& other : slot.members)
                {
                    if (other == handle) continue;
                    const auto otherIt = byHandle.find(other);
                    if (otherIt == byHandle.end()) continue;
                    const TransientMemoryRequest& otherRequest = *otherIt->second;
                    if (request.firstUse <= otherRequest.lastUse && otherRequest.firstUse <= request.lastUse)
                    {
                        return "resources '" + handle + "' and '" + other +
                               "' share a slot but their live ranges overlap (" + std::to_string(request.firstUse) +
                               ".." + std::to_string(request.lastUse) + " and " +
                               std::to_string(otherRequest.firstUse) + ".." + std::to_string(otherRequest.lastUse) +
                               ")";
                    }
                }
            }
        }
    }

    if (plan.reservedBytes != reserved)
    {
        return "reservedBytes is " + std::to_string(plan.reservedBytes) + ", expected " + std::to_string(reserved);
    }
    for (const TransientMemoryRequest& request : requests)
    {
        if (placements.find(request.handle) == placements.end())
        {
            return "resource '" + request.handle + "' was requested but never placed";
        }
    }
    return {};
}
}  // namespace Muyo::RenderGraph
