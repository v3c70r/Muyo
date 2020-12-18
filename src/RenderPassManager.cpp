#include <cassert>
#include "RenderPassManager.h"
#include "RenderPassGBuffer.h"
#include "RenderPass.h"
#include "RenderPassUI.h"
#include "RenderLayerIBL.h"

void RenderPassManager::Initialize(uint32_t uWidth, uint32_t uHeight, VkFormat swapchainFormat)
{
    m_uWidth = uWidth;
    m_uHeight = uHeight;
    VkExtent2D vp = {uWidth, uHeight};
    // GBuffer pass
    {
        m_vpRenderPasses[RENDERPASS_GBUFFER] = std::make_unique<RenderPassGBuffer>();
        RenderPassGBuffer *pGBufferPass = static_cast<RenderPassGBuffer *>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
        pGBufferPass->createGBufferViews(vp);
        pGBufferPass->createPipelines();
    }
    // Final pass
    m_vpRenderPasses[RENDERPASS_FINAL] = std::make_unique<RenderPassFinal>(swapchainFormat);
    // UI Pass
    m_vpRenderPasses[RENDERPASS_UI] = std::make_unique<RenderPassUI>(swapchainFormat);
    // IBL pass
    m_vpRenderPasses[RENDERPASS_IBL] = std::make_unique<RenderLayerIBL>();
}

void RenderPassManager::SetSwapchainImageViews(std::vector<VkImageView> &vImageViews, VkImageView depthImageView)
{
    assert(m_uWidth != 0 && m_uHeight != 0);
    static_cast<RenderPassFinal*>(m_vpRenderPasses[RENDERPASS_FINAL].get())->setSwapchainImageViews(vImageViews, depthImageView, m_uWidth, m_uHeight);
    static_cast<RenderPassUI*>(m_vpRenderPasses[RENDERPASS_UI].get())->setSwapchainImageViews(vImageViews, depthImageView, m_uWidth, m_uHeight);
}
void RenderPassManager::OnResize(uint32_t uWidth, uint32_t uHeight)
{
}
