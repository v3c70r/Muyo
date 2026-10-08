#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "RenderGraphExecutionPlan.h"

namespace Muyo
{
class VkRenderDevice;
}

namespace Muyo::RenderGraph
{
/// External synchronization for one execution.
///
/// Deliberately has no `signalFence`: a Vulkan submit signals at most one fence, so a caller-supplied
/// fence and the executor's per-slot fence could not both come from the completion submit without an
/// extra submission. A1.4 replaces the slot fence with a timeline value, which serves both, so the
/// caller-facing fence arrives there rather than here. The interim shape is `{wait, signal}` - #11.
///
/// `signalSemaphore` is the **only** completion signal a caller of the non-blocking path gets, and it
/// is not host-waitable (binary semaphores have no `vkWaitSemaphores`). To know the work finished,
/// either pass a semaphore into a later submission and give that a fence, or call `WaitIdle()`.
///
/// Overlapping executions of the same graph share that graph's resources and are unordered relative
/// to each other unless chained through these semaphores.
struct RenderGraphExecuteInfo
{
    /// Waited on before this execution's first work - e.g. the swapchain image-acquired semaphore.
    VkSemaphore waitSemaphore = VK_NULL_HANDLE;
    /// Signalled once *all* of this execution's queues have completed - e.g. for vkQueuePresentKHR.
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;
};

/// Executes a recorded render graph.
///
/// The graph records one command buffer per queue segment (see `RenderGraphExecutionPlan`) and hands
/// them here. Everything after recording belongs to the executor: the in-flight slots, the
/// cross-queue handover semaphores, the queue submissions, and waiting for work to finish.
///
/// The executor owns the command buffers, because they cannot be recycled while an execution is still
/// in flight - which is what the in-flight slots exist to track. `AcquireSlot` blocks while every
/// slot is busy, so the CPU can run at most `inFlightCount` executions ahead of the GPU.
class RenderGraphExecutor
{
public:
    /// @param renderDevice Device whose queues executions are submitted to.
    /// @param inFlightCount How many executions may be in flight at once. 1 serialises them: the next
    ///        AcquireSlot() blocks until the previous submission completes. A windowed caller passes
    ///        its frame count; a headless caller can pass 1 and wait explicitly.
    explicit RenderGraphExecutor(VkRenderDevice* renderDevice, uint32_t inFlightCount = 1);
    /// Waits for outstanding work, then releases every slot's objects.
    ~RenderGraphExecutor();

    /// Owns raw Vulkan handles and frees them on destruction, so copying would double-destroy.
    RenderGraphExecutor(const RenderGraphExecutor&) = delete;
    /// Deleted for the same reason as the copy constructor.
    RenderGraphExecutor& operator=(const RenderGraphExecutor&) = delete;

    /// The command buffers of one in-flight execution, ready to record into.
    struct ExecutionSlot
    {
        uint32_t index = 0;                                  ///< Identifies the slot for Submit().
        std::vector<VkCommandBuffer> segmentCommandBuffers;  ///< One per plan segment, in plan order.
    };

    /// Acquire a slot to record into, blocking while every slot is still in flight.
    ///
    /// The returned slot must be handed to Submit(). Its command buffers stay valid until the slot is
    /// reused, which cannot happen before that slot's fence has signalled.
    /// @param plan Plan this execution will use; its segments size and type the command buffers.
    /// @return The slot to record into.
    ExecutionSlot AcquireSlot(const RenderGraphExecutionPlan& plan);

    /// Submit a slot acquired from AcquireSlot().
    /// @param plan Plan the slot was recorded from.
    /// @param slot Slot returned by AcquireSlot().
    /// @param info External synchronization; both semaphores are optional.
    void Submit(const RenderGraphExecutionPlan& plan, const ExecutionSlot& slot, const RenderGraphExecuteInfo& info);

    /// Block until every submitted execution has completed. Safe to call at any time.
    void WaitIdle();

    /// @param type A resolved queue key (see `GetQueueKey`).
    /// @return The queue that key submits to.
    VkQueue GetQueueForType(QueueType type) const;

private:
    /// One in-flight execution's state. Everything in it is owned by this slot, so two executions
    /// never share a semaphore, a fence or a command buffer.
    struct InFlightSlot
    {
        VkFence fence = VK_NULL_HANDLE;               ///< Signalled by this slot's completion submit.
        bool bSubmitted = false;                      ///< The fence is pending and must be waited on.
        std::vector<VkCommandBuffer> commandBuffers;  ///< One per segment, allocated on AcquireSlot.
        std::vector<QueueType> segmentQueues;         ///< Queue of each buffer, to return it correctly.
        std::vector<VkSemaphore> handoverSemaphores;  ///< One per (producer, consumer) transfer pair.
        std::vector<VkSemaphore> doneSemaphores;      ///< One per segment, used by the completion join.
    };

    VkCommandBuffer AllocateCommandBufferForType(QueueType type) const;
    void FreeCommandBufferForType(QueueType type, VkCommandBuffer cmdBuf) const;
    /// Destroy the slot's semaphores and return its command buffers. Only safe once its fence has
    /// signalled.
    void ReleaseSlotObjects(InFlightSlot& slot);

    VkRenderDevice* m_renderDevice = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<InFlightSlot> m_slots;
    uint32_t m_nextSlot = 0;
};
}  // namespace Muyo::RenderGraph
