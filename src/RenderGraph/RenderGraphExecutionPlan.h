#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <vector>

#include "RenderGraphNodeResource.h"
#include "RenderGraphResourceHandle.h"

namespace Muyo::RenderGraph
{
/// Queue family indices a plan resolves queue keys against.
struct RenderGraphQueueFamilies
{
    uint32_t graphics = VK_QUEUE_FAMILY_IGNORED;  ///< Family that runs graphics and ray tracing work.
    uint32_t compute = VK_QUEUE_FAMILY_IGNORED;   ///< Family that runs compute work.
};

/// Resolve which queue a node's work runs on.
///
/// Only an explicitly-marked async compute node uses the dedicated async compute queue; everything
/// else (including non-async compute) follows the graphics queue.
/// @param type Queue the node declared.
/// @param bAsync Whether the node opted into the async compute queue.
/// @return The queue key the node is scheduled on.
inline QueueType GetQueueKey(QueueType type, bool bAsync)
{
    return (type == QueueType::COMPUTE && bAsync) ? QueueType::COMPUTE : QueueType::GRAPHICS;
}

/// @param queueType A resolved queue key (as returned by `GetQueueKey`).
/// @param families Queue family indices for this device.
/// @return The Vulkan queue family the key runs on. Compute uses the compute family; graphics,
/// ray tracing and CPU work follow the graphics family.
inline uint32_t GetQueueFamilyForQueueType(QueueType queueType, const RenderGraphQueueFamilies& families)
{
    return (queueType == QueueType::COMPUTE) ? families.compute : families.graphics;
}

/// A contiguous run of compiled nodes that executes on one queue.
///
/// Segment indices are ranges into `CompiledRenderGraph::GetNodes()`, so they are stable as long as
/// the graph is not rebuilt.
///
/// Segments are split at CPU nodes, so two *adjacent* segments can share a queue: a
/// `[GPU, CPU, GPU]` sequence on one queue yields two segments, and therefore two submissions where
/// the pre-split code made one. That is correct (program order on one queue) and adjacent segments
/// with the same `queueType` need no ownership transfer, so a scheduler is free to merge them into
/// a single submission.
struct RenderGraphQueueSegment
{
    QueueType queueType = QueueType::GRAPHICS;  ///< Resolved queue this segment runs on.
    std::size_t begin = 0;                      ///< First compiled node index (inclusive).
    std::size_t end = 0;                        ///< One past the last compiled node index.
};

/// A resource handed from one queue segment to another across a queue-family boundary.
///
/// Requires a release barrier when the producer segment ends and an acquire barrier before the
/// consumer segment first touches the resource, plus a semaphore to order the two submissions.
struct RenderGraphQueueTransfer
{
    std::size_t producer = 0;                           ///< Segment that last used the resource.
    std::size_t consumer = 0;                           ///< Segment that next uses it.
    ResourceHandle handle;                              ///< Resource being handed over.
    uint32_t producerFamily = VK_QUEUE_FAMILY_IGNORED;  ///< Queue family handing it over.
    uint32_t consumerFamily = VK_QUEUE_FAMILY_IGNORED;  ///< Queue family taking ownership.
};

/// The scheduling plan derived from a compiled graph.
///
/// Tells an executor what to run where: which nodes are host-side, how the GPU nodes split into
/// per-queue segments, and which resources cross a queue-family boundary. Derived purely from the
/// compiled nodes and the device's queue families, so it is rebuilt whenever the graph is built and
/// is read-only during execution.
struct RenderGraphExecutionPlan
{
    /// Compiled node indices that run host-side (QueueType::CPU). They all run before any GPU
    /// segment is recorded, and belong to no segment.
    std::vector<std::size_t> cpuNodes;
    /// Contiguous per-queue runs of compiled nodes, in execution order.
    std::vector<RenderGraphQueueSegment> segments;
    /// Resources crossing a queue-family boundary between two segments.
    std::vector<RenderGraphQueueTransfer> transfers;

    /// @return True when the graph has no GPU work to run.
    bool IsEmpty() const { return segments.empty(); }
    /// @return Number of queue segments.
    std::size_t GetSegmentCount() const { return segments.size(); }
    /// @return Number of cross-queue transfers.
    std::size_t GetTransferCount() const { return transfers.size(); }
};
}  // namespace Muyo::RenderGraph
