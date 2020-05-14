#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <string>
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
    RenderPassFinal(VkFormat swapChainFormat, bool bClearAttachments = true);
    virtual ~RenderPassFinal() override;
    void SetSwapchainImageViews(std::vector<VkImageView>& vImageViews,
                                VkImageView depthImageView, uint32_t nWidth,
                                uint32_t nHeight);
    void RecordOnce(VkBuffer vertexBuffer, VkBuffer indexBuffer,
                    uint32_t numIndices, VkPipeline pipeline,
                    VkPipelineLayout pipelineLayout,
                    VkDescriptorSet descriptorSet);

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

protected:
    std::vector<VkCommandBuffer>
        m_vCommandBuffers;  // Each command buffer draw to one frame buffer
    std::vector<VkFramebuffer>
        m_vFramebuffers;  // Each framebuffer bind to a swapchain image
    VkExtent2D mRenderArea = {0, 0};
private:
    void destroyFramebuffers();
};
