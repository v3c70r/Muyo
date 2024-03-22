#pragma once

#include <vulkan/vulkan.h>

#include <cassert>
#include <vector>

#include "RenderPassParameters.h"

namespace Muyo
{
class Geometry;

class IRenderPass
{
  public:
    virtual ~IRenderPass(){};
    virtual VkCommandBuffer GetCommandBuffer() const = 0;
    virtual void CreatePipeline()                    = 0;
    virtual void PrepareRenderPass()                 = 0;
};

class RenderPass : public IRenderPass
{
  public:
    virtual void PrepareRenderPass() override{};

  protected:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    RenderPassParameters m_renderPassParameters;
};

// The pass render to swap chain
class RenderPassFinal : public IRenderPass
{
  public:
    RenderPassFinal(VkFormat swapChainFormat, bool bClearAttachments = true);
    virtual ~RenderPassFinal() override;
    virtual void SetSwapchainImageViews(const std::vector<VkImageView> &vImageViews,
                                        VkImageView depthImageView,
                                        uint32_t nWidth, uint32_t nHeight);

    virtual void RecordCommandBuffers();
    virtual void Resize(const std::vector<VkImageView> &vImageViews, VkImageView depthImageView, uint32_t uWidth, uint32_t uHeight);

    virtual VkCommandBuffer GetCommandBuffer() const override
    {
        assert(m_nCurrentSwapchainImageIndex < m_vFramebuffers.size());
        return m_vCommandBuffers[m_nCurrentSwapchainImageIndex];
    }
    virtual void CreatePipeline() override;

    void SetCurrentSwapchainImageIndex(uint32_t nIndex) { m_nCurrentSwapchainImageIndex = nIndex; }
    void PrepareRenderPass() override{};

  protected:
    std::vector<VkFramebuffer> m_vFramebuffers;    // Each framebuffer bind to a swapchain image

    VkExtent2D mRenderArea = {0, 0};

    VkPipeline m_pipeline             = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    std::vector<VkCommandBuffer> m_vCommandBuffers;
    std::vector<VkRenderPass> m_vRenderPasses;

  private:
    void DestroyFramebuffers();
    uint32_t m_nCurrentSwapchainImageIndex = 0;
};
}    // namespace Muyo
