#include "RenderGraphExecutor.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "VkRenderDevice.h"

namespace Muyo::RenderGraph
{
namespace
{
/// @param device Device to create on.
/// @return A timeline semaphore whose counter starts at zero.
VkSemaphore CreateTimelineSemaphore(VkDevice device)
{
    VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphoreInfo.pNext = &typeInfo;

    VkSemaphore semaphore = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
    return semaphore;
}

/// Build a submit-time semaphore reference.
///
/// @param semaphore Semaphore to reference.
/// @param value Timeline value to wait for or signal; ignored for a binary semaphore.
/// @return The reference, ready to place in a VkSubmitInfo2.
VkSemaphoreSubmitInfo MakeSemaphoreSubmitInfo(VkSemaphore semaphore, uint64_t value = 0)
{
    VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    info.semaphore = semaphore;
    info.value = value;
    info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    info.deviceIndex = 0;
    return info;
}
}  // namespace

RenderGraphExecutor::RenderGraphExecutor(VkRenderDevice* renderDevice, uint32_t inFlightCount)
    : m_renderDevice(renderDevice), m_device(renderDevice != nullptr ? renderDevice->GetDevice() : VK_NULL_HANDLE)
{
    if (inFlightCount == 0) inFlightCount = 1;
    m_slots.resize(inFlightCount);

    // One timeline per *distinct* queue. Graphics and compute are usually different queues; on a device
    // with no dedicated compute family they are the same one, and sharing a single timeline is then
    // required rather than merely tidy - two timelines on one queue would let a handover between them
    // wait on a value signalled by work that had not been submitted yet.
    // Values start at 1: a timeline signal must be *greater* than the semaphore's current value, and
    // the counter starts at zero, so 0 is not a legal first signal
    // (VUID-VkSubmitInfo2-semaphore-03882).
    m_timelines.push_back(QueueTimeline{renderDevice->GetGraphicsQueue(), CreateTimelineSemaphore(m_device), 1});
    if (renderDevice->GetComputeQueue() != renderDevice->GetGraphicsQueue())
    {
        m_timelines.push_back(QueueTimeline{renderDevice->GetComputeQueue(), CreateTimelineSemaphore(m_device), 1});
    }
}

RenderGraphExecutor::~RenderGraphExecutor()
{
    WaitIdle();
    for (InFlightSlot& slot : m_slots)
    {
        ReleaseSlotObjects(slot);
    }
    for (QueueTimeline& timeline : m_timelines)
    {
        if (timeline.semaphore != VK_NULL_HANDLE) vkDestroySemaphore(m_device, timeline.semaphore, nullptr);
    }
    m_timelines.clear();
}

VkCommandBuffer RenderGraphExecutor::AllocateCommandBufferForType(QueueType type) const
{
    if (type == QueueType::COMPUTE) return m_renderDevice->AllocateComputeCommandBuffer();
    return m_renderDevice->AllocateReusablePrimaryCommandbuffer();
}

void RenderGraphExecutor::FreeCommandBufferForType(QueueType type, VkCommandBuffer cmdBuf) const
{
    if (cmdBuf == VK_NULL_HANDLE) return;
    if (type == QueueType::COMPUTE)
    {
        m_renderDevice->FreeComputeCommandBuffer(cmdBuf);
    }
    else
    {
        m_renderDevice->FreeReusablePrimaryCommandbuffer(cmdBuf);
    }
}

void RenderGraphExecutor::ReleaseSlotObjects(InFlightSlot& slot)
{
    for (size_t s = 0; s < slot.commandBuffers.size(); ++s)
    {
        FreeCommandBufferForType(slot.segmentQueues[s], slot.commandBuffers[s]);
    }
    slot.commandBuffers.clear();
    slot.segmentQueues.clear();
    slot.completion.clear();
}

RenderGraphExecutor::QueueTimeline& RenderGraphExecutor::TimelineFor(QueueType type)
{
    const VkQueue queue = GetQueueForType(type);
    for (QueueTimeline& timeline : m_timelines)
    {
        if (timeline.queue == queue) return timeline;
    }
    // GetQueueForType only ever returns the graphics or the compute queue, and the constructor creates a
    // timeline for each distinct one, so this is unreachable unless a queue key is added without one.
    std::cerr << "[FATAL]: RenderGraphExecutor has no timeline for the queue a segment requested" << std::endl;
    std::abort();
}

bool RenderGraphExecutor::IsComplete(const InFlightSlot& slot) const
{
    for (const auto& [timeline, value] : slot.completion)
    {
        uint64_t currentValue = 0;
        VK_ASSERT(vkGetSemaphoreCounterValue(m_device, timeline, &currentValue));
        if (currentValue < value) return false;
    }
    return true;
}

void RenderGraphExecutor::WaitFor(const InFlightSlot& slot) const
{
    if (slot.completion.empty()) return;

    std::vector<VkSemaphore> semaphores;
    std::vector<uint64_t> values;
    semaphores.reserve(slot.completion.size());
    values.reserve(slot.completion.size());
    for (const auto& [timeline, value] : slot.completion)
    {
        semaphores.push_back(timeline);
        values.push_back(value);
    }

    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.flags = 0;  // wait for all of them
    waitInfo.semaphoreCount = static_cast<uint32_t>(semaphores.size());
    waitInfo.pSemaphores = semaphores.data();
    waitInfo.pValues = values.data();
    VK_ASSERT(vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX));
}

RenderGraphExecutor::ExecutionSlot RenderGraphExecutor::AcquireSlot(const RenderGraphExecutionPlan& plan)
{
    assert(!m_slots.empty());

    const uint32_t index = m_nextSlot;
    m_nextSlot = (m_nextSlot + 1) % static_cast<uint32_t>(m_slots.size());
    InFlightSlot& slot = m_slots[index];

    // Backpressure: a slot cannot be recorded into again until its previous work has completed. This is
    // what bounds how far the CPU can run ahead of the GPU, and it is also what makes recycling the
    // command buffers below safe.
    if (slot.bSubmitted)
    {
        // Compare counters before blocking: the common case is that the work has finished, and the
        // non-blocking check avoids a wait call on every acquisition.
        if (!IsComplete(slot)) WaitFor(slot);
        slot.bSubmitted = false;
    }

    ReleaseSlotObjects(slot);

    const size_t nSegments = plan.GetSegmentCount();
    slot.commandBuffers.resize(nSegments);
    slot.segmentQueues.resize(nSegments);
    for (size_t s = 0; s < nSegments; ++s)
    {
        slot.segmentQueues[s] = plan.segments[s].queueType;
        slot.commandBuffers[s] = AllocateCommandBufferForType(plan.segments[s].queueType);
    }

    return ExecutionSlot{index, slot.commandBuffers};
}

void RenderGraphExecutor::Submit(const RenderGraphExecutionPlan& plan, const ExecutionSlot& executionSlot,
                                 const RenderGraphExecuteInfo& info)
{
    assert(executionSlot.index < m_slots.size());
    InFlightSlot& slot = m_slots[executionSlot.index];
    const std::vector<RenderGraphQueueSegment>& segments = plan.segments;
    const std::vector<RenderGraphQueueTransfer>& transfers = plan.transfers;

    // Submit what the caller recorded into, not the slot's own list: they are the same handles
    // when AcquireSlot's result is passed straight back, and using the caller's copy means a
    // stale slot cannot silently submit the current buffers instead. Checked rather than asserted
    // because a mismatch would index out of bounds (see #30 for the NDEBUG policy).
    if (executionSlot.segmentCommandBuffers.size() != segments.size())
    {
        std::cerr << "[FATAL]: RenderGraphExecutor::Submit got " << executionSlot.segmentCommandBuffers.size()
                  << " command buffers for " << segments.size() << " plan segments" << std::endl;
        std::abort();
    }

    const bool bWantsOutwardSignal = info.signalSemaphore != VK_NULL_HANDLE || info.signalFence != VK_NULL_HANDLE;

    // Nothing to submit. The execution is trivially complete - there are no values to reach - but the
    // caller may still be waiting on what it asked for, and a fence that is never signalled is
    // indistinguishable from work that has not finished. So honour the outward request with an empty
    // submission rather than returning silently.
    if (segments.empty())
    {
        std::cerr << "[WARNING]: RenderGraphExecutor::Submit called with an empty plan; nothing submitted" << std::endl;
        if (bWantsOutwardSignal)
        {
            VkSemaphoreSubmitInfo signalInfo = MakeSemaphoreSubmitInfo(info.signalSemaphore, info.signalValue);
            VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            if (info.signalSemaphore != VK_NULL_HANDLE)
            {
                submitInfo.signalSemaphoreInfoCount = 1;
                submitInfo.pSignalSemaphoreInfos = &signalInfo;
            }
            VK_ASSERT(vkQueueSubmit2(GetQueueForType(QueueType::GRAPHICS), 1, &submitInfo, info.signalFence));
        }
        return;
    }

    // ── One timeline value per segment, handed out in plan order. ────────────────────────────────
    // Each segment signals the value its consumers wait on, which replaces both the per-pair binary
    // handover semaphores and the per-segment "done" semaphores: one value serves as both, and unlike a
    // binary semaphore it can also be waited on from the host.
    std::vector<VkSemaphore> segmentTimelines(segments.size());
    std::vector<uint64_t> segmentValues(segments.size());
    for (size_t s = 0; s < segments.size(); ++s)
    {
        QueueTimeline& timeline = TimelineFor(segments[s].queueType);
        segmentTimelines[s] = timeline.semaphore;
        segmentValues[s] = timeline.nextValue++;
    }

    // What this execution must reach to be complete: the highest value it signalled on each queue it
    // touched. A queue may carry several segments, and its counter is ordered, so the highest value
    // implies the earlier ones.
    slot.completion.clear();
    for (size_t s = 0; s < segments.size(); ++s)
    {
        bool bRecorded = false;
        for (auto& [timeline, value] : slot.completion)
        {
            if (timeline == segmentTimelines[s])
            {
                value = std::max(value, segmentValues[s]);
                bRecorded = true;
                break;
            }
        }
        if (!bRecorded) slot.completion.emplace_back(segmentTimelines[s], segmentValues[s]);
    }

    const bool bNeedsJoin = segments.size() > 1;

    for (size_t s = 0; s < segments.size(); ++s)
    {
        std::vector<VkSemaphoreSubmitInfo> waitInfos;
        std::vector<VkSemaphoreSubmitInfo> signalInfos;
        const bool bLast = (s + 1 == segments.size());

        // The caller's wait gates the first segment; the graph's own handovers gate the rest.
        if (s == 0 && info.waitSemaphore != VK_NULL_HANDLE)
        {
            waitInfos.push_back(MakeSemaphoreSubmitInfo(info.waitSemaphore, info.waitValue));
        }
        for (const auto& transfer : transfers)
        {
            if (transfer.consumer != s) continue;
            const VkSemaphoreSubmitInfo producerInfo =
                MakeSemaphoreSubmitInfo(segmentTimelines[transfer.producer], segmentValues[transfer.producer]);
            // Several nodes in this segment may consume the same producer, and waiting twice on one
            // value would be redundant rather than wrong. Keep the submission small.
            bool bAlreadyWaited = false;
            for (const VkSemaphoreSubmitInfo& existing : waitInfos)
            {
                if (existing.semaphore == producerInfo.semaphore && existing.value == producerInfo.value)
                {
                    bAlreadyWaited = true;
                    break;
                }
            }
            if (!bAlreadyWaited) waitInfos.push_back(producerInfo);
        }

        // This segment's own completion value.
        signalInfos.push_back(MakeSemaphoreSubmitInfo(segmentTimelines[s], segmentValues[s]));
        // Without a join the last segment *is* the whole execution, so it carries the outward signal
        // too. (A single submit can signal a timeline value and a binary semaphore together, which is
        // what makes this possible at all.)
        if (bLast && !bNeedsJoin && info.signalSemaphore != VK_NULL_HANDLE)
        {
            signalInfos.push_back(MakeSemaphoreSubmitInfo(info.signalSemaphore, info.signalValue));
        }

        VkCommandBufferSubmitInfo commandBufferInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandBufferInfo.commandBuffer = executionSlot.segmentCommandBuffers[s];
        commandBufferInfo.deviceMask = 0;

        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
        submitInfo.pWaitSemaphoreInfos = waitInfos.empty() ? nullptr : waitInfos.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
        submitInfo.pSignalSemaphoreInfos = signalInfos.empty() ? nullptr : signalInfos.data();

        // Likewise the caller's fence, when there is no join to carry it.
        const VkFence fence = (bLast && !bNeedsJoin) ? info.signalFence : VK_NULL_HANDLE;

        VK_ASSERT(vkQueueSubmit2(GetQueueForType(segments[s].queueType), 1, &submitInfo, fence));
    }

    // A join is needed only to fold several queues into one *outward* signal. Completion itself is
    // tracked by the timeline values above, so an execution that asks for nothing outward needs no
    // extra submission - previously one was always made, to signal the slot fence.
    if (bNeedsJoin && bWantsOutwardSignal)
    {
        std::vector<VkSemaphoreSubmitInfo> waitInfos(slot.completion.size());
        for (size_t i = 0; i < slot.completion.size(); ++i)
        {
            waitInfos[i] = MakeSemaphoreSubmitInfo(slot.completion[i].first, slot.completion[i].second);
        }

        VkSemaphoreSubmitInfo resultSemaphoreInfo = MakeSemaphoreSubmitInfo(info.signalSemaphore, info.signalValue);
        VkSubmitInfo2 joinInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        joinInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
        joinInfo.pWaitSemaphoreInfos = waitInfos.data();
        if (info.signalSemaphore != VK_NULL_HANDLE)
        {
            joinInfo.signalSemaphoreInfoCount = 1;
            joinInfo.pSignalSemaphoreInfos = &resultSemaphoreInfo;
        }
        // An empty submission: it carries no work, only the wait on every touched queue and the
        // outward signal. Safe to place on the graphics queue - the waits are on timeline values, which
        // can be waited on from any queue.
        VK_ASSERT(vkQueueSubmit2(GetQueueForType(QueueType::GRAPHICS), 1, &joinInfo, info.signalFence));
    }

    slot.bSubmitted = true;
}

void RenderGraphExecutor::WaitIdle()
{
    for (InFlightSlot& slot : m_slots)
    {
        if (!slot.bSubmitted) continue;
        WaitFor(slot);
        slot.bSubmitted = false;
    }
}

VkQueue RenderGraphExecutor::GetQueueForType(QueueType type) const
{
    if (type == QueueType::COMPUTE) return m_renderDevice->GetComputeQueue();
    return m_renderDevice->GetGraphicsQueue();
}
}  // namespace Muyo::RenderGraph
