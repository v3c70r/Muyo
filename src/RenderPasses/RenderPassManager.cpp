#include "RenderPassManager.h"

#include <algorithm>
#include <cassert>
#include <memory>

#include "RenderLayerIBL.h"
#include "RenderPass.h"
#include "RenderPassCubeMapGeneration.h"
#include "RenderPassGBufferMeshShader.h"
#include "RenderPassRSM.h"
#include "RenderPassSkybox.h"
#include "RenderPassTransparent.h"
#include "RenderResourceManager.h"
#include "RenderResourceNames.h"
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

    // The unported SKYBOX and TRANSPARENT passes still name these two targets while their command buffers
    // are recorded and dropped. The GBUFFER/LIGHTING targets are graph-owned now, so these exist only for
    // those two passes and nothing writes them; they go with the passes (#51).
    GetRenderResourceManager()->GetRenderTarget(OPAQUE_LIGHTING_OUTPUT_ATTACHMENT_NAME, vp,
                                                VK_FORMAT_R16G16B16A16_SFLOAT);
    GetRenderResourceManager()->GetDepthTarget("GBufferDepth_", vp);

    // Create the bridge semaphore
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    // The bridge the graph's first scene node waits on (#20); removed with the last legacy scene pass.
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

void RenderPassManager::ReloadEnvironmentMap(const std::string &sNewEnvMapPath)
{
    auto *pIBLPass = static_cast<RenderLayerIBL *>(m_vpRenderPasses[RENDERPASS_IBL].get());
    pIBLPass->ReloadEnvironmentMap(sNewEnvMapPath);

    m_bIsIrradianceGenerated = false;
}

void RenderPassManager::SubmitSceneCommandBuffers(VkSemaphore imageAvailable)
{
    // Since #20's GBUFFER/LIGHTING increment this is not the scene any more: those passes are graph
    // nodes. What remains here is the legacy IBL/cubemap work (and the unported RT pass), recorded by
    // RecordStaticCmdBuffers. One submit consumes the acquire and signals the bridge the graph waits on.
    std::vector<VkCommandBuffer> vCmdBufs;

    if (!m_bIsIrradianceGenerated)
    {
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_IBL]->GetCommandBuffer());
        m_bIsIrradianceGenerated = true;
    }

    // Debug cubemap generation
    vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION]->GetCommandBuffer());

    // Mesh shader
    // vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_MESH_SHADER]->GetCommandBuffer());

    // The shadow command buffers are recorded but no longer submitted: their only consumer was the
    // deleted legacy lighting pass. Keeping them out is deliberate rather than an oversight; #49 ports
    // the RSM passes to the graph and they become real work again there.
    // auto vShadowPassCmds = m_pShadowPassManager->GetCommandBuffers();
    // vCmdBufs.insert(std::end(vCmdBufs), std::begin(vShadowPassCmds), std::end(vShadowPassCmds));

    // SKYBOX and TRANSPARENT are recorded by RecordStaticCmdBuffers but deliberately not submitted here.
    // That is pre-existing: the old submit built this batch and then cleared it before submitting, so the
    // legacy frame never contained either pass. Porting one has to make that visual change deliberately
    // rather than inherit it silently (#20 design note).

#ifdef FEATURE_RAY_TRACING
    // The ray tracing pass used to produce the image consumed by FINAL. Until it is ported (#253, #223,
    // #238) it still runs, but the graph presents the raster composite.
    if (m_vpRenderPasses[RENDERPASS_RAY_TRACING])
    {
        vCmdBufs.push_back(m_vpRenderPasses[RENDERPASS_RAY_TRACING]->GetCommandBuffer());
    }
#endif

    std::vector<VkSemaphore> vWaitForSemaphores = {imageAvailable};
    std::vector<VkPipelineStageFlags2> vWaitStages = {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    std::vector<VkSemaphore> vSignalSemaphores = {m_sceneFinished};
    // The legacy scene does not touch the swapchain, but it consumes the acquire so the graph's single
    // wait (on m_sceneFinished) transitively orders the acquired image's availability.
    GetRenderDevice()->SubmitCommandBuffers(vCmdBufs, GetRenderDevice()->GetGraphicsQueue(), vWaitForSemaphores,
                                            vSignalSemaphores, vWaitStages);
}

}  // namespace Muyo
