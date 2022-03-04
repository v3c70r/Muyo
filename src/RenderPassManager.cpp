#include <cassert>
#include "Scene.h"
#include "RenderPassManager.h"
#include "RenderResourceManager.h"
#include "RenderPass.h"
#include "RenderPassGBuffer.h"
#include "RenderPassUI.h"
#include "RenderLayerIBL.h"
#include "RenderPassTransparent.h"
#include "RenderPassSkybox.h"
#include "DebugUI.h"

static RenderPassManager renderPassManager;

RenderPassManager* GetRenderPassManager()
{
    return &renderPassManager;
}

void RenderPassManager::Initialize(uint32_t uWidth, uint32_t uHeight)
{
    m_uWidth = uWidth;
    m_uHeight = uHeight;
    const Swapchain* pSwapchian = GetRenderDevice()->GetSwapchain();

    VkExtent2D vp = {uWidth, uHeight};
    // GBuffer pass
    {
        m_vpRenderPasses[RENDERPASS_GBUFFER] = std::make_unique<RenderPassGBuffer>();
        RenderPassGBuffer *pGBufferPass = static_cast<RenderPassGBuffer *>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
        pGBufferPass->createGBufferViews(vp);
        pGBufferPass->CreatePipeline();
    }
    // Final pass
    m_vpRenderPasses[RENDERPASS_FINAL] = std::make_unique<RenderPassFinal>(pSwapchian->GetImageFormat());
    // UI Pass
    {
        m_vpRenderPasses[RENDERPASS_UI] = std::make_unique<RenderPassUI>(pSwapchian->GetImageFormat());
        RenderPassUI *pUIPass = static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get());
        pUIPass->RegisterDebugPage<DockSpace>("DockSpace");
        // Register debug ui pages
        pUIPass->RegisterDebugPage<ResourceManagerDebugPage>("Render Manager Resources");
        pUIPass->RegisterDebugPage<SceneDebugPage>("Loaded Scenes");
        pUIPass->RegisterDebugPage<EnvironmentMapDebugPage>("Env HDRs");
        //pUIPass->RegisterDebugPage<DemoDebugPage>("demo");
    }

    // IBL pass
    m_vpRenderPasses[RENDERPASS_IBL] = std::make_unique<RenderLayerIBL>();
    // Transparent pass
    m_vpRenderPasses[RENDERPASS_TRANSPARENT] = std::make_unique<RenderPassTransparent>();
    // Skybox pass
    m_vpRenderPasses[RENDERPASS_SKYBOX] = std::make_unique<RenderPassSkybox>();
}

void RenderPassManager::SetSwapchainImageViews(const std::vector<VkImageView> &vImageViews, VkImageView depthImageView)
{
    assert(m_uWidth != 0 && m_uHeight != 0);
    static_cast<RenderPassFinal*>(m_vpRenderPasses[RENDERPASS_FINAL].get())->SetSwapchainImageViews(vImageViews, depthImageView, m_uWidth, m_uHeight);
    static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get())->CreatePipeline();

    static_cast<RenderPassUI*>(m_vpRenderPasses[RENDERPASS_UI].get())->SetSwapchainImageViews(vImageViews, depthImageView, m_uWidth, m_uHeight);
    static_cast<RenderPassUI*>(m_vpRenderPasses[RENDERPASS_UI].get())->CreateImGuiResources();
    static_cast<RenderPassUI*>(m_vpRenderPasses[RENDERPASS_UI].get())->CreatePipeline();

    static_cast<RenderPassTransparent*>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get())->CreateFramebuffer(m_uWidth, m_uHeight);
    static_cast<RenderPassTransparent*>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get())->CreatePipeline();

    static_cast<RenderPassSkybox*>(m_vpRenderPasses[RENDERPASS_SKYBOX].get())->CreateFramebuffer(m_uWidth, m_uHeight);
    static_cast<RenderPassSkybox*>(m_vpRenderPasses[RENDERPASS_SKYBOX].get())->CreatePipeline();
}

void RenderPassManager::OnResize(uint32_t uWidth, uint32_t uHeight)
{
    if (m_uWidth != uWidth || m_uHeight != uHeight)
    {
        m_uWidth = uWidth;
        m_uHeight = uHeight;

        assert(m_uWidth != 0 && m_uHeight != 0);
        GetRenderDevice()->SetViewportSize({uWidth, uHeight});

        // Reallocate depth target
        GetRenderResourceManager()->RemoveResource("depthTarget");
        RenderTarget *pDepthResource = GetRenderResourceManager()->GetDepthTarget("depthTarget", {uWidth, uHeight});

        // Recreate swap chian
        const Swapchain *pSwapchain = GetRenderDevice()->GetSwapchain();

        // Recreate dependent render passes
        static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get())->Resize(pSwapchain->GetImageViews(), pDepthResource->getView(), m_uWidth, m_uHeight);
        static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get())->Resize(pSwapchain->GetImageViews(), pDepthResource->getView(), m_uWidth, m_uHeight);

        // No need to update back buffers
        //static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get())->DestroyFramebuffer();
        //static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get())->CreateFramebuffer(m_uWidth, m_uHeight);
        //static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get())->CreatePipeline();

        //static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get())->DestroyFramebuffer();
        //static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get())->CreateFramebuffer(m_uWidth, m_uHeight);
        //static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get())->CreatePipeline();
    }
}

void RenderPassManager::Unintialize()
{
    for (auto &pPass : m_vpRenderPasses)
    {
        pPass = nullptr;
    }
}

void RenderPassManager::RecordStaticCmdBuffers(const DrawLists& drawLists)
{
    {
        RenderPassGBuffer *pGBufferPass = static_cast<RenderPassGBuffer *>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
        const std::vector<const SceneNode *> &opaqueDrawList = drawLists.m_aDrawLists[DrawLists::DL_OPAQUE];
        std::vector<const Geometry *> vpGeometries;
        vpGeometries.reserve(opaqueDrawList.size());
        for (const SceneNode *pNode : opaqueDrawList)
        {
            vpGeometries.push_back(
                static_cast<const GeometrySceneNode *>(pNode)->GetGeometry());
        }
        pGBufferPass->RecordCommandBuffer(vpGeometries);
    }
    {
        RenderPassTransparent *pTransparentPass = static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get());
        const std::vector<const SceneNode *> &transparentDrawList = drawLists.m_aDrawLists[DrawLists::DL_TRANSPARENT];
        std::vector<const Geometry *> vpGeometries;
        vpGeometries.reserve(transparentDrawList.size());
        for (const SceneNode *pNode : transparentDrawList)
        {
            vpGeometries.push_back(
                static_cast<const GeometrySceneNode *>(pNode)->GetGeometry());
        }
        pTransparentPass->RecordCommandBuffers(vpGeometries);
    }

    RenderPassSkybox *pSkybox = static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get());
    pSkybox->RecordCommandBuffers();
    RenderPassFinal *pFinalPass = static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get());
    pFinalPass->RecordCommandBuffers();
}

void RenderPassManager::RecordDynamicCmdBuffers(uint32_t nFrameIdx, VkExtent2D vpExtent)
{
    RenderPassUI* pUIPass = static_cast<RenderPassUI*>(m_vpRenderPasses[RENDERPASS_UI].get());
    pUIPass->newFrame(vpExtent);
    pUIPass->updateBuffers(nFrameIdx);
    pUIPass->RecordCommandBuffer(vpExtent, nFrameIdx);
}

std::vector<VkCommandBuffer> RenderPassManager::GetCommandBuffers(uint32_t uImgIdx)
{
    std::vector<VkCommandBuffer> vCmdBufs;
    for (size_t i = 0; i < RENDERPASS_COUNT; i++)
    {
        if (m_bIsIrradianceGenerated && i == RENDERPASS_IBL)
        {
            continue;
        }
        auto &pass = m_vpRenderPasses[i];
        VkCommandBuffer cmdBuf = pass->GetCommandBuffer(uImgIdx);
        if (cmdBuf != VK_NULL_HANDLE)
        {
            vCmdBufs.push_back(cmdBuf);
        }
        m_bIsIrradianceGenerated = true;
    }
    return vCmdBufs;
}

void RenderPassManager::ReloadEnvironmentMap(const std::string &sNewEnvMapPath)
{
    RenderLayerIBL *pIBLPass = static_cast<RenderLayerIBL *>(m_vpRenderPasses[RENDERPASS_IBL].get());
    pIBLPass->ReloadEnvironmentMap(sNewEnvMapPath);

    m_bIsIrradianceGenerated = false;
}
