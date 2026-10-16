#include "FrameSync.h"

#include <limits>
#include <stdexcept>
#include <string>

#include "Swapchain.h"
#include "VkRenderDevice.h"

namespace Muyo
{
namespace
{
/// @param device Device to create on.
/// @return A binary semaphore, ready to signal or wait on.
VkSemaphore CreateBinarySemaphore(VkDevice device)
{
    VkSemaphoreCreateInfo createInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore));
    return semaphore;
}

/// @param device     Device to create on.
/// @param bSignalled Whether it starts signalled, so the first wait does not deadlock.
/// @return A fence.
VkFence CreateFence(VkDevice device, bool bSignalled)
{
    VkFenceCreateInfo createInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (bSignalled) createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateFence(device, &createInfo, nullptr, &fence));
    return fence;
}
}  // namespace

FrameSync::FrameSync(VkRenderDevice* device, Swapchain* swapchain, uint32_t framesInFlight)
    : m_device(device), m_swapchain(swapchain), m_framesInFlight(framesInFlight == 0 ? 1 : framesInFlight)
{
    if (m_device == nullptr || m_swapchain == nullptr)
    {
        throw std::runtime_error("FrameSync needs a device and a swapchain.");
    }

    const VkDevice vkDevice = m_device->GetDevice();
    m_imageAvailable.resize(m_framesInFlight);
    m_inFlight.resize(m_framesInFlight);
    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        m_imageAvailable[i] = CreateBinarySemaphore(vkDevice);
        // Signalled, so the first BeginFrame's wait returns instead of blocking forever.
        m_inFlight[i] = CreateFence(vkDevice, true);
    }
    OnSwapchainRecreated();
}

FrameSync::~FrameSync()
{
    // A frame acquired but never presented would leave work pending; wait before freeing anything.
    vkDeviceWaitIdle(m_device->GetDevice());
    const VkDevice vkDevice = m_device->GetDevice();
    for (VkSemaphore semaphore : m_imageAvailable)
    {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(vkDevice, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : m_renderFinished)
    {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(vkDevice, semaphore, nullptr);
    }
    for (VkFence fence : m_inFlight)
    {
        if (fence != VK_NULL_HANDLE) vkDestroyFence(vkDevice, fence, nullptr);
    }
}

void FrameSync::OnSwapchainRecreated()
{
    const VkDevice vkDevice = m_device->GetDevice();
    // Render-finished semaphores are per image, so a resize rebuilds them. The caller drains the old
    // images before recreating, so nothing is still waiting on the old ones.
    for (VkSemaphore semaphore : m_renderFinished)
    {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(vkDevice, semaphore, nullptr);
    }
    m_renderFinished.assign(m_swapchain->GetImageCount(), VK_NULL_HANDLE);
    for (VkSemaphore& semaphore : m_renderFinished) semaphore = CreateBinarySemaphore(vkDevice);

    // The caller drains the old images before recreating, so the tracking restarts empty.
    m_imagesInFlight.assign(m_swapchain->GetImageCount(), VK_NULL_HANDLE);
    m_currentFrame = 0;
}

std::optional<FrameSync::Frame> FrameSync::BeginFrame()
{
    const VkDevice vkDevice = m_device->GetDevice();
    VkFence& slotFence = m_inFlight[m_currentFrame];
    VK_ASSERT(vkWaitForFences(vkDevice, 1, &slotFence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
    VK_ASSERT(vkResetFences(vkDevice, 1, &slotFence));

    uint32_t imageIndex = 0;
    const VkResult acquireResult = m_swapchain->AcquireNextImage(m_imageAvailable[m_currentFrame], imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) return std::nullopt;
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("FrameSync: vkAcquireNextImageKHR failed with VkResult " +
                                 std::to_string(acquireResult));
    }

    if (imageIndex >= m_imagesInFlight.size())
    {
        throw std::runtime_error("FrameSync: the acquired image index " + std::to_string(imageIndex) +
                                 " is out of range for " + std::to_string(m_imagesInFlight.size()) + " images.");
    }
    // The image may still be rendering in an earlier frame, and waiting on that frame's fence is what
    // makes it safe to touch. Unless the stored fence is *this* slot's: it was just waited on and reset
    // above, so the image's last use by this slot is already complete, and waiting on the reset fence
    // would deadlock. That happens as soon as the swapchain hands back an image the slot recently used.
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE && m_imagesInFlight[imageIndex] != slotFence)
    {
        VK_ASSERT(
            vkWaitForFences(vkDevice, 1, &m_imagesInFlight[imageIndex], VK_TRUE, std::numeric_limits<uint64_t>::max()));
    }
    m_imagesInFlight[imageIndex] = slotFence;

    Frame frame;
    frame.frameIndex = m_currentFrame;
    frame.imageIndex = imageIndex;
    frame.imageAvailable = m_imageAvailable[m_currentFrame];
    frame.renderFinished = m_renderFinished[imageIndex];
    frame.inFlight = slotFence;
    frame.image = m_swapchain->GetImage(imageIndex);
    return frame;
}

RenderGraph::RenderGraphExecuteInfo FrameSync::ExecuteInfo(const Frame& frame) const
{
    RenderGraph::RenderGraphExecuteInfo info;
    info.waitSemaphore = frame.imageAvailable;
    info.signalSemaphore = frame.renderFinished;
    // The caller does not submit directly, so the graph must signal this frame's fence - otherwise the
    // slot is never released and the next reuse of it blocks forever.
    info.signalFence = frame.inFlight;
    return info;
}

VkResult FrameSync::Present(const Frame& frame)
{
    VkSwapchainKHR swapchain = m_swapchain->GetSwapChain();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &frame.imageIndex;

    const VkResult result = vkQueuePresentKHR(m_device->GetPresentQueue(), &presentInfo);
    m_currentFrame = (m_currentFrame + 1) % m_framesInFlight;
    return result;
}
}  // namespace Muyo
