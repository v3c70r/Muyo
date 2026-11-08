#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>

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
    ///
    /// With a timeline, 0 is legal but waits for nothing - a counter starts at 0, so the wait is already
    /// satisfied. Pass the value you actually mean.
    uint64_t waitValue = 0;
    /// Signalled once *all* of this execution's queues have completed - e.g. for vkQueuePresentKHR.
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;
    /// Value to signal when `signalSemaphore` is a timeline semaphore. Must be greater than the
    /// semaphore's current value, so the default 0 is never valid for one. Ignored for a binary
    /// semaphore.
    ///
    /// A signal of 0 on a counter at 0 is rejected (VUID-VkSubmitInfo2-semaphore-03882), so a caller
    /// chaining onto a timeline it owns has to pass the value it intends - which is also the value the
    /// next execution waits for. If the only question is "has this finished?", `signalFence` needs no
    /// value at all.
    uint64_t signalValue = 0;
    /// Signalled once *all* of this execution's queues have completed. Unlike `signalSemaphore` this is
    /// waitable from the host, and unlike a query it needs no submission of the caller's own.
    VkFence signalFence = VK_NULL_HANDLE;
};
}  // namespace Muyo::RenderGraph
