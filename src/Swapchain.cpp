#include "Swapchain.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#include "RenderResourceManager.h"
#include "VkRenderDevice.h"

namespace Muyo
{

Swapchain::~Swapchain()
{
    // Only a surface this object owns. A host that created the surface for its device and handed a
    // non-owning reference here destroys it itself.
    if (m_bOwnsSurface && m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(GetRenderDevice()->GetInstance(), m_surface, nullptr);
    }
}
void Swapchain::CreateSwapchain(const VkSurfaceKHR& surface, const VkSurfaceFormatKHR& surfaceFormat,
                                const VkPresentModeKHR& presentMode, uint32_t numBuffers, bool bOwnSurface)
{
    if (m_surface == VK_NULL_HANDLE)
    {
        m_surface = surface;
        m_bOwnsSurface = bOwnSurface;
    }

    SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport();
    const VkSurfaceCapabilitiesKHR& caps = swapchainSupport.capabilities;
    // A window surface reports its extent here. A headless surface reports the special
    // "application chooses" value (0xFFFFFFFF), so fall back to a default clamped to what it allows.
    m_swapchainExtent = caps.currentExtent;
    if (m_swapchainExtent.width == std::numeric_limits<uint32_t>::max() ||
        m_swapchainExtent.height == std::numeric_limits<uint32_t>::max())
    {
        m_swapchainExtent = {std::clamp(1280u, caps.minImageExtent.width, caps.maxImageExtent.width),
                             std::clamp(720u, caps.minImageExtent.height, caps.maxImageExtent.height)};
    }
    if (m_swapchainExtent.width == 0 || m_swapchainExtent.height == 0)
    {
        throw std::runtime_error("Swapchain: the surface reports a zero extent, so there is nothing to present to.");
    }

    m_swapchainFormat = surfaceFormat;

    // Runtime checks, not asserts: a surface that offers no usable configuration is input, and a Release
    // build must still say so rather than create a swapchain the driver will reject (AGENTS section 3).
    if (caps.minImageCount == 0)
    {
        throw std::runtime_error("Swapchain: the surface reports no supported image count.");
    }
    if (numBuffers < caps.minImageCount)
    {
        throw std::runtime_error("Swapchain: " + std::to_string(numBuffers) +
                                 " images requested, but the surface needs at least " +
                                 std::to_string(caps.minImageCount) + ".");
    }
    if (caps.maxImageCount != 0 && numBuffers > caps.maxImageCount)
    {
        throw std::runtime_error("Swapchain: " + std::to_string(numBuffers) +
                                 " images requested, but the surface allows at most " +
                                 std::to_string(caps.maxImageCount) + ".");
    }

    // Take TRANSFER_SRC/DST when the surface offers them: a caller copies the presented image out for a
    // readback, and a test clears it before presenting. COLOR_ATTACHMENT is the one required bit.
    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    {
        imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    {
        imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    // OPAQUE is preferred; a headless surface may offer only another mode, so pick from what it reports.
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0)
    {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
        {
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        }
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
        {
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
        else
        {
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = numBuffers;
    createInfo.imageFormat = m_swapchainFormat.format;
    createInfo.imageColorSpace = m_swapchainFormat.colorSpace;
    createInfo.imageExtent = m_swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = imageUsage;

    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = compositeAlpha;

    createInfo.presentMode = presentMode;
    // Don't care about the window above us
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_ASSERT(vkCreateSwapchainKHR(GetRenderDevice()->GetDevice(), &createInfo, nullptr, &m_swapchain));

    // Get swap chain images;
    uint32_t imageCount = 0;
    VK_ASSERT(vkGetSwapchainImagesKHR(GetRenderDevice()->GetDevice(), m_swapchain, &imageCount, nullptr));
    if (imageCount > m_swapchainResourceNames.size())
    {
        throw std::runtime_error("Swapchain: the surface returned " + std::to_string(imageCount) +
                                 " images, more than the " + std::to_string(m_swapchainResourceNames.size()) +
                                 " resource names available.");
    }
    m_swapchainImages.resize(imageCount);
    VK_ASSERT(
        vkGetSwapchainImagesKHR(GetRenderDevice()->GetDevice(), m_swapchain, &imageCount, m_swapchainImages.data()));
    m_swapchainImageResources.resize(imageCount);
    m_swapchainImageViews.resize(imageCount);
    // Create swapchain image views
    for (uint32_t i = 0; i < imageCount; i++)
    {
        m_swapchainImageResources[i] = GetRenderResourceManager()->GetSwapchainImageResource(
            m_swapchainResourceNames[i], m_swapchainImages[i], createInfo.imageExtent, m_swapchainFormat.format);
        m_swapchainImageViews[i] = m_swapchainImageResources[i]->getView();
    }
}

void Swapchain::DestroySwapchain()
{
    // Wait for all swapchain images are consumed ?? Is it safe?
    vkDeviceWaitIdle(GetRenderDevice()->GetDevice());

    for (size_t i = 0; i < m_swapchainImageResources.size(); i++)
    {
        GetRenderResourceManager()->RemoveResource(m_swapchainResourceNames[i]);
    }
    m_swapchainImageResources.clear();
    m_swapchainImages.clear();
    m_swapchainImageViews.clear();
    vkDestroySwapchainKHR(GetRenderDevice()->GetDevice(), m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

VkResult Swapchain::AcquireNextImage(VkSemaphore semaphore, uint32_t& outImageIndex)
{
    return vkAcquireNextImageKHR(GetRenderDevice()->GetDevice(), m_swapchain, std::numeric_limits<uint64_t>::max(),
                                 semaphore, VK_NULL_HANDLE, &outImageIndex);
}

uint32_t Swapchain::GetNextImage(VkSemaphore& semaphore)
{
    uint32_t imageIndex = 0;
    AcquireNextImage(semaphore, imageIndex);
    return imageIndex;
}

Swapchain::SwapchainSupportDetails Swapchain::QuerySwapchainSupport()
{
    Swapchain::SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface, &formatCount, nullptr);
    if (formatCount == 0) throw std::runtime_error("Swapchain: the surface reports no supported formats.");
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface, &formatCount,
                                         details.formats.data());

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface, &presentCount,
                                              nullptr);
    if (presentCount == 0) throw std::runtime_error("Swapchain: the surface reports no supported present modes.");
    details.presentModes.resize(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface, &presentCount,
                                              details.presentModes.data());

    return details;
}

}  // namespace Muyo
