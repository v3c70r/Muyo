#pragma once

#include <vulkan/vulkan.h>
#include <cassert>
#include <array>
#include <vector>

class RenderPass
{
public:
    virtual ~RenderPass() {}
protected:
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};

// The pass render to swap chain
class RenderPassFinal : RenderPass
{
public:
    RenderPassFinal(VkFormat swapChainFormat);
    ~RenderPassFinal();
    void SetSwapchainImageViews(std::vector<VkImageView>& vImageViews, VkImageView depthImageView,uint32_t nWidth, uint32_t nHeight);

    // Getters
    VkRenderPass& GetRenderPass() { return m_renderPass; }
    VkFramebuffer& GetFramebuffer(size_t idx)
    {
        assert(idx < m_vFramebuffers.size());
        return m_vFramebuffers[idx];
    }

private:
    void ClearFramebuffers();
    std::vector<VkFramebuffer> m_vFramebuffers;
};

class RenderPassUI : RenderPass
{
public:
    RenderPassUI();
    void SetRenderTargetImageView(VkImageView targetView, uint32_t nWidth, uint32_t nHeight);
    ~RenderPassUI();
private:
    VkFramebuffer m_framebuffer;
};
