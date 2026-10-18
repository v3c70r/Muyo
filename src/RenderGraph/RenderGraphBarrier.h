#pragma once
#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

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
