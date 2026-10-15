#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "RenderGraph/RenderGraphExecutor.h"

namespace Muyo
{
class VkRenderDevice;
class Swapchain;

/// Per-frame acquire/present synchronization for a swapchain, owned by the host.
///
/// The graph owns orchestration and expresses completion as timeline values; the host owns the window,
/// the swapchain and presenting. This is the host half: it hands the graph the semaphores to wait on and
/// signal for a frame, and presents the acquired image once that frame's work has completed. The caller
/// holds no `VkSemaphore`, `VkFence` or image index.
///
/// Per frame of `framesInFlight` there is an `imageAvailable` binary and an in-flight fence; per swapchain
/// image there is a `renderFinished` binary and the fence of the frame currently using it. An image is
/// never re-acquired while still in use, and its render-finished semaphore is only re-signalled once a
/// re-acquire proves the previous present consumed it. `RenderPassManager` gets the first wrong by keeping
/// one acquire semaphore for the process and indexing its fence by image rather than by frame.
class FrameSync
{
public:
    /// One frame's handles and image.
    struct Frame
    {
        uint32_t frameIndex = 0;                      ///< Which in-flight slot.
        uint32_t imageIndex = 0;                      ///< Acquired swapchain image.
        VkSemaphore imageAvailable = VK_NULL_HANDLE;  ///< Wait on this before rendering the image.
        VkSemaphore renderFinished = VK_NULL_HANDLE;  ///< Signal this when the frame's work completes.
        VkFence inFlight = VK_NULL_HANDLE;            ///< The slot's fence, for a caller that waits.
        VkImage image = VK_NULL_HANDLE;               ///< The acquired image.
    };

    /// @param device          Device the swapchain belongs to, with a present queue.
    /// @param swapchain       Swapchain to acquire from and present to. Not owned.
    /// @param framesInFlight  In-flight slots; at least 1.
    FrameSync(VkRenderDevice* device, Swapchain* swapchain, uint32_t framesInFlight);
    /// Waits idle, then frees the semaphores and fences.
    ~FrameSync();

    /// The helper owns raw Vulkan handles, so copying would double-free.
    FrameSync(const FrameSync&) = delete;
    /// Deleted for the same reason as the copy constructor.
    FrameSync& operator=(const FrameSync&) = delete;

    /// Wait for the current slot's previous frame, acquire the next swapchain image, and return the
    /// frame.
    /// @return The frame, or `nullopt` when the swapchain is out of date and must be recreated.
    std::optional<Frame> BeginFrame();

    /// @param frame A frame from `BeginFrame`.
    /// @return The wait/signal to pass to the graph's `Execute`, so the graph waits for the acquired
    ///         image and signals present-wait on completion.
    RenderGraph::RenderGraphExecuteInfo ExecuteInfo(const Frame& frame) const;

    /// Present a frame's image, waiting on its `renderFinished`.
    /// @param frame A frame from `BeginFrame`.
    /// @return The present result; `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` mean the swapchain
    ///         must be recreated.
    VkResult Present(const Frame& frame);

    /// Call after the swapchain is recreated (a resize): re-sizes the per-image tracking and restarts
    /// the slot cycle. Safe only once the old images are drained.
    void OnSwapchainRecreated();

    /// @return How many frames may be in flight.
    uint32_t FramesInFlight() const { return m_framesInFlight; }

private:
    VkRenderDevice* m_device = nullptr;
    Swapchain* m_swapchain = nullptr;
    uint32_t m_framesInFlight = 1;
    uint32_t m_currentFrame = 0;
    std::vector<VkSemaphore> m_imageAvailable;
    /// One per *swapchain image*, not per frame: the semaphore is signalled by the submit and waited on by
    /// present, so a frame-indexed one could be re-signalled while the swapchain still waits on it
    /// (`VUID-vkQueueSubmit2-semaphore-03869`). Re-acquiring an image proves its previous present
    /// completed and the semaphore was consumed, so keying by image is what makes reuse safe.
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlight;
    /// One entry per swapchain image: the fence of the frame currently rendering it, or null.
    std::vector<VkFence> m_imagesInFlight;
};
}  // namespace Muyo
