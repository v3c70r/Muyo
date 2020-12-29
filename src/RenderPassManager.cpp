#include <cassert>
#include "RenderPassManager.h"
#include "RenderResourceManager.h"
#include "RenderPass.h"
#include "RenderPassGBuffer.h"
#include "RenderPassUI.h"
#include "RenderLayerIBL.h"

static RenderPassManager renderPassManager;

RenderPassManager* GetRenderPassManager()
{
    return &renderPassManager;
}

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

void RenderPassManager::Unintialize()
{
    for (auto &pPass : m_vpRenderPasses)
    {
        pPass = nullptr;
    }
}

void RenderPassManager::RecordCmdBuffers(const std::vector<const Geometry*>& vpGeometries)
{
    RenderPassGBuffer *pGBufferPass = static_cast<RenderPassGBuffer *>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
    pGBufferPass->recordCommandBuffer(vpGeometries);

    RenderPassFinal *pFinalPass = static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get());
    Geometry *pQuadGeometry = GetQuad();
    //pFinalPass->RecordOnce(*pQuadGeometry,
    //                       GetRenderResourceManager()
    //                           ->getColorTarget("LIGHTING_OUTPUT", VkExtent2D({0, 0}))
    //                           ->getView());
}

std::vector<VkCommandBuffer> RenderPassManager::GetCommandBuffers(uint32_t uImgIdx)
{
    RenderPassGBuffer *pGBufferPass = static_cast<RenderPassGBuffer*>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
    std::vector<VkCommandBuffer> vCmdBufs;
    for (const auto& pass : m_vpRenderPasses)
    {
        vCmdBufs.push_back(pass->GetCommandBuffer());
    }
    if (m_bIsIrradianceGenerated)
    {
        //vCmdBufs = {// TODO: Disable IBL submitting for every frame
        //              // pIBLPass->GetCommandBuffer(),
        //              pGBufferPass->GetCommandBuffer(),
        //              pFinalPass->GetCommandBuffer(uImgIdx),
        //              pUIPass->GetCommandBuffer(uImgIdx)};
    }
    return vCmdBufs;
}
