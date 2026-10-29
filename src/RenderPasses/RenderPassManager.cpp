#include "RenderPassManager.h"

#include <algorithm>
#include <cassert>
#include <memory>

#include "DebugUI.h"
#include "RenderLayerIBL.h"
#include "RenderPass.h"
#include "RenderPassCubeMapGeneration.h"
#include "RenderPassGBuffer.h"
#include "RenderPassGBufferMeshShader.h"
#include "RenderPassOpaqueLighting.h"
#include "RenderPassRSM.h"
#include "RenderPassSkybox.h"
#include "RenderPassTransparent.h"
#include "RenderPassUI.h"
#include "RenderResourceManager.h"
#include "Scene.h"
#ifdef FEATURE_RAY_TRACING
#include "RayTracingSceneManager.h"
#include "RenderPassRayTracing.h"
#endif
namespace Muyo
{

static RenderPassManager renderPassManager;

RenderPassManager *GetRenderPassManager() { return &renderPassManager; }

void RenderPassManager::UpdatePerViewData()
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
}

void RenderPassManager::Initialize(uint32_t uWidth, uint32_t uHeight)
{
    m_uWidth = uWidth;
    m_uHeight = uHeight;

    m_pShadowPassManager = std::make_unique<ShadowPassManager>();
    VkExtent2D vp = {uWidth, uHeight};
    // GBuffer and opaque lighting
    {
        m_vpRenderPasses[RENDERPASS_GBUFFER] = std::make_unique<RenderPassGBuffer>(vp);
        m_vpRenderPasses[RENDERPASS_OPAQUE_LIGHTING] =
            std::make_unique<RenderPassOpaqueLighting>(vp, *m_pShadowPassManager);
    }
    // UI Pass
    m_vpRenderPasses[RENDERPASS_UI] = std::make_unique<RenderPassUI>(vp);
    auto *pUIPass = static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get());
    pUIPass->PrepareRenderPass();
    pUIPass->RegisterDebugPage<DockSpace>("DockSpace");
    // Register debug ui pages
    pUIPass->RegisterDebugPage<ResourceManagerDebugPage>("Render Manager Resources");
    pUIPass->RegisterDebugPage<SceneDebugPage>("Loaded Scenes");
    pUIPass->RegisterDebugPage<EnvironmentMapDebugPage>("Env HDRs");
    pUIPass->RegisterDebugPage<LightsDebugPage>("Lights");
    pUIPass->RegisterDebugPage<CameraDebugPage>("MainCamera");

    // pUIPass->RegisterDebugPage<DemoDebugPage>("demo");

    // IBL pass
    m_vpRenderPasses[RENDERPASS_IBL] = std::make_unique<RenderLayerIBL>();

    m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION] =
        std::make_unique<RenderPassCubeMapGeneration>(VkExtent2D({128, 128}));

    m_vpRenderPasses[RENDERPASS_MESH_SHADER] =
        std::make_unique<RenderPassGBufferMeshShader>(VkExtent2D({m_uWidth, m_uHeight}));

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

    GetRenderResourceManager()->GetDepthTarget("depthTarget", VkExtent2D({m_uWidth, m_uHeight}));

    // Create semaphores
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_ASSERT(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_depthReady));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_depthReady), VK_OBJECT_TYPE_SEMAPHORE, "Depth Ready");
    // The bridge the graph's FINAL node waits on (#20); removed with the last legacy scene pass.
    VK_ASSERT(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr, &m_sceneFinished));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_sceneFinished), VK_OBJECT_TYPE_SEMAPHORE, "Scene Finished");

    // Allocate an arcball camera
    // TODO(P3): allocate camera as needed if we ever support multiple render targets - deleted with
    // src/RenderPasses/.
    const float far = 100.0F;
    m_pCamera = std::make_unique<Arcball>(
        glm::perspective(glm::radians(80.0f), static_cast<float>(m_uWidth) / static_cast<float>(m_uHeight), 0.1F, far),
        glm::lookAt(glm::vec3(0.0f, 0.0f, -2.0f),  // Eye
                    glm::vec3(0.0f, 0.0f, 0.0f),   // Center
                    glm::vec3(0.0f, 1.0f, 0.0f)),  // Up
        0.1f,                                      // near
        far,                                       // far
        static_cast<float>(m_uWidth), static_cast<float>(m_uHeight));
}

void RenderPassManager::Unintialize()
{
    m_pShadowPassManager = nullptr;
    for (auto &pPass : m_vpRenderPasses)
    {
        pPass = nullptr;
    }
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_depthReady, nullptr);
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_sceneFinished, nullptr);
}

void RenderPassManager::RecordStaticCmdBuffers(const DrawLists &drawLists)
{
    {
        auto *pCubeMapGenerationPass =
            static_cast<RenderPassCubeMapGeneration *>(m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION].get());
        pCubeMapGenerationPass->PrepareRenderPass();
        pCubeMapGenerationPass->RecordCommandBuffers();
    }

    {
        auto *pMeshShaderPass =
            static_cast<RenderPassGBufferMeshShader *>(m_vpRenderPasses[RENDERPASS_MESH_SHADER].get());
        pMeshShaderPass->PrepareRenderPass();
        pMeshShaderPass->RecordCommandBuffers();
    }
    // Prepare shadow pass
    {
        m_pShadowPassManager->SetLights(drawLists.m_aDrawLists[DrawLists::DL_LIGHT]);
        m_pShadowPassManager->PrepareRenderPasses();
        const std::vector<const SceneNode *> &opaqueDrawList = drawLists.m_aDrawLists[DrawLists::DL_OPAQUE];
        m_pShadowPassManager->RecordCommandBuffers(opaqueDrawList);
    }
    {
        auto *pGBufferPass = static_cast<RenderPassGBuffer *>(m_vpRenderPasses[RENDERPASS_GBUFFER].get());
        pGBufferPass->PrepareRenderPass();
        const std::vector<const SceneNode *> &opaqueDrawList = drawLists.m_aDrawLists[DrawLists::DL_OPAQUE];
        pGBufferPass->RecordCommandBuffers(opaqueDrawList);
    }
    {
        auto *pOpaqueLightingPass =
            static_cast<RenderPassOpaqueLighting *>(m_vpRenderPasses[RENDERPASS_OPAQUE_LIGHTING].get());
        pOpaqueLightingPass->PrepareRenderPass();
        pOpaqueLightingPass->RecordCommandBuffers();
    }
    {
        auto *pTransparentPass = static_cast<RenderPassTransparent *>(m_vpRenderPasses[RENDERPASS_TRANSPARENT].get());
        const std::vector<const SceneNode *> &transparentDrawList = drawLists.m_aDrawLists[DrawLists::DL_TRANSPARENT];
        pTransparentPass->PrepareRenderPass();
        pTransparentPass->RecordCommandBuffers(transparentDrawList);
    }

#ifdef FEATURE_RAY_TRACING
    if (m_vpRenderPasses[RENDERPASS_RAY_TRACING])
    {
        RenderPassRayTracing *pRTPass =
            static_cast<RenderPassRayTracing *>(m_vpRenderPasses[RENDERPASS_RAY_TRACING].get());
        pRTPass->PrepareRenderPass();
        pRTPass->RecordCommandBuffer();
    }
#endif
    auto *pSkybox = static_cast<RenderPassSkybox *>(m_vpRenderPasses[RENDERPASS_SKYBOX].get());
    pSkybox->PrepareRenderPass();
    pSkybox->RecordCommandBuffers();
}

void RenderPassManager::RecordDynamicCmdBuffers()
{
    VkExtent2D vpExtent = {m_uWidth, m_uHeight};
    auto *pUIPass = static_cast<RenderPassUI *>(m_vpRenderPasses[RENDERPASS_UI].get());
    pUIPass->NewFrame(vpExtent);
    pUIPass->UpdateBuffers();
    pUIPass->RecordCommandBuffer();
}

void RenderPassManager::ReloadEnvironmentMap(const std::string &sNewEnvMapPath)
{
    auto *pIBLPass = static_cast<RenderLayerIBL *>(m_vpRenderPasses[RENDERPASS_IBL].get());
    pIBLPass->ReloadEnvironmentMap(sNewEnvMapPath);

    m_bIsIrradianceGenerated = false;
}

void RenderPassManager::SubmitSceneCommandBuffers(VkSemaphore imageAvailable)
{
    // This function manages command buffer submissions and queue synchronizations. It no longer owns
    // acquire or present: FrameSync does, and the graph's FINAL node consumes the composite this batch
    // produces. The two batches and their semaphores are the named bridge on #20.
    std::vector<VkCommandBuffer> vCmdBufs;
    std::vector<VkSemaphore> vWaitForSemaphores;
    std::vector<VkPipelineStageFlags2> vWaitStages;
    std::vector<VkSemaphore> vSignalSemaphores;

    if (!m_bIsIrradianceGenerated)
    {
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_IBL]->GetCommandBuffer());
        m_bIsIrradianceGenerated = true;
    }

    // Debug cubemap generation
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION]->GetCommandBuffer());

    // Mesh shader
    // vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_MESH_SHADER]->GetCommandBuffer());

    // Shadow pass
    // vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_SHADOW]->GetCommandBuffer());
    auto vShadowPassCmds = m_pShadowPassManager->GetCommandBuffers();
    vCmdBufs.insert(std::end(vCmdBufs), std::begin(vShadowPassCmds), std::end(vShadowPassCmds));

    // Submit graphics queue to signal depth ready semaphore
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_GBUFFER]->GetCommandBuffer());
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_OPAQUE_LIGHTING]->GetCommandBuffer());
    vSignalSemaphores.push_back(m_depthReady);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetGraphicsQueue(), vWaitForSemaphores,
                                            vSignalSemaphores, vWaitStages);

    vCmdBufs.clear();
    vSignalSemaphores.clear();

    // SKYBOX and TRANSPARENT are recorded by RecordStaticCmdBuffers but deliberately not submitted here.
    // That is pre-existing: the old submit built this batch and then cleared it before submitting, so the
    // legacy frame never contained either pass. Porting one has to make that visual change deliberately
    // rather than inherit it silently (#20 design note).

#ifdef FEATURE_RAY_TRACING
    // The ray tracing pass used to produce the image consumed by FINAL. Until it is ported (#253, #223,
    // #238) it still runs, but the graph's FINAL node samples the raster composite.
    if (m_vpRenderPasses[RENDERPASS_RAY_TRACING])
    {
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_RAY_TRACING]->GetCommandBuffer());
    }
#endif

    // Submit UI. UI writes the composite, so it waits on the batch that produced it.
    VkCommandBuffer uiCmdBuffer = m_vpRenderPasses[RENDERPASS_UI]->GetCommandBuffer();
    if (uiCmdBuffer != VK_NULL_HANDLE)  // it's possible there's no UI to draw
    {
        vCmdBufs.push_back(uiCmdBuffer);
    }

    vWaitForSemaphores.push_back(m_depthReady);
    vWaitStages.push_back(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    // The legacy scene does not touch the swapchain, but it consumes the acquire so the graph's single
    // wait (on m_sceneFinished) transitively orders the acquired image's availability.
    vWaitForSemaphores.push_back(imageAvailable);
    vWaitStages.push_back(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    vSignalSemaphores.push_back(m_sceneFinished);
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetGraphicsQueue(), vWaitForSemaphores,
                                            vSignalSemaphores, vWaitStages);
}

}  // namespace Muyo
