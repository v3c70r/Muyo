#pragma once
// Extract swapchain for easier management
// Probably need to inherit from a common interface of render destination
//
#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <vector>
namespace Muyo
{
class SwapchainImageResource;
class Swapchain
{
public:
    virtual ~Swapchain();
    /// Create (or recreate) the swapchain.
    /// @param surface       Surface to present to. Stored as-is.
    /// @param surfaceFormat Preferred format; the surface may report another.
    /// @param presentMode   Preferred present mode.
    /// @param numBuffers    Images to request. Must be within the surface's reported range.
    /// @param bOwnSurface   True when this object destroys `surface` on teardown. False when the caller
    ///                      owns it (a device created with the same surface, for instance), which is what
    ///                      lets one host hand a single surface to both.
    /// @param requestedExtent Window size to use when the surface lets the application choose, as Wayland
    ///                      does (it reports `0xFFFFFFFF` where X11 reports the window size). Ignored when
    ///                      the surface reports a fixed extent. {0,0} falls back to 1280x720.
    void CreateSwapchain(const VkSurfaceKHR& surface, const VkSurfaceFormatKHR& surfaceFormat,
                         const VkPresentModeKHR& presentMode, uint32_t numBuffers = 2, bool bOwnSurface = true,
                         VkExtent2D requestedExtent = {0, 0});

    void DestroySwapchain();
    VkExtent2D GetSwapchainExtent() const { return m_swapchainExtent; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    VkFormat GetImageFormat() const { return m_swapchainFormat.format; }
    /// Acquire the next image, reporting the result so a caller can react to `VK_ERROR_OUT_OF_DATE_KHR`
    /// and `VK_SUBOPTIMAL_KHR` instead of treating every outcome as success.
    /// @param semaphore     Signalled when the image is available.
    /// @param outImageIndex Receives the acquired image index.
    /// @return The `vkAcquireNextImageKHR` result.
    VkResult AcquireNextImage(VkSemaphore semaphore, uint32_t& outImageIndex);
    /// Legacy convenience: returns the index and ignores the result. A caller that presents should use
    /// `AcquireNextImage`.
    uint32_t GetNextImage(VkSemaphore& semaphore);
    /// @return The images and views, indexed by the acquired image index.
    uint32_t GetImageCount() const { return static_cast<uint32_t>(m_swapchainImages.size()); }
    VkImage GetImage(uint32_t index) const { return m_swapchainImages.at(index); }
    VkImageView GetImageView(uint32_t index) const { return m_swapchainImageViews.at(index); }
    /// @param index Acquired image index.
    /// @return The `ImageResource` wrapping that swapchain image, for a graph to import. Not owned.
    SwapchainImageResource* GetImageResource(uint32_t index) const { return m_swapchainImageResources.at(index); }
    VkSwapchainKHR& GetSwapChain() { return m_swapchain; }

    std::vector<std::string> GetSwapchainResourceNames() const
    {
        return std::vector<std::string>(m_swapchainResourceNames.begin(),
                                        m_swapchainResourceNames.begin() + m_swapchainImageViews.size());
    }

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
    const std::array<std::string, 4> m_swapchainResourceNames = {"swapchainImage0_",
                                                                 "swapchainImage1_"
                                                                 "swapchainImage2_"
                                                                 "swapchainImage3_"};

    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;  // todo: remove this
    /// True when ~Swapchain destroys m_surface. False when the caller owns it.
    bool m_bOwnsSurface = true;
};

}  // namespace Muyo
