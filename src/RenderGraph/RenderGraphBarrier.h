#pragma once
#include <vulkan/vulkan.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "RenderGraphNodeResource.h"

namespace Muyo::RenderGraph
{
/// What the graph has recorded about a resource's most recent access.
///
/// One of these is kept per resource for the duration of one `Execute`. It is a plain struct, and the
/// derivation below is a free function, so the barrier a pair of uses produces can be checked without a
/// device - which is the only way to test that the *masks* are right rather than that a run happened to
/// validate.
struct BarrierPriorState
{
    bool bSeen = false;             ///< A GPU use of this resource has been recorded in this execution.
    VkAccessFlags2 lastAccess = 0;  ///< Access mask of that use; the barrier's source scope.
    VkPipelineStageFlags2 lastStages = VK_PIPELINE_STAGE_2_NONE;  ///< Its stages; the source stage scope.
    VkImageLayout lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;         ///< The layout it left the resource in.
    ResourceIOType lastIo = ResourceIOType::READ;                 ///< Its direction, for the WAR/WAW test.
    bool bWrittenByHost = false;        ///< The last writer was a CPU node, so the write needs flushing.
    int32_t pendingAcquireFamily = -1;  ///< Producer queue family when an ownership handover is pending.
};

/// The masks, layouts and queue-family indices one barrier between two uses needs.
///
/// Produced by `DeriveBarrierMasks`; consumed by `RenderGraphBuilder::RecordBarriers`, which only has
/// to turn it into a `VkImageMemoryBarrier2` or a `VkBufferMemoryBarrier2`.
struct BarrierMasks
{
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;  ///< Stage scope to wait on.
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_NONE;  ///< Stage scope to block until done.
    VkAccessFlags2 srcAccess = 0;                               ///< Access scope to make visible.
    VkAccessFlags2 dstAccess = 0;                               ///< Access scope the consumer declares.
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;        ///< Layout the resource is in.
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;        ///< Layout the consumer needs.
    bool bNeeded = false;                                       ///< Whether a barrier has to be recorded at all.
    uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED;          ///< Producer family for an ownership acquire.
    uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED;          ///< Consumer family for an ownership acquire.
};

/// Derive the barrier between a resource's last recorded access and the next use.
///
/// The stage masks come from the uses themselves: `ResolvedResourceUse::stages` is the destination scope,
/// and the previous use's stages are the source scope. That is the whole point of the table in
/// `ResourceUseResolver.h` - a colour attachment write is ordered at `COLOR_ATTACHMENT_OUTPUT` and a
/// vertex-buffer read at `VERTEX_INPUT`, rather than both at `ALL_COMMANDS`. The three cases where the
/// previous stages are not known or are not the source are handled explicitly:
///
/// - **First use in an execution.** The contents are discarded (the image goes from `UNDEFINED`), so
///   there is nothing to wait for: `TOP_OF_PIPE`, no source access.
/// - **Last written by a CPU node.** The write happened on the host, so the source is `HOST` and the
///   access is `HOST_WRITE` - which is also what the destination-stage VUIDs require of a mask that
///   includes `HOST_WRITE`.
/// - **Ownership handover pending.** The producing queue's stages are not recorded in this execution
///   (the release barrier carried them), so the acquire waits on `ALL_COMMANDS`.
///
/// A barrier is recorded when any of the following is true, and skipped otherwise: the first use, a
/// pending acquire, a host write to flush, a change of access mask, a change of layout, or a write on
/// either side (a WAW or WAR hazard remains even when the masks are identical).
///
/// Stage bits are also masked against the stages the device actually enables. The table names a union of
/// the stages that can touch a resource, and a stage bit whose feature is not enabled may not appear in a
/// mask at all (`VUID-VkImageMemoryBarrier2-dstStageMask-07946` for the ray tracing stage): a build
/// without ray tracing would otherwise emit `RAY_TRACING_SHADER` in every sampled-texture barrier. A use
/// whose *only* stage is masked out is a declaration the device cannot honour - an acceleration structure
/// on a device without ray tracing - and is reported rather than silently emitting an empty mask.
///
/// @param prior           What the graph recorded for this resource, or a default-constructed value when
///                        it has not been used.
/// @param use             The use about to be recorded.
/// @param queueFamily     Family of the queue the use is being recorded on; used for the acquire's
///                        destination.
/// @param supportedStages Stage bits this device enables; see `RenderGraphBuilder::SupportedStages()`.
/// @return The masks and layouts of the barrier, and whether one is needed.
/// @throws std::runtime_error if every stage the use needs is one this device does not enable.
inline BarrierMasks DeriveBarrierMasks(const BarrierPriorState& prior, const ResolvedResourceUse& use,
                                       uint32_t queueFamily,
                                       VkPipelineStageFlags2 supportedStages = ~VkPipelineStageFlags2{0})
{
    BarrierMasks masks{};

    const bool bAcquire = prior.pendingAcquireFamily >= 0;

    if (bAcquire)
    {
        // The producer recorded its own stages in the release barrier; this side only knows that
        // whatever it was has to finish before the consumer starts.
        masks.srcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        masks.srcAccess = 0;
    }
    else if (prior.bWrittenByHost)
    {
        masks.srcStage = VK_PIPELINE_STAGE_2_HOST_BIT;
        masks.srcAccess = VK_ACCESS_2_HOST_WRITE_BIT;
    }
    else if (prior.bSeen)
    {
        masks.srcStage = prior.lastStages;
        masks.srcAccess = prior.lastAccess;
    }
    else
    {
        // Nothing has touched the resource yet, so its contents are not a dependency.
        masks.srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        masks.srcAccess = 0;
    }

    masks.srcStage &= supportedStages;
    masks.dstStage = use.stages & supportedStages;
    masks.dstAccess = use.access;

    if (masks.dstStage == VK_PIPELINE_STAGE_2_NONE && masks.dstAccess != 0)
    {
        throw std::runtime_error("Resource '" + use.handle +
                                 "' is used at a pipeline stage this device does not enable (usage " +
                                 NameOf(use.usage) +
                                 "). A ray tracing use on a build without ray tracing "
                                 "cannot be synchronised.");
    }

    masks.oldLayout = prior.bSeen ? prior.lastLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    masks.newLayout = use.imageLayout;

    masks.srcQueueFamily = bAcquire ? static_cast<uint32_t>(prior.pendingAcquireFamily) : VK_QUEUE_FAMILY_IGNORED;
    masks.dstQueueFamily = bAcquire ? queueFamily : VK_QUEUE_FAMILY_IGNORED;

    // A write on either side leaves a hazard even when the access and layout are unchanged.
    const bool bHazard = prior.lastIo != ResourceIOType::READ || use.io != ResourceIOType::READ;
    masks.bNeeded = bAcquire || prior.bWrittenByHost || !prior.bSeen || (prior.lastAccess != use.access) ||
                    (prior.lastLayout != use.imageLayout) || bHazard;

    return masks;
}

/// The barrier lists one queue-family handover contributes to.
///
/// A handover produces at most one barrier, but which list it belongs in depends on the resource kind, so the
/// emitter appends rather than returns, and the caller records both lists in one `vkCmdPipelineBarrier2`.
struct QueueTransferBarriers
{
    std::vector<VkImageMemoryBarrier2> images;    ///< Image handovers.
    std::vector<VkBufferMemoryBarrier2> buffers;  ///< Buffer and acceleration-structure handovers.
};

/// Append the release half of a handover: the producer's own stages and access, an empty destination scope,
/// and both queue-family indices.
///
/// The destination scope is empty because the acquire barrier on the consumer side names it; waiting on
/// `ALL_COMMANDS` on both sides would make the handover wait for work neither side has.
///
/// Free and device-free so the recorded barrier can be asserted without a device - the same reason
/// `DeriveBarrierMasks` is. Nothing here reads the handle: a test may pass `VK_NULL_HANDLE` and check the
/// masks and indices, which are the part that regressed in #153.
///
/// @param out       Lists to append to.
/// @param handle    The resource's name, for the diagnostic.
/// @param kind      Resource kind; decides the list, and an unexpressible kind is reported.
/// @param image     The image handle, when `kind` is `IMAGE`.
/// @param buffer    The buffer handle, or an acceleration structure's backing buffer.
/// @param aspect    Image aspect mask, when `kind` is `IMAGE`.
/// @param prior     What the graph recorded for the resource.
/// @param srcFamily Producing queue family.
/// @param dstFamily Consuming queue family.
/// @throws std::runtime_error if the kind has no barrier to express a handover.
inline void AppendReleaseTransfer(QueueTransferBarriers& out, const std::string& handle, ResourceKind kind,
                                  VkImage image, VkBuffer buffer, VkImageAspectFlags aspect,
                                  const BarrierPriorState& prior, uint32_t srcFamily, uint32_t dstFamily)
{
    switch (kind)
    {
        case ResourceKind::IMAGE:
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = prior.lastStages;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            barrier.srcAccessMask = prior.lastAccess;
            barrier.dstAccessMask = 0;
            barrier.oldLayout = prior.lastLayout;
            barrier.newLayout = prior.lastLayout;
            barrier.srcQueueFamilyIndex = srcFamily;
            barrier.dstQueueFamilyIndex = dstFamily;
            barrier.image = image;
            barrier.subresourceRange = {aspect, 0, 1, 0, 1};
            out.images.push_back(barrier);
            return;
        }
        case ResourceKind::BUFFER:
        case ResourceKind::ACCELERATION_STRUCTURE:
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = prior.lastStages;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            barrier.srcAccessMask = prior.lastAccess;
            barrier.dstAccessMask = 0;
            barrier.srcQueueFamilyIndex = srcFamily;
            barrier.dstQueueFamilyIndex = dstFamily;
            barrier.buffer = buffer;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            out.buffers.push_back(barrier);
            return;
        }
        case ResourceKind::SAMPLER:
            break;
    }

    // Silently skipping a transfer is how an acceleration structure's handover went missing for two releases
    // (#153): the chain matched images and buffers and said nothing about anything else.
    throw std::runtime_error("Queue handover of '" + handle + "' is scheduled, but a " + std::string(NameOf(kind)) +
                             " has no barrier to express it.");
}

/// Append the acquire half of a handover: an empty source scope, the consumer's stages and access, and both
/// queue-family indices.
///
/// @param out    Lists to append to.
/// @param handle The resource's name, for the diagnostic.
/// @param kind   Resource kind; decides the list, and an unexpressible kind is reported.
/// @param image  The image handle, when `kind` is `IMAGE`.
/// @param buffer The buffer handle, or an acceleration structure's backing buffer.
/// @param aspect Image aspect mask, when `kind` is `IMAGE`.
/// @param masks  The masks `DeriveBarrierMasks` produced for the first use in the consumer family; its
///               `srcQueueFamily`/`dstQueueFamily` are the indices recorded here.
/// @throws std::runtime_error if no handover is pending, or the kind has no barrier to express one.
inline void AppendAcquireTransfer(QueueTransferBarriers& out, const std::string& handle, ResourceKind kind,
                                  VkImage image, VkBuffer buffer, VkImageAspectFlags aspect, const BarrierMasks& masks)
{
    if (masks.srcQueueFamily == VK_QUEUE_FAMILY_IGNORED)
    {
        throw std::runtime_error("An acquire barrier was requested for '" + handle +
                                 "', but no handover is pending for it.");
    }

    switch (kind)
    {
        case ResourceKind::IMAGE:
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = masks.srcStage;
            barrier.dstStageMask = masks.dstStage;
            barrier.srcAccessMask = masks.srcAccess;
            barrier.dstAccessMask = masks.dstAccess;
            barrier.oldLayout = masks.oldLayout;
            barrier.newLayout = masks.newLayout;
            barrier.srcQueueFamilyIndex = masks.srcQueueFamily;
            barrier.dstQueueFamilyIndex = masks.dstQueueFamily;
            barrier.image = image;
            barrier.subresourceRange = {aspect, 0, 1, 0, 1};
            out.images.push_back(barrier);
            return;
        }
        case ResourceKind::BUFFER:
        case ResourceKind::ACCELERATION_STRUCTURE:
        {
            if (buffer == VK_NULL_HANDLE)
            {
                throw std::runtime_error(
                    "A queue handover is pending for '" + handle +
                    "', but it did not resolve to a structure with a backing buffer - which is the only way to "
                    "express it. Recording a family-less memory barrier instead would consume the handover and "
                    "perform nothing (#153).");
            }
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = masks.srcStage;
            barrier.dstStageMask = masks.dstStage;
            barrier.srcAccessMask = masks.srcAccess;
            barrier.dstAccessMask = masks.dstAccess;
            barrier.srcQueueFamilyIndex = masks.srcQueueFamily;
            barrier.dstQueueFamilyIndex = masks.dstQueueFamily;
            barrier.buffer = buffer;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            out.buffers.push_back(barrier);
            return;
        }
        case ResourceKind::SAMPLER:
            break;
    }

    throw std::runtime_error("A queue handover is pending for '" + handle + "', but a " + std::string(NameOf(kind)) +
                             " has no barrier to express it.");
}

/// The barrier(s) one use needs: image and buffer lists, plus the device-wide memory barrier an in-family
/// acceleration-structure use takes.
struct UseBarriers
{
    QueueTransferBarriers barriers;          ///< Image and buffer barrier lists.
    std::optional<VkMemoryBarrier2> memory;  ///< Set for an in-family acceleration-structure use.
};

/// Build the barrier(s) a use needs, **including which kind of barrier it takes**.
///
/// The routing is the point. An acceleration structure with a queue handover pending has to be a buffer barrier
/// on its backing buffer - a memory barrier has no field for the family indices - while an in-family use is the
/// device-wide memory barrier. That decision used to live inline in `RecordBarriers`, where no test could observe
/// it: the emission was asserted after #156 and the routing was not, and falsifying it left the suite green
/// (#158).
///
/// Free and device-free like `DeriveBarrierMasks`, so both the choice and the barrier it produces can be
/// asserted without a device. The handles are only copied into the barrier, never dereferenced.
///
/// @param use    The resolved use; its kind selects the barrier.
/// @param masks  The masks `DeriveBarrierMasks` produced for it.
/// @param image  The image handle, when `use.kind` is `IMAGE`.
/// @param buffer The buffer handle, or an acceleration structure's backing buffer.
/// @param aspect Image aspect mask, when `use.kind` is `IMAGE`.
/// @return The barriers; every list empty for a kind that has none.
/// @throws std::runtime_error if a handover is pending for an acceleration structure that did not resolve to a
///         structure with a backing buffer - the only way to express it - forwarded from
///         `AppendAcquireTransfer`.
inline UseBarriers DeriveUseBarriers(const ResolvedResourceUse& use, const BarrierMasks& masks, VkImage image,
                                     VkBuffer buffer, VkImageAspectFlags aspect)
{
    UseBarriers out;

    switch (use.kind)
    {
        case ResourceKind::IMAGE:
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = masks.srcStage;
            barrier.dstStageMask = masks.dstStage;
            barrier.srcAccessMask = masks.srcAccess;
            barrier.dstAccessMask = masks.dstAccess;
            barrier.oldLayout = masks.oldLayout;
            barrier.newLayout = masks.newLayout;
            barrier.srcQueueFamilyIndex = masks.srcQueueFamily;
            barrier.dstQueueFamilyIndex = masks.dstQueueFamily;
            barrier.image = image;
            barrier.subresourceRange = {aspect, 0, 1, 0, 1};
            out.barriers.images.push_back(barrier);
            return out;
        }
        case ResourceKind::BUFFER:
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = masks.srcStage;
            barrier.dstStageMask = masks.dstStage;
            barrier.srcAccessMask = masks.srcAccess;
            barrier.dstAccessMask = masks.dstAccess;
            barrier.srcQueueFamilyIndex = masks.srcQueueFamily;
            barrier.dstQueueFamilyIndex = masks.dstQueueFamily;
            barrier.buffer = buffer;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            out.barriers.buffers.push_back(barrier);
            return out;
        }
        case ResourceKind::ACCELERATION_STRUCTURE:
        {
            if (masks.srcQueueFamily != VK_QUEUE_FAMILY_IGNORED)
            {
                // A handover needs the two family indices, which only a buffer barrier can carry.
                AppendAcquireTransfer(out.barriers, use.handle, use.kind, VK_NULL_HANDLE, buffer,
                                      VK_IMAGE_ASPECT_COLOR_BIT, masks);
                return out;
            }

            // No handle to address in-family: the barrier is a memory barrier and the masks name the AS bits.
            VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            barrier.srcStageMask = masks.srcStage;
            barrier.srcAccessMask = masks.srcAccess;
            barrier.dstStageMask = masks.dstStage;
            barrier.dstAccessMask = masks.dstAccess;
            out.memory = barrier;
            return out;
        }
        case ResourceKind::SAMPLER:
            return out;
    }
    return out;
}

/// Record a use as the resource's new prior state.
///
/// The stages are stored **filtered**, with the same set the barrier used. Storing the raw table stages would
/// put a bit the device does not enable into the next barrier's source scope - a release barrier that names
/// `RAY_TRACING_SHADER` on a build without ray tracing, which is the VUID class the filter exists to prevent.
///
/// @param prior           The state to update.
/// @param use             The use just recorded.
/// @param supportedStages Stage bits this device enables; the default keeps everything, for tests and for
///                        callers that have no device.
/// @return The state the next barrier for this resource is derived from.
inline BarrierPriorState RecordAccess(BarrierPriorState prior, const ResolvedResourceUse& use,
                                      VkPipelineStageFlags2 supportedStages = ~VkPipelineStageFlags2{0})
{
    prior.bSeen = true;
    prior.lastAccess = use.access;
    prior.lastStages = use.stages & supportedStages;
    prior.lastLayout = use.imageLayout;
    prior.lastIo = use.io;
    prior.bWrittenByHost = false;
    prior.pendingAcquireFamily = -1;
    return prior;
}
}  // namespace Muyo::RenderGraph
