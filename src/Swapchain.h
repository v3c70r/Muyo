#pragma once
// Extract swapchain for easier management
// Probably need to inherit from a common interface of render destination
//
#include <vulkan/vulkan.h>
#include <vector>

class Swapchain
{
public:
    void createSwapchain(const VkSurfaceFormatKHR& surfaceFormat,
                         const VkPresentModeKHR& presentMode,
                         uint32_t numBuffers = 2);

    void destroySwapchain();
    virtual void createSurface() = 0;
    virtual void destroySurface() = 0;
    VkExtent2D getSwapchainExtent() const { return m_swapchainExtent; }
    VkSurfaceKHR getSurface() const { return m_surface; }
    std::vector<VkImageView>& getImageViews() { return m_swapchainImageViews; }
    VkFormat getImageFormat() const {return m_swapchainFormat.format;}
    uint32_t getNextImage(VkSemaphore& semaphore);
    VkSwapchainKHR& getSwapChain() { return m_swapchain; }

protected:
    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapchainSupportDetails querySwapchainSupport();

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkExtent2D m_swapchainExtent = {0, 0};
    VkSurfaceFormatKHR m_swapchainFormat = {};

    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
};

