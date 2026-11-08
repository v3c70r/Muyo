#pragma once
// Internal to muyo_rg: implementation detail, not part of the public surface. See docs/RenderGraph.md.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "RenderGraphBarrier.h"
#include "RenderGraphTrace.h"

namespace Muyo::RenderGraph
{
/// Receives what one execution actually submitted, as it is submitted.
///
/// The timeline values are handed out by the executor, not derivable from the plan: a plan says "segment 1 waits on
/// segment 0", and only the executor knows that the wait is on value 7 of the graphics timeline. That is the fact a
/// trace exists to record, so it is reported where the values are chosen rather than recomputed by a consumer from
/// counters it cannot see.
///
/// This is internal playback plumbing: `RenderGraphTrace` is the public record, and a consumer receives a finished
/// trace through `RenderGraphBuilder::SetTraceSink` without naming this interface. It lives beside the executor
/// because the executor needs the trace types anyway - the other direction would be a cycle.
class SubmissionObserver
{
public:
    /// @brief Required for correct destruction through the interface pointer.
    virtual ~SubmissionObserver() = default;
    /// @param segment Index into the plan's segments.
    /// @param queue   Queue the segment submits to.
    /// @param value   Timeline value this segment signals on its queue.
    virtual void OnSegmentSignalled(std::size_t segment, QueueType queue, uint64_t value) = 0;
    /// @param segment Index of the segment that waits.
    /// @param queue   Queue the waited value belongs to.
    /// @param value   Timeline value waited for.
    /// @param reason  `Timeline` for a graph handover, `Caller` for the external wait gating segment 0.
    virtual void OnSegmentWait(std::size_t segment, QueueType queue, uint64_t value, TraceWaitReason reason) = 0;
};

/// Collects a trace while one execution runs.
///
/// Three contributions, each taken where its information exists rather than reconstructed afterwards:
/// `Begin` from the plan (device-free), `OnUseBarriers` where a use's barriers are derived (the only place the
/// resource name and the barrier are in scope together), and `OnSegmentSignalled`/`OnSegmentWait` where the executor
/// hands out timeline values (the only place those values exist at all).
///
/// Not thread-safe, by design: it records one execution, and an execution records from one thread. Two executions
/// in flight get two recorders.
class TraceRecorder : public BarrierObserver, public SubmissionObserver
{
public:
    /// Start recording an execution.
    ///
    /// The execution number is passed in rather than assigned afterwards, because `Begin` replaces the trace
    /// wholesale: a caller that wrote the counter *before* calling this had it discarded, and every trace carried
    /// execution 0 - found in review of #166.
    /// @param plan      Plan about to run.
    /// @param nodes     Compiled nodes the plan indexes.
    /// @param scope     Device and topology producing this trace.
    /// @param execution Engine-lifetime counter for this execution.
    void Begin(const RenderGraphExecutionPlan& plan, std::span<const TraceNodeView> nodes, const TraceScope& scope,
               uint64_t execution)
    {
        m_trace = TracePlan(plan, nodes, scope);
        m_trace.execution = execution;
        m_segment = 0;
    }

    /// @param segment Segment whose recordings follow; barriers reported until the next call belong to it.
    void BeginSegment(std::size_t segment) { m_segment = segment; }

    /// @param handle   Resource the barriers address.
    /// @param barriers The barriers derived for that use.
    void OnUseBarriers(const ResourceHandle& handle, const UseBarriers& barriers) override
    {
        if (m_segment >= m_trace.segments.size())
        {
            return;
        }
        std::vector<TraceBarrier>& sink = m_trace.segments[m_segment].barriers;
        for (const VkImageMemoryBarrier2& barrier : barriers.barriers.images)
        {
            sink.push_back(MakeBarrier(handle, TraceBarrierKind::Image, barrier.srcStageMask, barrier.dstStageMask,
                                       barrier.srcAccessMask, barrier.dstAccessMask, barrier.oldLayout,
                                       barrier.newLayout, barrier.srcQueueFamilyIndex, barrier.dstQueueFamilyIndex));
        }
        for (const VkBufferMemoryBarrier2& barrier : barriers.barriers.buffers)
        {
            sink.push_back(MakeBarrier(handle, TraceBarrierKind::Buffer, barrier.srcStageMask, barrier.dstStageMask,
                                       barrier.srcAccessMask, barrier.dstAccessMask, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_UNDEFINED, barrier.srcQueueFamilyIndex,
                                       barrier.dstQueueFamilyIndex));
        }
        if (barriers.memory.has_value())
        {
            const VkMemoryBarrier2& barrier = *barriers.memory;
            // No resource: a memory barrier is device-wide, and naming one resource for it would be a lie.
            sink.push_back(MakeBarrier(ResourceHandle{}, TraceBarrierKind::Memory, barrier.srcStageMask,
                                       barrier.dstStageMask, barrier.srcAccessMask, barrier.dstAccessMask,
                                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_QUEUE_FAMILY_IGNORED,
                                       VK_QUEUE_FAMILY_IGNORED));
        }
    }

    /// @param segment Segment whose completion is signalled.
    /// @param queue   Queue carrying the value.
    /// @param value   Timeline value signalled.
    void OnSegmentSignalled(std::size_t segment, QueueType queue, uint64_t value) override
    {
        m_trace.signals.push_back(TraceSignal{.segment = segment, .queue = queue, .value = value});
    }

    /// @param segment Segment that waits.
    /// @param queue   Queue carrying the waited value.
    /// @param value   Timeline value waited for.
    /// @param reason  `timeline` or `caller`.
    void OnSegmentWait(std::size_t segment, QueueType queue, uint64_t value, TraceWaitReason reason) override
    {
        m_trace.waits.push_back(TraceWait{.segment = segment, .queue = queue, .value = value, .reason = reason});
    }

    /// @return What has been recorded so far.
    const RenderGraphTrace& GetTrace() const { return m_trace; }

private:
    static TraceBarrier MakeBarrier(const ResourceHandle& resource, TraceBarrierKind kind,
                                    VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                                    VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess, VkImageLayout oldLayout,
                                    VkImageLayout newLayout, uint32_t srcFamily, uint32_t dstFamily)
    {
        return TraceBarrier{.resource = resource,
                            .kind = kind,
                            .srcStage = NameOfPipelineStages(srcStage),
                            .dstStage = NameOfPipelineStages(dstStage),
                            .srcAccess = NameOfAccesses(srcAccess),
                            .dstAccess = NameOfAccesses(dstAccess),
                            .oldLayout = oldLayout,
                            .newLayout = newLayout,
                            .srcFamily = srcFamily,
                            .dstFamily = dstFamily};
    }

    RenderGraphTrace m_trace;   ///< The trace being filled.
    std::size_t m_segment = 0;  ///< Segment the next barrier report belongs to.
};

}  // namespace Muyo::RenderGraph
