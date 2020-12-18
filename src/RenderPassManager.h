#pragma once
#include <memory>
#include <array>
#include <vector>
#include <vulkan/vulkan.h>

class RenderPass;
enum RenderPassNames
{
    RENDERPASS_GBUFFER,
    RENDERPASS_IBL,
    RENDERPASS_UI,
    RENDERPASS_FINAL,

    RENDERPASS_COUNT
};
class RenderPassManager
{
    void Initialize(uint32_t uWidth, uint32_t uHeight, VkFormat swapchainFormat);
    void SetSwapchainImageViews(std::vector<VkImageView> &vImageViews, VkImageView depthImageView);
    void OnResize(uint32_t uWidth, uint32_t uHeight);
private:
    std::array<std::unique_ptr<RenderPass>, RENDERPASS_COUNT> m_vpRenderPasses;
    uint32_t m_uWidth = 0;
    uint32_t m_uHeight = 0;
};
