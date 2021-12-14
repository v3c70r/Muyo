#pragma once
// Extract swapchain for easier management
// Probably need to inherit from a common interface of render destination
//
#include <vulkan/vulkan.h>
#include <vector>

class Swapchain
{
public:
    virtual ~Swapchain();
    void CreateSwapchain(
        const VkSurfaceKHR& surface,
        const VkSurfaceFormatKHR& surfaceFormat,
        const VkPresentModeKHR& presentMode,
        uint32_t numBuffers = 2);

    void DestroySwapchain();
    VkExtent2D GetSwapchainExtent() const { return m_swapchainExtent; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    const std::vector<VkImageView>& GetImageViews() const { return m_swapchainImageViews; }
    VkFormat GetImageFormat() const {return m_swapchainFormat.format;}
    uint32_t GetNextImage(VkSemaphore& semaphore);
    VkSwapchainKHR& GetSwapChain() { return m_swapchain; }

private:
    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapchainSupportDetails QuerySwapchainSupport();

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkExtent2D m_swapchainExtent = {0, 0};
    VkSurfaceFormatKHR m_swapchainFormat = {};

    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
};

