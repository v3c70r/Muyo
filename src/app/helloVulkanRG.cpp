// helloVulkanRG — the render graph's first consumer.
//
// The same kind of frame as helloVulkan, but the loop is the graph's: `FrameSync` acquires and presents,
// and every pass is a graph node. This is the design note's layer (b) — the render pipeline written
// against the graph's public API — so anything it cannot express is a surface gap rather than a
// workaround.
//
// Passes are moved over one at a time. This shell owns the loop and presents a cleared frame; the scene
// passes follow.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#if defined(USE_SDL)
#include "WindowSDL.h"
#else
#include "WindowGLFW.h"
#endif

#include "DescriptorManager.h"
#include "FrameSync.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "Swapchain.h"
#include "VkExtFuncsLoader.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

using namespace Muyo;
using namespace Muyo::RenderGraph;

namespace
{
constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kImagesInFlight = 2;

/// @return The window's instance extensions plus what the renderer needs.
std::vector<const char*> InstanceExtensions()
{
    std::vector<const char*> extensions;
    if (Window::GetVulkanInstanceExtensions(extensions) == 0 || extensions.empty())
    {
        throw std::runtime_error("helloVulkanRG: the window reported no Vulkan instance extensions.");
    }
    extensions.push_back("VK_KHR_get_physical_device_properties2");
    return extensions;
}

/// @return A surface format the swapchain supports.
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
}  // namespace

int main()
{
    if (!Window::Initialize("hello Vulkan (render graph)", kWidth, kHeight)) return -1;

    GetRenderDevice()->Initialize(InstanceExtensions(), {});
    VkExt::LoadInstanceFunctions(GetRenderDevice()->GetInstance());
    const VkSurfaceKHR surface = Window::GetVulkanSurface(GetRenderDevice()->GetInstance());

    // The device takes the surface so it can select a present-capable queue family.
    GetRenderDevice()->CreateDevice({VK_KHR_SWAPCHAIN_EXTENSION_NAME}, {}, &surface, {});
    GetMemoryAllocator()->Initalize(GetRenderDevice());
    GetRenderDevice()->CreateCommandPools();
    GetDescriptorManager()->CreateDescriptorPool();
    GetDescriptorManager()->CreateDescriptorSetLayouts();
    GetSamplerManager()->createSamplers();
    GetRenderResourceManager()->Initialize();

    {
        Swapchain swapchain;
        // The window owns the surface, so the swapchain must not destroy it.
        swapchain.CreateSwapchain(surface, ChooseSurfaceFormat(GetRenderDevice()->GetPhysicalDevice(), surface),
                                  VK_PRESENT_MODE_FIFO_KHR, kImagesInFlight, /*bOwnSurface=*/false);
        FrameSync frameSync(GetRenderDevice(), &swapchain, kImagesInFlight);

        while (!Window::ShouldQuit())
        {
            Window::ProcessEvents();

            const std::optional<FrameSync::Frame> frame = frameSync.BeginFrame();
            if (!frame.has_value())
            {
                // Out of date (a resize): recreate the swapchain and rebuild the per-image tracking.
                vkDeviceWaitIdle(GetRenderDevice()->GetDevice());
                const VkSurfaceKHR reclaimed = swapchain.GetSurface();
                swapchain.DestroySwapchain();
                swapchain.CreateSwapchain(reclaimed,
                                          ChooseSurfaceFormat(GetRenderDevice()->GetPhysicalDevice(), reclaimed),
                                          VK_PRESENT_MODE_FIFO_KHR, kImagesInFlight, /*bOwnSurface=*/false);
                frameSync.OnSwapchainRecreated();
                continue;
            }

            // A graph per frame, because the acquired image rotates and the graph fixes its imports at
            // Build. One graph per swapchain image is the optimisation and belongs with #40.
            RenderGraphBuilder builder(GetRenderDevice());
            builder.ImportResource("Present", swapchain.GetImageResource(frame->imageIndex));
            builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "ClearPresent",
                                                      .queueType = QueueType::GRAPHICS,
                                                      .resourceUses = {ResourceUse{.handle = ResourceHandle("Present"),
                                                                                   .io = ResourceIOType::WRITE,
                                                                                   .usage = ResourceUsage::PRESENT,
                                                                                   .kind = ResourceKind::IMAGE}},
                                                      .attachmentClearValues = {{{.color = {0.1F, 0.1F, 0.15F, 1.0F}}}},
                                                      .execute = [](RenderGraphNodeContext&) {}});
            builder.Build();
            builder.Execute(frameSync.ExecuteInfo(*frame));
            frameSync.Present(*frame);
        }

        vkDeviceWaitIdle(GetRenderDevice()->GetDevice());
        swapchain.DestroySwapchain();
    }

    GetSamplerManager()->destroySamplers();
    GetRenderResourceManager()->Unintialize();
    GetDescriptorManager()->DestroyDescriptorSetLayouts();
    GetDescriptorManager()->DestroyDescriptorPool();
    GetRenderDevice()->DestroyCommandPools();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
    Window::Uninitialize();
    return 0;
}
