#include "Swapchain.h"
#include "VkRenderDevice.h"
#include <cassert>
#include <limits>

void Swapchain::createSwapchain(const VkSurfaceFormatKHR& surfaceFormat,
                                const VkPresentModeKHR& presentMode,
                                uint32_t numBuffers)
{
    if (m_surface == VK_NULL_HANDLE)
    {
        createSurface();
    }

    SwapchainSupportDetails swapchainSupport = querySwapchainSupport();
    m_swapchainExtent = swapchainSupport.capabilities.currentExtent;

    m_swapchainFormat = surfaceFormat;

    assert(swapchainSupport.capabilities.maxImageCount > 0 &&
           "Swapchain is not supported");
    assert(swapchainSupport.capabilities.maxImageCount >= numBuffers &&
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

    assert(vkCreateSwapchainKHR(GetRenderDevice()->GetDevice(), &createInfo, nullptr, &m_swapchain) ==
           VK_SUCCESS);

    // Get swap chain images;
    vkGetSwapchainImagesKHR(GetRenderDevice()->GetDevice(), m_swapchain, &numBuffers, nullptr);
    m_swapchainImages.resize(numBuffers);
    vkGetSwapchainImagesKHR(GetRenderDevice()->GetDevice(), m_swapchain, &numBuffers,
                            m_swapchainImages.data());

    // Create swapchain image views
    m_swapchainImageViews.resize(numBuffers);
    for (size_t i = 0; i < m_swapchainImageViews.size(); i++)
    {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchainFormat.format;

        // Swizzles

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // subresource
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        assert(vkCreateImageView(GetRenderDevice()->GetDevice(), &createInfo, nullptr,
                                 &m_swapchainImageViews[i]) == VK_SUCCESS);
    }
}

void Swapchain::destroySwapchain()
{
    // Wait for all swapchain images are consumed ?? Is it safe?
    vkDeviceWaitIdle(GetRenderDevice()->GetDevice());

    // Destroy imageviews
    for (auto& imageView : m_swapchainImageViews)
    {
        vkDestroyImageView(GetRenderDevice()->GetDevice(), imageView, nullptr);
    }
    m_swapchainImageViews.clear();
    // Clear swapchian, I assume the swapchian images are destroyed with it
    vkDestroySwapchainKHR(GetRenderDevice()->GetDevice(), m_swapchain, nullptr);
    m_swapchainImages.clear();
}

uint32_t Swapchain::getNextImage(VkSemaphore& semaphore)
{
    uint32_t imageIndex;
   vkAcquireNextImageKHR(
        GetRenderDevice()->GetDevice(), m_swapchain, std::numeric_limits<uint64_t>::max(),
        semaphore, VK_NULL_HANDLE, &imageIndex);
   return imageIndex;
}

Swapchain::SwapchainSupportDetails Swapchain::querySwapchainSupport()
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


