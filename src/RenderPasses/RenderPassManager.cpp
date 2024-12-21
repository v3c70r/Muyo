#include "RenderPassManager.h"

#include <cassert>
#include <memory>

#include "DebugUI.h"
#include "RenderLayerIBL.h"
#include "RenderPass.h"
#include "RenderPassCubeMapGeneration.h"
#include "RenderPassGBuffer.h"
#include "RenderPassGaussianSplats.h"
#include "RenderPassOpaqueLighting.h"
#include "RenderPassRSM.h"
#include "RenderPassSkybox.h"
#include "RenderPassTransparent.h"
#include "RenderPassUI.h"
#include "RenderResourceManager.h"
#include "RenderPassGBufferMeshShader.h"
#include "Scene.h"
#ifdef FEATURE_RAY_TRACING
#include "RayTracingSceneManager.h"
#include "RenderPassRayTracing.h"
#endif
namespace Muyo
{

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
    if (m_pCamera->IsTransforationUpdated())
    {
        // Hack: Use frame id to track number of frame without transformation
        m_temporalInfo.nFrameId = 1;
    }
    m_pCamera->SetFrameId(m_temporalInfo.nFrameId);
    m_temporalInfo.nFrameId++;
    m_temporalInfo.nFrameNoCameraMove++;

    UniformBuffer<PerViewData> *pUniformBuffer = GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");
    m_pCamera->UpdatePerViewDataUBO(pUniformBuffer);

    m_uImageIdx2Present = m_pSwapchain->GetNextImage(m_imageAvailable);

    static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get())->SetCurrentSwapchainImageIndex(m_uImageIdx2Present);

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

    m_pShadowPassManager = std::make_unique<ShadowPassManager>();
    VkExtent2D vp = {uWidth, uHeight};
    // GBuffer and opaque lighting
    {
        m_vpRenderPasses[RENDERPASS_GBUFFER] = std::make_unique<RenderPassGBuffer>(vp);
        m_vpRenderPasses[RENDERPASS_OPAQUE_LIGHTING] = std::make_unique<RenderPassOpaqueLighting>(vp, *m_pShadowPassManager);
    }
    // Final pass
    m_vpRenderPasses[RENDERPASS_FINAL] = std::make_unique<RenderPassFinal>(*m_pSwapchain, true);
    // UI Pass
    m_vpRenderPasses[RENDERPASS_UI] = std::make_unique<RenderPassUI>(vp);
    RenderPassUI *pUIPass = static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get());
    pUIPass->PrepareRenderPass();
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

    m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION] = std::make_unique<RenderPassCubeMapGeneration>(VkExtent2D({128, 128}));

    m_vpRenderPasses[RENDERPASS_MESH_SHADER] = std::make_unique<RenderPassGBufferMeshShader>(VkExtent2D({m_uWidth, m_uHeight}));

    m_vpRenderPasses[RENDERPASS_GAUSSIAN_SPLATS] = std::make_unique<RenderPassGaussianSplats>(vp);

    // IBL pass separated
    // Transparent pass
    m_vpRenderPasses[RENDERPASS_TRANSPARENT] = std::make_unique<RenderPassTransparent>(vp);
    // Skybox pass
    m_vpRenderPasses[RENDERPASS_SKYBOX] = std::make_unique<RenderPassSkybox>(vp);
#ifdef FEATURE_RAY_TRACING
    // RenderPass Ray Tracing
    m_vpRenderPasses[RENDERPASS_RAY_TRACING] = std::make_unique<RenderPassRayTracing>(vp);
#else
    m_vpRenderPasses[RENDERPASS_RAY_TRACING] = nullptr;
#endif

    RenderTarget *pDepthResource = GetRenderResourceManager()->GetDepthTarget("depthTarget", VkExtent2D({m_uWidth, m_uHeight}));

    // Create semaphores

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_ASSERT(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_depthReady));

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_depthReady), VK_OBJECT_TYPE_SEMAPHORE, "Depth Ready");

    VK_ASSERT(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailable));
    VK_ASSERT(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_renderFinished));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_imageAvailable), VK_OBJECT_TYPE_SEMAPHORE, "Swapchian ImageAvailable");
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_renderFinished), VK_OBJECT_TYPE_SEMAPHORE, "Render Finished");

    // Create a fence to wait for GPU execution for each swapchain image
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto &fence : m_aGPUExecutionFence)
    {
        VK_ASSERT(vkCreateFence(GetRenderDevice()->GetDevice(), &fenceInfo, nullptr, &fence));
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
        (float)m_uHeight);
    pCameraDebugPage->SetCamera(GetCamera());
}

void RenderPassManager::OnResize(uint32_t uWidth, uint32_t uHeight)
{
    // TODO(qgu): Implement resize function
    //if (m_uWidth != uWidth || m_uHeight != uHeight)
    //{
    //    m_uWidth = uWidth;
    //    m_uHeight = uHeight;

    //    assert(m_uWidth != 0 && m_uHeight != 0);

    //    // recreate swapchain with the same surface
    //    VkSurfaceKHR surface = m_pSwapchain->GetSurface();
    //    m_pSwapchain->DestroySwapchain();
    //    m_pSwapchain->CreateSwapchain(surface,
    //                                  SWAPCHAIN_FORMAT,
    //                                  PRESENT_MODE,
    //                                  NUM_BUFFERS);

    //    // Reallocate depth target
    //    GetRenderResourceManager()->RemoveResource("depthTarget");
    //    RenderTarget *pDepthResource = GetRenderResourceManager()->GetDepthTarget("depthTarget", {uWidth, uHeight});

    //    // Recreate dependent render passes
    //    static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get())->Resize(m_pSwapchain->GetImageViews(), pDepthResource->getView(), m_uWidth, m_uHeight);

    //    m_vpRenderPasses[RENDERPASS_SKYBOX] = std::make_unique<RenderPassSkybox>(VkExtent2D({m_uWidth, m_uHeight}));

    //    // Update arcball tracking
    //    m_pCamera->Resize(glm::vec2((float)uWidth, (float)uHeight));
    //}
}

void RenderPassManager::Unintialize()
{
    m_pShadowPassManager = nullptr;
    for (auto &pPass : m_vpRenderPasses)
    {
        pPass = nullptr;
    }
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_depthReady, nullptr);
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
        RenderPassCubeMapGeneration* pCubeMapGenerationPass = static_cast<RenderPassCubeMapGeneration*>(m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION].get());
        pCubeMapGenerationPass->PrepareRenderPass();
        pCubeMapGenerationPass->RecordCommandBuffers();
    }

    {
        RenderPassGBufferMeshShader* pMeshShaderPass = static_cast<RenderPassGBufferMeshShader*>(m_vpRenderPasses[RENDERPASS_MESH_SHADER].get());
        pMeshShaderPass->PrepareRenderPass();
        pMeshShaderPass->RecordCommandBuffers();
    }

    {
        RenderPassGaussianSplats* pGaussianSplatsPass = static_cast<RenderPassGaussianSplats*>(m_vpRenderPasses[RENDERPASS_GAUSSIAN_SPLATS].get());
        pGaussianSplatsPass->SetGaussainSplatsSceneNode(static_cast<const GaussianSplatsSceneNode*>(drawLists.m_aDrawLists[DrawLists::DL_GS][0]));
        pGaussianSplatsPass->PrepareRenderPass();
        pGaussianSplatsPass->RecordCommandBuffers();
    }
    // Prepare shadow pass
    {
        m_pShadowPassManager->SetLights(drawLists.m_aDrawLists[DrawLists::DL_LIGHT]);
        m_pShadowPassManager->PrepareRenderPasses();
        const std::vector<const SceneNode *> &opaqueDrawList = drawLists.m_aDrawLists[DrawLists::DL_OPAQUE];
        m_pShadowPassManager->RecordCommandBuffers(opaqueDrawList);
    }
    {
        RenderPassGBuffer *pGBufferPass = static_cast<RenderPassGBuffer*>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
        pGBufferPass->PrepareRenderPass();
        const std::vector<const SceneNode *> &opaqueDrawList = drawLists.m_aDrawLists[DrawLists::DL_OPAQUE];
        pGBufferPass->RecordCommandBuffers(opaqueDrawList);
    }
    {
        RenderPassOpaqueLighting *pOpaqueLightingPass = static_cast<RenderPassOpaqueLighting *>(m_vpRenderPasses[RENDERPASS_OPAQUE_LIGHTING].get());
        pOpaqueLightingPass->PrepareRenderPass();
        pOpaqueLightingPass->RecordCommandBuffers();
    }
    {
        RenderPassTransparent *pTransparentPass = static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get());
        const std::vector<const SceneNode *> &transparentDrawList = drawLists.m_aDrawLists[DrawLists::DL_TRANSPARENT];
        pTransparentPass->PrepareRenderPass();
        pTransparentPass->RecordCommandBuffers(transparentDrawList);
    }

#ifdef FEATURE_RAY_TRACING
    if (m_vpRenderPasses[RENDERPASS_RAY_TRACING])
    {
        RenderPassRayTracing *pRTPass = static_cast<RenderPassRayTracing *>(m_vpRenderPasses[RENDERPASS_RAY_TRACING].get());
        pRTPass->PrepareRenderPass();
        pRTPass->RecordCommandBuffer();
    }
#endif
    RenderPassSkybox *pSkybox = static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get());
    pSkybox->PrepareRenderPass();
    pSkybox->RecordCommandBuffers();

    RenderPassFinal *pFinalPass = static_cast<RenderPassFinal *>(m_vpRenderPasses[RENDERPASS_FINAL].get());
    pFinalPass->PrepareRenderPass();
    pFinalPass->RecordCommandBuffers();
}

void RenderPassManager::RecordDynamicCmdBuffers()
{
    VkExtent2D vpExtent = {m_uWidth, m_uHeight};
    RenderPassUI *pUIPass = static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get());
    pUIPass->NewFrame(vpExtent);
    pUIPass->UpdateBuffers();
    pUIPass->RecordCommandBuffer();
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
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_IBL]->GetCommandBuffer());
        m_bIsIrradianceGenerated = true;
    }

    // Debug cubemap generation
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION]->GetCommandBuffer());
   
    // Mesh shader
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_MESH_SHADER]->GetCommandBuffer());

    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_GAUSSIAN_SPLATS]->GetCommandBuffer());

    // Shadow pass
    // vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_SHADOW]->GetCommandBuffer());
    auto vShadowPassCmds = m_pShadowPassManager->GetCommandBuffers();
    vCmdBufs.insert(std::end(vCmdBufs), std::begin(vShadowPassCmds), std::end(vShadowPassCmds));

    // Submit graphics queue to signal depth ready semaphore
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_GBUFFER]->GetCommandBuffer());
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_OPAQUE_LIGHTING]->GetCommandBuffer());
    vSignalSemaphores.push_back(m_depthReady);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetGraphicsQueue(), vWaitForSemaphores, vSignalSemaphores, vWaitStages);

    vCmdBufs.clear();
    vSignalSemaphores.clear();

    // Submit other graphics tasks
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_SKYBOX]->GetCommandBuffer());
    if (auto cmdBuf = m_vpRenderPasses[RENDERPASS_TRANSPARENT]->GetCommandBuffer())
    {
        vCmdBufs.push_back(cmdBuf);
    }

    vCmdBufs.clear();
    // Submit compute tasks
    vWaitForSemaphores.push_back(m_depthReady);
    vWaitStages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetComputeQueue(), vWaitForSemaphores, vSignalSemaphores, vWaitStages);

    vCmdBufs.clear();
    vWaitForSemaphores.clear();
    vSignalSemaphores.clear();
    vWaitStages.clear();

    // Submit UI pass
    VkCommandBuffer uiCmdBuffer = m_vpRenderPasses[RENDERPASS_UI]->GetCommandBuffer();
    if (uiCmdBuffer != VK_NULL_HANDLE)    // it's possible there's no UI to draw
    {
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_UI]->GetCommandBuffer());
    }
    // Submit passes to swapchain
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_FINAL]->GetCommandBuffer());

    
    vWaitForSemaphores.push_back(m_imageAvailable);
    vWaitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vWaitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vSignalSemaphores.push_back(m_renderFinished);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetComputeQueue(), vWaitForSemaphores, vSignalSemaphores, vWaitStages, m_aGPUExecutionFence[m_uImageIdx2Present]);
}

}  // namespace Muyo
