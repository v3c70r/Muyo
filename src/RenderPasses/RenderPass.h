#pragma once

#include <vulkan/vulkan.h>

#include <cassert>
#include <vector>

#include "RenderPassParameters.h"
#include "Swapchain.h"

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
    RenderPassFinal(const Swapchain& swapchain, bool bClearAttachments);
    virtual ~RenderPassFinal() override;
    virtual void RecordCommandBuffers();

    virtual VkCommandBuffer GetCommandBuffer() const override
    {
        assert(m_nCurrentSwapchainImageIndex < m_vCommandBuffers.size());
        return m_vCommandBuffers[m_nCurrentSwapchainImageIndex];
    }
    virtual void CreatePipeline() override{};
    void SetCurrentSwapchainImageIndex(uint32_t nIndex) { m_nCurrentSwapchainImageIndex = nIndex; }
    void PrepareRenderPass() override{};

  protected:
    VkExtent2D m_renderArea           = { 0, 0 };

    std::vector<VkCommandBuffer> m_vCommandBuffers;
    std::vector<RenderPassParameters> m_vRenderPassParameters;
    std::vector<VkPipeline> m_vPipelines;

  private:
    void DestroyFramebuffers();
    uint32_t m_nCurrentSwapchainImageIndex = 0;
};
}    // namespace Muyo
