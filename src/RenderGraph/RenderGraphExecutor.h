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
/// `signalSemaphore` and `signalFence` both mean "every queue of this execution has completed". The
/// semaphore is what a present path needs; the fence is host-waitable, so a caller that only wants to
/// know the work is done does not have to submit anything of its own to find out.
///
/// Passing neither is legitimate: the executor tracks completion itself with timeline values, and
/// `WaitIdle()` observes it. Asking for no outward signal therefore costs nothing - in particular it
/// needs no join submission.
///
/// The executor's own timelines stay private. A caller that wants to chain executions owns its own
/// timeline and passes it as both `signalSemaphore`/`signalValue` here and
/// `waitSemaphore`/`waitValue` on the next execution, so the chaining contract does not depend on how
/// the executor lays out its internal counters.
///
/// Overlapping executions of the same graph share that graph's resources and are unordered relative
/// to each other unless chained through these semaphores.
struct RenderGraphExecuteInfo
{
    /// Waited on before this execution's first work - e.g. the swapchain image-acquired semaphore.
    VkSemaphore waitSemaphore = VK_NULL_HANDLE;
    /// Value to wait for when `waitSemaphore` is a timeline semaphore. This is what lets a caller chain
    /// one execution onto another's completion: pass the same timeline the earlier execution signalled
    /// and the value it signalled. Ignored for a binary semaphore.
    uint64_t waitValue = 0;
    /// Signalled once *all* of this execution's queues have completed - e.g. for vkQueuePresentKHR.
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;
    /// Value to signal when `signalSemaphore` is a timeline semaphore. Ignored for a binary semaphore.
    uint64_t signalValue = 0;
    /// Signalled once *all* of this execution's queues have completed. Unlike `signalSemaphore` this is
    /// waitable from the host, and unlike a query it needs no submission of the caller's own.
    VkFence signalFence = VK_NULL_HANDLE;
};

/// Executes a recorded render graph.
///
/// The graph records one command buffer per queue segment (see `RenderGraphExecutionPlan`) and hands
/// them here. Everything after recording belongs to the executor: the in-flight slots, the
/// cross-queue handovers, the queue submissions, and what completion means.
///
/// **Completion is a timeline value per queue** - not a fence, and not a binary semaphore. Every
/// segment signals the next value on its queue's timeline, and waits on the values of the segments it
/// consumes, so an execution has completed exactly when the values it signalled have been reached.
/// Three things follow, and each replaces something the binary design could not express:
///
///  - a caller can wait for an execution itself, either through `signalFence` or by waiting on the
///    values, where a binary semaphore is not host-waitable at all;
///  - slot reclamation is a value comparison against a counter, so a slot is reusable the moment its
///    work is done rather than when a particular submission happened to signal a fence;
///  - nothing is created or destroyed per submission except command buffers, so overlapping executions
///    cannot alias each other's signalling state.
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
    /// A timeline semaphore for one queue, and the next value to hand out.
    ///
    /// One per *queue*, not per slot: a timeline is a counter shared by everything submitted to that
    /// queue, and its values must be signalled in increasing order. So slots own no semaphores at all,
    /// which is what lets executions overlap without aliasing each other's signalling state.
    struct QueueTimeline
    {
        VkQueue queue = VK_NULL_HANDLE;          ///< The queue this timeline tracks.
        VkSemaphore semaphore = VK_NULL_HANDLE;  ///< Timeline semaphore; never reset, only counted up.
        uint64_t nextValue = 0;                  ///< Next value to signal; handed out in submit order.
    };

    /// One in-flight execution's state.
    struct InFlightSlot
    {
        bool bSubmitted = false;                      ///< Work outstanding; must be waited for.
        std::vector<VkCommandBuffer> commandBuffers;  ///< One per segment, allocated on AcquireSlot.
        std::vector<QueueType> segmentQueues;         ///< Queue of each buffer, to return it correctly.
        /// What this execution must reach to be complete: one entry per queue it touched, holding the
        /// *highest* value it signalled there. Values never repeat, so a completed slot's entries stay
        /// satisfied and waiting on them again is a no-op.
        std::vector<std::pair<VkSemaphore, uint64_t>> completion;
    };

    VkCommandBuffer AllocateCommandBufferForType(QueueType type) const;
    void FreeCommandBufferForType(QueueType type, VkCommandBuffer cmdBuf) const;
    /// @param type A resolved queue key.
    /// @return The timeline for the queue that key submits to.
    QueueTimeline& TimelineFor(QueueType type);
    /// Non-blocking: are all of the slot's completion values already reached?
    /// @param slot Slot to test.
    /// @return True when the slot's work has completed.
    bool IsComplete(const InFlightSlot& slot) const;
    /// Block until the slot's completion values are reached.
    /// @param slot Slot to wait for.
    void WaitFor(const InFlightSlot& slot) const;
    /// Return the slot's command buffers for reuse. Only safe once it has completed.
    void ReleaseSlotObjects(InFlightSlot& slot);

    VkRenderDevice* m_renderDevice = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<QueueTimeline> m_timelines;
    std::vector<InFlightSlot> m_slots;
    uint32_t m_nextSlot = 0;
};
}  // namespace Muyo::RenderGraph
