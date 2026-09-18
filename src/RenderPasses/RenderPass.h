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
    virtual ~IRenderPass()= default;
    virtual VkCommandBuffer GetCommandBuffer() const = 0;
    virtual void CreatePipeline()                    = 0;
    virtual void PrepareRenderPass()                 = 0;
    virtual const std::vector<const IRenderResource*>& GetInputResources() const = 0;
    virtual const std::vector<const IRenderResource*>& GetOutputResources() const = 0;
    virtual std::string GetName() const = 0;
};

class RenderPass : public IRenderPass
{
public:
     void PrepareRenderPass() override{};
     const std::vector<const IRenderResource*>& GetInputResources() const override
     {
         return m_renderPassParameters.GetInputResources();
     }
     const std::vector<const IRenderResource*>& GetOutputResources() const override
     {
         return m_renderPassParameters.GetOutputResources();
     }
     std::string GetName() const override { return m_renderPassParameters.GetName(); }

protected:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    RenderPassParameters m_renderPassParameters;
};

// The pass render to swap chain
class RenderPassFinal : public IRenderPass
{
public:
    RenderPassFinal(const Swapchain& swapchain, bool bClearAttachments);
    ~RenderPassFinal() override;
    virtual void RecordCommandBuffers();

    VkCommandBuffer GetCommandBuffer() const override
    {
        assert(m_nCurrentSwapchainImageIndex < m_vCommandBuffers.size());
        return m_vCommandBuffers[m_nCurrentSwapchainImageIndex];
    }
    void CreatePipeline() override{};
    void SetCurrentSwapchainImageIndex(uint32_t nIndex) { m_nCurrentSwapchainImageIndex = nIndex; }
    void PrepareRenderPass() override{};

    const std::vector<const IRenderResource*>& GetInputResources() const override
    {
        return m_vRenderPassParameters[0].GetInputResources();
    }
    const std::vector<const IRenderResource*>& GetOutputResources() const override
    {
        return m_vRenderPassParameters[0].GetOutputResources();
    }
    std::string GetName() const override { return m_vRenderPassParameters[0].GetName(); }

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
