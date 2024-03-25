#include "Swapchain.h"

#include <cassert>
#include <limits>

#include "RenderResourceManager.h"
#include "VkRenderDevice.h"

namespace Muyo
{

Swapchain::~Swapchain()
{
    if (m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(GetRenderDevice()->GetInstance(), m_surface, nullptr);
    }
}
void Swapchain::CreateSwapchain(
    const VkSurfaceKHR& surface,
    const VkSurfaceFormatKHR& surfaceFormat,
    const VkPresentModeKHR& presentMode,
    uint32_t numBuffers)
{
    if (m_surface == VK_NULL_HANDLE)
    {
        m_surface = surface;
    }

    SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport();
    m_swapchainExtent = swapchainSupport.capabilities.currentExtent;

    m_swapchainFormat = surfaceFormat;

    assert(swapchainSupport.capabilities.minImageCount > 0 &&
           "Swapchain is not supported");
    assert((swapchainSupport.capabilities.maxImageCount == 0 || swapchainSupport.capabilities.maxImageCount >= numBuffers) &&
           "Too much buffers for swapchain");
    assert(numBuffers >= swapchainSupport.capabilities.minImageCount &&
           "Not enough image for swapchain");

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = numBuffers;
    createInfo.imageFormat = m_swapchainFormat.format;
    createInfo.imageColorSpace = m_swapchainFormat.colorSpace;
    createInfo.imageExtent = m_swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    // Blending with other windows
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    createInfo.presentMode = presentMode;
    // Don't care about the window above us
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_ASSERT(vkCreateSwapchainKHR(GetRenderDevice()->GetDevice(), &createInfo, nullptr, &m_swapchain));

    // Get swap chain images;
    std::vector<VkImage> swapchainImages;
    vkGetSwapchainImagesKHR(GetRenderDevice()->GetDevice(), m_swapchain, &numBuffers, nullptr);
    swapchainImages.resize(numBuffers);
    assert(swapchainImages.size() <= m_swapchainImageNames.size());
    vkGetSwapchainImagesKHR(GetRenderDevice()->GetDevice(), m_swapchain, &numBuffers, swapchainImages.data());
    m_swapchainImageResources.resize(swapchainImages.size());
    m_swapchainImageViews.resize(swapchainImages.size());
    // Create swapchain image views
    for (size_t i = 0; i < swapchainImages.size(); i++)
    {
        m_swapchainImageResources[i] = GetRenderResourceManager()->GetSwapchainImageResource(m_swapchainImageNames[i], swapchainImages[i], createInfo.imageExtent, m_swapchainFormat.format);
        m_swapchainImageViews[i]     = m_swapchainImageResources[i]->getView();
    }
}

void Swapchain::DestroySwapchain()
{
    // Wait for all swapchain images are consumed ?? Is it safe?
    vkDeviceWaitIdle(GetRenderDevice()->GetDevice());

    for (size_t i = 0; i < m_swapchainImageResources.size(); i++)
    {
        GetRenderResourceManager()->RemoveResource(m_swapchainImageNames[i]);
    }
    m_swapchainImageResources.clear();
    vkDestroySwapchainKHR(GetRenderDevice()->GetDevice(), m_swapchain, nullptr);
}

uint32_t Swapchain::GetNextImage(VkSemaphore& semaphore)
{
    uint32_t imageIndex;
    vkAcquireNextImageKHR(
        GetRenderDevice()->GetDevice(), m_swapchain, std::numeric_limits<uint64_t>::max(),
        semaphore, VK_NULL_HANDLE, &imageIndex);
    return imageIndex;
}

Swapchain::SwapchainSupportDetails Swapchain::QuerySwapchainSupport()
{
    Swapchain::SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface,
                                              &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(GetRenderDevice()->GetPhysicalDevice(),
                                         m_surface, &formatCount, nullptr);
    assert(formatCount > 0);
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface, &formatCount,
                                         details.formats.data());

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(GetRenderDevice()->GetPhysicalDevice(), m_surface,
                                              &presentCount, nullptr);
    assert(presentCount > 0);
    details.presentModes.resize(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        GetRenderDevice()->GetPhysicalDevice(), m_surface, &presentCount, details.presentModes.data());

    return details;
}

}  // namespace Muyo
