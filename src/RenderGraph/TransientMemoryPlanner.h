#pragma once
// Internal to muyo_rg: implementation detail, not part of the public surface. See docs/RenderGraph.md.
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
#include "TransientMemoryPlan.h"

namespace Muyo::RenderGraph
{
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
    /// Queue-family "domain" two requests must match to share a *slot* (see `PlanTransientMemory`).
    ///
    /// Disjoint live ranges are not enough on their own: the graph orders uses within one queue family
    /// (a pipeline barrier's first scope covers earlier commands on the same queue, so the new
    /// resource's first-use barrier waits on the old resource's last use), but not across families -
    /// `RenderGraphExecutor` only inserts a wait for a *shared handle*, and an aliased pair is two. A
    /// domain is the queue family a resource's uses run on, one-based; a resource with no GPU use
    /// (host-written only) is domain 0. Two requests never share a slot unless their domains match.
    uint64_t sharingDomain = 0;
    /// False when a resource's uses span more than one queue family. Its slot is then exclusively its
    /// own: its uses can overlap another resource's on either family, so no other resource may reuse the
    /// bytes - including another spanning resource, which is why this is not folded into the domain.
    bool canShareSlot = true;
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
        /// Domain of every member placed here, and whether a later request may join it at all.
        uint64_t sharingDomain = 0;
        bool reusable = true;
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
            // A slot is reusable only by a request of the same queue-family domain, and never by a
            // request whose uses span families. Both halves are what keep an aliased hand-over inside one
            // queue, where the graph's barriers order it.
            const auto slot = request->canShareSlot
                                  ? std::find_if(openSlots.begin(), openSlots.end(),
                                                 [request](const OpenSlot& candidate)
                                                 {
                                                     return candidate.reusable &&
                                                            candidate.sharingDomain == request->sharingDomain &&
                                                            candidate.lastUse < request->firstUse;
                                                 })
                                  : openSlots.end();
            if (slot == openSlots.end())
            {
                openSlots.push_back(OpenSlot{});
                openSlots.back().members.push_back(request);
                openSlots.back().lastUse = request->lastUse;
                openSlots.back().size = request->size;
                openSlots.back().alignment = request->alignment;
                openSlots.back().sharingDomain = request->sharingDomain;
                openSlots.back().reusable = request->canShareSlot;
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
                if (!request.canShareSlot && slot.members.size() > 1)
                {
                    return "resource '" + handle +
                           "' spans queue families and cannot share a slot, but shares one with others";
                }

                for (const ResourceHandle& other : slot.members)
                {
                    if (other == handle) continue;
                    const auto otherIt = byHandle.find(other);
                    if (otherIt == byHandle.end()) continue;
                    const TransientMemoryRequest& otherRequest = *otherIt->second;
                    if (request.sharingDomain != otherRequest.sharingDomain)
                    {
                        return "resources '" + handle + "' and '" + other +
                               "' share a slot but run on different queue families";
                    }
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
