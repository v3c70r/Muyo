#pragma once
// Internal to muyo_rg: implementation detail, not part of the public surface. See docs/RenderGraph.md.
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "RenderGraphExecuteInfo.h"
#include "RenderGraphExecutionPlan.h"
#include "RenderGraphTraceRecorder.h"

namespace Muyo
{
class VkRenderDevice;
}

namespace Muyo::RenderGraph
{
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

    /// @return How many executions this executor may have in flight at once.
    uint32_t GetInFlightCount() const { return static_cast<uint32_t>(m_slots.size()); }

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
    /// @param plan      Plan the slot was recorded from.
    /// @param slot      Slot returned by AcquireSlot().
    /// @param info      External synchronization; both semaphores are optional.
    /// @param pObserver Optional observer of the timeline values this submission actually uses.
    void Submit(const RenderGraphExecutionPlan& plan, const ExecutionSlot& slot, const RenderGraphExecuteInfo& info,
                SubmissionObserver* pObserver = nullptr);

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
