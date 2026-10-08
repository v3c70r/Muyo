#include "RenderGraphExecutor.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>

#include "VkRenderDevice.h"

namespace Muyo::RenderGraph
{
RenderGraphExecutor::RenderGraphExecutor(VkRenderDevice* renderDevice, uint32_t inFlightCount)
    : m_renderDevice(renderDevice), m_device(renderDevice != nullptr ? renderDevice->GetDevice() : VK_NULL_HANDLE)
{
    if (inFlightCount == 0) inFlightCount = 1;
    m_slots.resize(inFlightCount);

    for (InFlightSlot& slot : m_slots)
    {
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        // Signalled, so the first AcquireSlot() on each slot does not wait for work that never ran.
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_ASSERT(vkCreateFence(m_device, &fenceInfo, nullptr, &slot.fence));
    }
}

RenderGraphExecutor::~RenderGraphExecutor()
{
    WaitIdle();
    for (InFlightSlot& slot : m_slots)
    {
        ReleaseSlotObjects(slot);
        if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(m_device, slot.fence, nullptr);
    }
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
    for (VkSemaphore semaphore : slot.handoverSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    slot.handoverSemaphores.clear();

    for (VkSemaphore semaphore : slot.doneSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    slot.doneSemaphores.clear();

    for (size_t s = 0; s < slot.commandBuffers.size(); ++s)
    {
        FreeCommandBufferForType(slot.segmentQueues[s], slot.commandBuffers[s]);
    }
    slot.commandBuffers.clear();
    slot.segmentQueues.clear();
}

RenderGraphExecutor::ExecutionSlot RenderGraphExecutor::AcquireSlot(const RenderGraphExecutionPlan& plan)
{
    assert(!m_slots.empty());

    const uint32_t index = m_nextSlot;
    m_nextSlot = (m_nextSlot + 1) % static_cast<uint32_t>(m_slots.size());
    InFlightSlot& slot = m_slots[index];

    // Backpressure: a slot cannot be recorded into again until its previous work has completed. This
    // is what bounds how far the CPU can run ahead of the GPU, and it is also what makes recycling
    // the command buffers and semaphores below safe.
    if (slot.bSubmitted)
    {
        VK_ASSERT(vkWaitForFences(m_device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
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

    // Nothing to submit: an empty plan would otherwise reset a fence that is never signalled, and
    // the next AcquireSlot on this slot would block on it forever.
    if (segments.empty())
    {
        std::cerr << "[WARNING]: RenderGraphExecutor::Submit called with an empty plan; nothing submitted" << std::endl;
        return;
    }

    // ── One semaphore per (producer, consumer) pair this execution needs. ─────────────────────────
    std::map<std::pair<size_t, size_t>, VkSemaphore> handoverFor;
    for (const auto& transfer : transfers)
    {
        const auto key = std::make_pair(transfer.producer, transfer.consumer);
        if (handoverFor.find(key) != handoverFor.end()) continue;

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore));
        handoverFor[key] = semaphore;
        slot.handoverSemaphores.push_back(semaphore);
    }

    // With more than one segment there is no single submission whose completion implies the whole
    // execution finished, so each segment signals a "done" semaphore and an empty join submission
    // folds them into the slot fence. One segment can signal the fence directly and skip the join.
    const bool bNeedsJoin = segments.size() > 1;
    if (bNeedsJoin)
    {
        for (size_t s = 0; s < segments.size(); ++s)
        {
            VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore semaphore = VK_NULL_HANDLE;
            VK_ASSERT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore));
            slot.doneSemaphores.push_back(semaphore);
        }
    }

    VK_ASSERT(vkResetFences(m_device, 1, &slot.fence));

    for (size_t s = 0; s < segments.size(); ++s)
    {
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags2> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        const bool bLast = (s + 1 == segments.size());

        // The caller's wait gates the first segment; the graph's own transfers gate the rest.
        if (s == 0 && info.waitSemaphore != VK_NULL_HANDLE)
        {
            waitSemaphores.push_back(info.waitSemaphore);
            waitStages.push_back(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        }
        for (const auto& [key, semaphore] : handoverFor)
        {
            if (key.second == s)
            {
                waitSemaphores.push_back(semaphore);
                waitStages.push_back(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
            }
            if (key.first == s) signalSemaphores.push_back(semaphore);
        }
        if (bNeedsJoin) signalSemaphores.push_back(slot.doneSemaphores[s]);
        // Without a join the last segment *is* the whole execution, so it signals the outward
        // semaphore too, alongside the slot fence passed to vkQueueSubmit2 below.
        if (bLast && !bNeedsJoin && info.signalSemaphore != VK_NULL_HANDLE)
        {
            signalSemaphores.push_back(info.signalSemaphore);
        }

        std::vector<VkSemaphoreSubmitInfo> waitSemaphoreInfos(waitSemaphores.size());
        for (size_t i = 0; i < waitSemaphores.size(); ++i)
        {
            waitSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemaphoreInfos[i].semaphore = waitSemaphores[i];
            waitSemaphoreInfos[i].value = 0;  // ignored for binary semaphores
            waitSemaphoreInfos[i].stageMask = waitStages[i];
            waitSemaphoreInfos[i].deviceIndex = 0;
        }
        std::vector<VkSemaphoreSubmitInfo> signalSemaphoreInfos(signalSemaphores.size());
        for (size_t i = 0; i < signalSemaphores.size(); ++i)
        {
            signalSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemaphoreInfos[i].semaphore = signalSemaphores[i];
            signalSemaphoreInfos[i].value = 0;  // ignored for binary semaphores
            signalSemaphoreInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalSemaphoreInfos[i].deviceIndex = 0;
        }
        VkCommandBufferSubmitInfo commandBufferInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandBufferInfo.commandBuffer = executionSlot.segmentCommandBuffers[s];
        commandBufferInfo.deviceMask = 0;

        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitSemaphoreInfos.size());
        submitInfo.pWaitSemaphoreInfos = waitSemaphoreInfos.empty() ? nullptr : waitSemaphoreInfos.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreInfos.size());
        submitInfo.pSignalSemaphoreInfos = signalSemaphoreInfos.empty() ? nullptr : signalSemaphoreInfos.data();

        // Without a join the last segment is the whole execution, so it signals the fence as well.
        const VkFence fence = (bLast && !bNeedsJoin) ? slot.fence : VK_NULL_HANDLE;

        VK_ASSERT(vkQueueSubmit2(GetQueueForType(segments[s].queueType), 1, &submitInfo, fence));
    }

    if (bNeedsJoin)
    {
        std::vector<VkSemaphoreSubmitInfo> waitSemaphoreInfos(slot.doneSemaphores.size());
        for (size_t i = 0; i < slot.doneSemaphores.size(); ++i)
        {
            waitSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemaphoreInfos[i].semaphore = slot.doneSemaphores[i];
            waitSemaphoreInfos[i].value = 0;
            waitSemaphoreInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            waitSemaphoreInfos[i].deviceIndex = 0;
        }
        VkSemaphoreSubmitInfo resultSemaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        resultSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 joinInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        joinInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitSemaphoreInfos.size());
        joinInfo.pWaitSemaphoreInfos = waitSemaphoreInfos.data();
        if (info.signalSemaphore != VK_NULL_HANDLE)
        {
            resultSemaphoreInfo.semaphore = info.signalSemaphore;
            joinInfo.signalSemaphoreInfoCount = 1;
            joinInfo.pSignalSemaphoreInfos = &resultSemaphoreInfo;
        }
        // An empty submission: it carries no work, only the wait on every segment and the fence that
        // marks the execution complete.
        VK_ASSERT(vkQueueSubmit2(GetQueueForType(QueueType::GRAPHICS), 1, &joinInfo, slot.fence));
    }

    slot.bSubmitted = true;
}

void RenderGraphExecutor::WaitIdle()
{
    for (InFlightSlot& slot : m_slots)
    {
        if (!slot.bSubmitted) continue;
        VK_ASSERT(vkWaitForFences(m_device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
        slot.bSubmitted = false;
    }
}

VkQueue RenderGraphExecutor::GetQueueForType(QueueType type) const
{
    if (type == QueueType::COMPUTE) return m_renderDevice->GetComputeQueue();
    return m_renderDevice->GetGraphicsQueue();
}
}  // namespace Muyo::RenderGraph
