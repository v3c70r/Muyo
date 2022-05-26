#include "RenderPassManager.h"

#include <cassert>

#include "DebugUI.h"
#include "RenderLayerIBL.h"
#include "RenderPass.h"
#include "RenderPassAO.h"
#include "RenderPassGBuffer.h"
#include "RenderPassSkybox.h"
#include "RenderPassTransparent.h"
#include "RenderPassUI.h"
#include "RenderResourceManager.h"
#include "RenderPassRayTracing.h"
#include "Scene.h"
#include "Swapchain.h"
#ifdef FEATURE_RAY_TRACING
#include "RayTracingSceneManager.h"
#endif

static RenderPassManager renderPassManager;

RenderPassManager *GetRenderPassManager()
{
    return &renderPassManager;
}

void RenderPassManager::CreateSwapchain(const VkSurfaceKHR &swapchainSurface)
{
    assert(m_pSwapchain == nullptr);
    m_pSwapchain = std::make_unique<Swapchain>();
    m_pSwapchain->CreateSwapchain(swapchainSurface,
                                  SWAPCHAIN_FORMAT,
                                  PRESENT_MODE,
                                  NUM_BUFFERS);
}

void RenderPassManager::BeginFrame()
{

    m_pCamera->SetFrameId(m_temporalInfo.nFrameId);
    m_temporalInfo.nFrameId++;
    m_temporalInfo.nFrameNoCameraMove++;

    UniformBuffer<PerViewData> *pUniformBuffer = GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");
    m_pCamera->UpdatePerViewDataUBO(pUniformBuffer);
    

    m_uImageIdx2Present = m_pSwapchain->GetNextImage(m_imageAvailable);

    // Wait for previous command renders to current swaphchain image to finish
    vkWaitForFences(GetRenderDevice()->GetDevice(), 1, &m_aGPUExecutionFence[m_uImageIdx2Present], VK_TRUE, std::numeric_limits<uint64_t>::max());
    vkResetFences(GetRenderDevice()->GetDevice(), 1, &m_aGPUExecutionFence[m_uImageIdx2Present]);
}

void RenderPassManager::Present()
{
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinished;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &(m_pSwapchain->GetSwapChain());
    presentInfo.pImageIndices = &m_uImageIdx2Present;

    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(GetRenderDevice()->GetPresentQueue(), &presentInfo);
}

void RenderPassManager::Initialize(uint32_t uWidth, uint32_t uHeight, const VkSurfaceKHR &swapchainSurface)
{
    m_uWidth = uWidth;
    m_uHeight = uHeight;

    CreateSwapchain(swapchainSurface);

    VkExtent2D vp = {uWidth, uHeight};
    // GBuffer pass
    {
        m_vpRenderPasses[RENDERPASS_GBUFFER] = std::make_unique<RenderPassGBuffer>();
        RenderPassGBuffer *pGBufferPass = static_cast<RenderPassGBuffer *>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
        pGBufferPass->createGBufferViews(vp);
        pGBufferPass->CreatePipeline();
    }
    // Final pass
    m_vpRenderPasses[RENDERPASS_FINAL] = std::make_unique<RenderPassFinal>(m_pSwapchain->GetImageFormat());
    // UI Pass
    m_vpRenderPasses[RENDERPASS_UI] = std::make_unique<RenderPassUI>(m_pSwapchain->GetImageFormat());
    RenderPassUI *pUIPass = static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get());
    pUIPass->RegisterDebugPage<DockSpace>("DockSpace");
    // Register debug ui pages
    pUIPass->RegisterDebugPage<ResourceManagerDebugPage>("Render Manager Resources");
    pUIPass->RegisterDebugPage<SceneDebugPage>("Loaded Scenes");
    pUIPass->RegisterDebugPage<EnvironmentMapDebugPage>("Env HDRs");
    pUIPass->RegisterDebugPage<LightsDebugPage>("Lights");
    CameraDebugPage *pCameraDebugPage = pUIPass->RegisterDebugPage<CameraDebugPage>("MainCamera");

    // pUIPass->RegisterDebugPage<DemoDebugPage>("demo");

    // IBL pass
    m_vpRenderPasses[RENDERPASS_IBL] = std::make_unique<RenderLayerIBL>();
    // Transparent pass
    m_vpRenderPasses[RENDERPASS_TRANSPARENT] = std::make_unique<RenderPassTransparent>();
    // Skybox pass
    m_vpRenderPasses[RENDERPASS_SKYBOX] = std::make_unique<RenderPassSkybox>();
    // AO pass
    m_vpRenderPasses[RENDERPASS_AO] = std::make_unique<RenderPassAO>();
#ifdef FEATURE_RAY_TRACING
    // RenderPass Ray Tracing
    m_vpRenderPasses[RENDERPASS_RAY_TRACING] = std::make_unique<RenderPassRayTracing>();
#else
    m_vpRenderPasses[RENDERPASS_RAY_TRACING] = nullptr;
#endif


    RenderTarget *pDepthResource = GetRenderResourceManager()->GetDepthTarget("depthTarget", VkExtent2D({m_uWidth, m_uHeight}));

    SetSwapchainImageViews(m_pSwapchain->GetImageViews(), pDepthResource->getView());

    // Create semaphores
    //
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    assert(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_depthReady) == VK_SUCCESS);
    assert(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_aoReady) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_depthReady), VK_OBJECT_TYPE_SEMAPHORE, "Depth Ready");
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_aoReady), VK_OBJECT_TYPE_SEMAPHORE, "AO Ready");

    assert(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailable) == VK_SUCCESS);
    assert(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_renderFinished) == VK_SUCCESS);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_imageAvailable), VK_OBJECT_TYPE_SEMAPHORE, "Swapchian ImageAvailable");
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_renderFinished), VK_OBJECT_TYPE_SEMAPHORE, "Render Finished");

    // Create a fence to wait for GPU execution for each swapchain image
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto &fence : m_aGPUExecutionFence)
    {
        assert(vkCreateFence(GetRenderDevice()->GetDevice(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(fence), VK_OBJECT_TYPE_FENCE, "renderFinished");
    }

    // Allocate an arcball camera
    // TODO: Allocate camera as needed if we ever support mulit render targets
    const float FAR = 100.0f;
    m_pCamera = std::make_unique<Arcball>(
        glm::perspective(glm::radians(80.0f),
                         (float)m_uWidth / (float)m_uHeight, 0.1f,
                         FAR),
        glm::lookAt(glm::vec3(0.0f, 0.0f, -2.0f),  // Eye
                    glm::vec3(0.0f, 0.0f, 0.0f),   // Center
                    glm::vec3(0.0f, 1.0f, 0.0f)),  // Up
        0.1f,                                      // near
        FAR,                                       // far
        (float)m_uWidth,
        (float)m_uHeight
    );
    pCameraDebugPage->SetCamera(GetCamera());
}

void RenderPassManager::SetSwapchainImageViews(const std::vector<VkImageView> &vImageViews, VkImageView depthImageView)
{
    assert(m_uWidth != 0 && m_uHeight != 0);
    static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get())->SetSwapchainImageViews(vImageViews, depthImageView, m_uWidth, m_uHeight);
    static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get())->CreatePipeline();

    static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get())->SetSwapchainImageViews(vImageViews, depthImageView, m_uWidth, m_uHeight);
    static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get())->CreateImGuiResources();
    static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get())->CreatePipeline();

    static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get())->CreateFramebuffer(m_uWidth, m_uHeight);
    static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get())->CreatePipeline();

    static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get())->CreateFramebuffer(m_uWidth, m_uHeight);
    static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get())->CreatePipeline();
}

void RenderPassManager::OnResize(uint32_t uWidth, uint32_t uHeight)
{
    if (m_uWidth != uWidth || m_uHeight != uHeight)
    {
        m_uWidth = uWidth;
        m_uHeight = uHeight;

        assert(m_uWidth != 0 && m_uHeight != 0);

        // recreate swapchain with the same surface
        VkSurfaceKHR surface = m_pSwapchain->GetSurface();
        m_pSwapchain->DestroySwapchain();
        m_pSwapchain->CreateSwapchain(surface,
                                      SWAPCHAIN_FORMAT,
                                      PRESENT_MODE,
                                      NUM_BUFFERS);

        // Reallocate depth target
        GetRenderResourceManager()->RemoveResource("depthTarget");
        RenderTarget *pDepthResource = GetRenderResourceManager()->GetDepthTarget("depthTarget", {uWidth, uHeight});

        // Recreate swap chian

        // Recreate dependent render passes
        static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get())->Resize(m_pSwapchain->GetImageViews(), pDepthResource->getView(), m_uWidth, m_uHeight);
        static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get())->Resize(m_pSwapchain->GetImageViews(), pDepthResource->getView(), m_uWidth, m_uHeight);

        // Update arcball tracking
        m_pCamera->Resize(glm::vec2((float)uWidth, (float)uHeight));
    }
}

void RenderPassManager::Unintialize()
{
    for (auto &pPass : m_vpRenderPasses)
    {
        pPass = nullptr;
    }
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_depthReady, nullptr);
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_aoReady, nullptr);
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_imageAvailable, nullptr);
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_renderFinished, nullptr);
    for (auto &fence : m_aGPUExecutionFence)
    {
        vkDestroyFence(GetRenderDevice()->GetDevice(), fence, nullptr);
    }
    m_pSwapchain->DestroySwapchain();
    m_pSwapchain = nullptr;
}

void RenderPassManager::RecordStaticCmdBuffers(const DrawLists &drawLists)
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

#ifdef FEATURE_RAY_TRACING
    if (m_vpRenderPasses[RENDERPASS_RAY_TRACING])
    {
        RenderPassRayTracing *pRTPass = static_cast<RenderPassRayTracing *>(m_vpRenderPasses[RENDERPASS_RAY_TRACING].get());
        pRTPass->RecordCommandBuffer(
                VkExtent2D({m_uWidth, m_uHeight}),
                m_pRayTracingSceneManager->GetRayGenRegion(),
                m_pRayTracingSceneManager->GetRayMissRegion(),
                m_pRayTracingSceneManager->GetRayHitRegion(),
                m_pRayTracingSceneManager->GetPipelineLayout(),
                m_pRayTracingSceneManager->GetPipeline()
                );
    }
#endif
    RenderPassSkybox *pSkybox = static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get());
    pSkybox->RecordCommandBuffers();
    RenderPassFinal *pFinalPass = static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get());
    pFinalPass->RecordCommandBuffers();
    RenderPassAO *pAOPass = static_cast<RenderPassAO *>(m_vpRenderPasses[RENDERPASS_AO].get());
    pAOPass->RecordCommandBuffer();


}

void RenderPassManager::RecordDynamicCmdBuffers()
{

    VkExtent2D vpExtent = {m_uWidth, m_uHeight};
    RenderPassUI *pUIPass = static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get());
    pUIPass->newFrame(vpExtent);
    pUIPass->updateBuffers(m_uImageIdx2Present);
    pUIPass->RecordCommandBuffer(vpExtent, m_uImageIdx2Present);
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

void RenderPassManager::SubmitCommandBuffers()
{
    // This function manages command buffer submissions and queue synchronizations
    std::vector<VkCommandBuffer> vCmdBufs;
    std::vector<VkSemaphore> vWaitForSemaphores;
    std::vector<VkPipelineStageFlags> vWaitStages;
    std::vector<VkSemaphore> vSignalSemaphores;

    if (!m_bIsIrradianceGenerated)
    {
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_IBL]->GetCommandBuffer(m_uImageIdx2Present));
        m_bIsIrradianceGenerated = true;
    }

    // Submit graphics queue to signal depth ready semaphore
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_GBUFFER]->GetCommandBuffer(m_uImageIdx2Present));
    vSignalSemaphores.push_back(m_depthReady);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetGraphicsQueue(), vWaitForSemaphores, vSignalSemaphores, vWaitStages);

    vCmdBufs.clear();
    vSignalSemaphores.clear();

    // Submit other graphics tasks
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_SKYBOX]->GetCommandBuffer(m_uImageIdx2Present));
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_TRANSPARENT]->GetCommandBuffer(m_uImageIdx2Present));
#ifdef FEATURE_RAY_TRACING
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_RAY_TRACING]->GetCommandBuffer(m_uImageIdx2Present));
#endif
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetGraphicsQueue(), vWaitForSemaphores, vSignalSemaphores, vWaitStages);

    vCmdBufs.clear();
    // Submit compute tasks
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_AO]->GetCommandBuffer(m_uImageIdx2Present));
    vWaitForSemaphores.push_back(m_depthReady);
    vSignalSemaphores.push_back(m_aoReady);
    vWaitStages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetComputeQueue(), vWaitForSemaphores, vSignalSemaphores, vWaitStages);

    vCmdBufs.clear();
    vWaitForSemaphores.clear();
    vSignalSemaphores.clear();
    vWaitStages.clear();
    // Submit passes to swapchain
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_FINAL]->GetCommandBuffer(m_uImageIdx2Present));

    VkCommandBuffer uiCmdBuffer = m_vpRenderPasses[RENDERPASS_UI]->GetCommandBuffer(m_uImageIdx2Present);
    if (uiCmdBuffer != VK_NULL_HANDLE)  // it's possible there's no UI to draw
    {
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_UI]->GetCommandBuffer(m_uImageIdx2Present));
    }
    vWaitForSemaphores.push_back(m_imageAvailable);
    vWaitForSemaphores.push_back(m_aoReady);
    vWaitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vWaitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vSignalSemaphores.push_back(m_renderFinished);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetComputeQueue(), vWaitForSemaphores, vSignalSemaphores, vWaitStages, m_aGPUExecutionFence[m_uImageIdx2Present]);
}
