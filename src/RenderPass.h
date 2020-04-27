#pragma once

#include <vulkan/vulkan.h>
#include <cassert>
#include <array>
#include <vector>

class RenderPass
{
public:
    virtual ~RenderPass() {}
    VkRenderPass GetPass() { return m_renderPass; }

protected:
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};

// The pass render to swap chain
class RenderPassFinal : public RenderPass
{
public:
    RenderPassFinal(VkFormat swapChainFormat);
    ~RenderPassFinal();
    void SetSwapchainImageViews(std::vector<VkImageView>& vImageViews, VkImageView depthImageView,uint32_t nWidth, uint32_t nHeight);
    void RecordOnce(VkBuffer vertexBuffer, VkBuffer indexBuffer,
                uint32_t numIndices,
                //std::vector<VkCommandBuffer>& commandBuffers,
                VkPipeline pipeline, VkPipelineLayout pipelineLayout,
                std::vector<VkDescriptorSet> vDescriptorSets);

    // Getters
    VkRenderPass& GetRenderPass() { return m_renderPass; }
    VkFramebuffer& GetFramebuffer(size_t idx)
    {
        assert(idx < m_vFramebuffers.size());
        return m_vFramebuffers[idx];
    }
    VkCommandBuffer& GetCommandBuffer(size_t idx)
    {
        assert(idx < m_vFramebuffers.size());
        return m_vCommandBuffers[idx];
    }

private:
    void ClearFramebuffers();
    std::vector<VkFramebuffer> m_vFramebuffers;
    VkExtent2D mRenderArea = {0, 0};
    std::vector<VkCommandBuffer> m_vCommandBuffers; // save the command buffer to be submitted
};

class RenderPassUI : public RenderPass
{
public:
    RenderPassUI();
    void SetRenderTargetImageView(VkImageView targetView, uint32_t nWidth, uint32_t nHeight);
    ~RenderPassUI();
private:
    VkFramebuffer m_framebuffer;
};
