#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <string>
#include <vector>

#include "RenderPassParameters.h"

class Geometry;

class IRenderPass
{
    virtual ~IRenderPass();
    virtual VkCommandBuffer GetCommandBuffer() const = 0;
};

class RenderPass
{
public:
    virtual ~RenderPass() {}
    virtual VkCommandBuffer GetCommandBuffer(size_t idx = 0) const  = 0;
    virtual void CreatePipeline() = 0;
    virtual void PrepareRenderPass() {};
protected:
    std::vector<VkCommandBuffer> m_vCommandBuffers;
    std::vector<VkRenderPass> m_vRenderPasses;
    RenderPassParameters m_renderPassParameters;
};

// The pass render to swap chain
class RenderPassFinal : public RenderPass
{
public:
    RenderPassFinal(VkFormat swapChainFormat, bool bClearAttachments = true);
    virtual ~RenderPassFinal() override;
    virtual void SetSwapchainImageViews(const std::vector<VkImageView>& vImageViews,
                                        VkImageView depthImageView,
                                        uint32_t nWidth, uint32_t nHeight);

    virtual void RecordCommandBuffers();
    virtual void Resize(const std::vector<VkImageView>& vImageViews, VkImageView depthImageView, uint32_t uWidth, uint32_t uHeight);

    // Getters
    VkFramebuffer& GetFramebuffer(size_t idx)
    {
        assert(idx < m_vFramebuffers.size());
        return m_vFramebuffers[idx];
    }
    virtual VkCommandBuffer GetCommandBuffer(size_t idx) const override
    {
        assert(idx < m_vFramebuffers.size());
        return m_vCommandBuffers[idx];
    }
    virtual void CreatePipeline() override;

protected:
    std::vector<VkFramebuffer> m_vFramebuffers;  // Each framebuffer bind to a swapchain image

    VkExtent2D mRenderArea = {0, 0};

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

private:
    void DestroyFramebuffers();
};
