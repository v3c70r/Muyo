#pragma once
// Extract swapchain for easier management
// Probably need to inherit from a common interface of render destination
//
#include <array>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
namespace Muyo
{
class SwapchainImageResource;
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
    VkFormat GetImageFormat() const { return m_swapchainFormat.format; }
    uint32_t GetNextImage(VkSemaphore& semaphore);
    VkSwapchainKHR& GetSwapChain() { return m_swapchain; }

    std::vector<std::string> GetSwapchainResourceNames() const { return std::vector<std::string>(m_swapchainResourceNames.begin(), m_swapchainResourceNames.begin() + m_swapchainImageViews.size()); }

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

    std::vector<SwapchainImageResource*> m_swapchainImageResources;
    const std::array<std::string, 4> m_swapchainResourceNames = {
        "swapchainImage0_",
        "swapchainImage1_"
        "swapchainImage2_"
        "swapchainImage3_"
    };

    std::vector<VkImageView> m_swapchainImageViews;    // todo: remove this
};

}  // namespace Muyo
