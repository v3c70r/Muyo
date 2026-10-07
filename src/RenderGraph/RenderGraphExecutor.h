#pragma once
#include <vulkan/vulkan.h>

#include <vector>

#include "RenderGraphExecutionPlan.h"

namespace Muyo
{
class VkRenderDevice;
}

namespace Muyo::RenderGraph
{
/// Executes a recorded render graph.
///
/// The graph records one command buffer per queue segment (see `RenderGraphExecutionPlan`) and hands
/// them here. Everything after recording belongs to the executor: the cross-queue handover
/// semaphores, the queue submissions, and waiting for the work to finish.
///
/// It exists so that in-flight executions, per-queue timeline semaphores and completion signalling
/// can grow here without the graph knowing about them (issue #11, A1.3).
class RenderGraphExecutor
{
public:
    /// @param renderDevice Device whose queues executions are submitted to.
    explicit RenderGraphExecutor(VkRenderDevice* renderDevice);
    /// Releases the synchronization objects held for the last submission.
    ~RenderGraphExecutor();

    /// Submit one recorded execution.
    /// @param plan Plan the command buffers were recorded from. Its segments decide which queues are
    ///        used and its transfers decide where cross-queue handover semaphores are needed.
    /// @param segmentCommandBuffers Recorded command buffers, one per segment, in plan order.
    void Submit(const RenderGraphExecutionPlan& plan, const std::vector<VkCommandBuffer>& segmentCommandBuffers);

    /// Block until every queue used by the last submission has drained.
    void WaitIdle();

    /// @param type A resolved queue key (see `GetQueueKey`).
    /// @return The queue that key submits to.
    VkQueue GetQueueForType(QueueType type) const;

private:
    /// Destroy the handover semaphores held for the previous submission.
    void DestroyHandoverSemaphores();

    VkRenderDevice* m_renderDevice = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    /// Semaphores ordering the cross-queue handovers of the most recent submission. Created per
    /// submission because the transfer set is a property of the graph rather than of the executor;
    /// once executions can overlap (A1.3b) each in-flight slot will own its own set.
    std::vector<VkSemaphore> m_handoverSemaphores;
};
}  // namespace Muyo::RenderGraph
