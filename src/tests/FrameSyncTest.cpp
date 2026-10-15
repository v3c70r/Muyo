#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <vector>

#include "FrameSync.h"
#include "GraphicsTestEnv.h"
#include "Swapchain.h"
#include "VkRenderDevice.h"

using namespace Muyo;

namespace
{
/// @return A surface format the headless surface supports, resolving the "no preference" case.
VkSurfaceFormatKHR ChooseSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
    if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        return VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    return formats[0];
}

/// Transition `image` to a clearable layout, clear it, and leave it ready to present.
void RecordClear(VkCommandBuffer cmd, VkImage image, float red)
{
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier2 toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toDst.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = image;
    toDst.subresourceRange = range;
    VkDependencyInfo toDstDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    toDstDep.imageMemoryBarrierCount = 1;
    toDstDep.pImageMemoryBarriers = &toDst;
    vkCmdPipelineBarrier2(cmd, &toDstDep);

    VkClearColorValue clear{};
    clear.float32[0] = red;
    clear.float32[1] = 0.0F;
    clear.float32[2] = 0.0F;
    clear.float32[3] = 1.0F;
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

    VkImageMemoryBarrier2 toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = image;
    toPresent.subresourceRange = range;
    VkDependencyInfo toPresentDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    toPresentDep.imageMemoryBarrierCount = 1;
    toPresentDep.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(cmd, &toPresentDep);
}
}  // namespace

TEST_CASE_METHOD(GraphicsTestEnv, "FrameSync: a headless swapchain presents several frames in flight", "[FrameSync]")
{
    // This is the present path the test suite could not exercise before: a real swapchain acquired and
    // presented for several frames in flight. The subject is exactly the per-frame bookkeeping, so a
    // machine without VK_EXT_headless_surface states that it cannot exercise it rather than passing
    // quietly (AGENTS section 4).
    if (!HasHeadlessSurface())
    {
        WARN("no VK_EXT_headless_surface here; the present path is not exercised by this run");
        return;
    }

    Swapchain swapchain;
    // A headless surface chooses its own constraints; Lavapipe needs at least 4 images, so ask for what
    // the surface reports rather than a fixed number.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(GetRenderDevice()->GetPhysicalDevice(), GetHeadlessSurface(), &caps);
    const uint32_t imageCount = caps.minImageCount > 3 ? caps.minImageCount : 3;
    swapchain.CreateSwapchain(GetHeadlessSurface(),
                              ChooseSurfaceFormat(GetRenderDevice()->GetPhysicalDevice(), GetHeadlessSurface()),
                              VK_PRESENT_MODE_FIFO_KHR, imageCount, /*bOwnSurface=*/false);
    REQUIRE(swapchain.GetSwapChain() != VK_NULL_HANDLE);
    REQUIRE(swapchain.GetImageCount() == imageCount);

    FrameSync sync(GetRenderDevice(), &swapchain, 3);

    const uint32_t framesToRun = 8;
    std::vector<uint32_t> slots;
    std::vector<VkSemaphore> acquireSemaphores;
    std::vector<VkCommandBuffer> commandBuffers;
    for (uint32_t i = 0; i < framesToRun; ++i)
    {
        const std::optional<FrameSync::Frame> frame = sync.BeginFrame();
        REQUIRE(frame.has_value());
        slots.push_back(frame->frameIndex);
        acquireSemaphores.push_back(frame->imageAvailable);

        VkCommandBuffer cmd = GetRenderDevice()->AllocateReusablePrimaryCommandbuffer();
        commandBuffers.push_back(cmd);
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        REQUIRE(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS);
        RecordClear(cmd, frame->image, static_cast<float>(i) / static_cast<float>(framesToRun));
        REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);

        VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        waitInfo.semaphore = frame->imageAvailable;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signalInfo.semaphore = frame->renderFinished;
        VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmdInfo.commandBuffer = cmd;
        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalInfo;
        REQUIRE(vkQueueSubmit2(GetRenderDevice()->GetGraphicsQueue(), 1, &submitInfo, frame->inFlight) == VK_SUCCESS);

        const VkResult presentResult = sync.Present(*frame);
        CHECK((presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR));
    }

    // A presented frame's command buffer may still be pending, so drain before freeing them.
    REQUIRE(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()) == VK_SUCCESS);
    for (VkCommandBuffer cmd : commandBuffers) GetRenderDevice()->FreeReusablePrimaryCommandbuffer(cmd);

    // The slot cycles 0,1,2,0,1,2,0,1, and each slot has its own acquire semaphore - the two things
    // RenderPassManager gets wrong (one semaphore for the process, fence indexed by image).
    const std::vector<uint32_t> expectedSlots = {0, 1, 2, 0, 1, 2, 0, 1};
    CHECK(slots == expectedSlots);
    REQUIRE(acquireSemaphores.size() >= 3);
    CHECK(acquireSemaphores[0] != acquireSemaphores[1]);
    CHECK(acquireSemaphores[0] != acquireSemaphores[2]);
    CHECK(acquireSemaphores[1] != acquireSemaphores[2]);

    swapchain.DestroySwapchain();
}
