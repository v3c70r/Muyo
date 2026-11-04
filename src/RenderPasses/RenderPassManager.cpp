#include "RenderPassManager.h"

#include <algorithm>
#include <cassert>
#include <memory>

#include "RenderLayerIBL.h"
#include "RenderPass.h"
#include "RenderPassCubeMapGeneration.h"
#include "RenderResourceManager.h"
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

    // IBL pass
    m_vpRenderPasses[RENDERPASS_IBL] = std::make_unique<RenderLayerIBL>();

    m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION] =
        std::make_unique<RenderPassCubeMapGeneration>(VkExtent2D({128, 128}));

#ifdef FEATURE_RAY_TRACING
    // RenderPass Ray Tracing
    m_vpRenderPasses[RENDERPASS_RAY_TRACING] =
        std::make_unique<RenderPassRayTracing>(VkExtent2D({m_uWidth, m_uHeight}));
#else
    m_vpRenderPasses[RENDERPASS_RAY_TRACING] = nullptr;
#endif

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
    for (auto &pPass : m_vpRenderPasses)
    {
        pPass = nullptr;
    }
    vkDestroySemaphore(GetRenderDevice()->GetDevice(), m_sceneFinished, nullptr);
}

void RenderPassManager::RecordStaticCmdBuffers()
{
    {
        auto *pCubeMapGenerationPass =
            static_cast<RenderPassCubeMapGeneration *>(m_vpRenderPasses[RENDERPASS_CUBEMAP_GENERATION].get());
        pCubeMapGenerationPass->PrepareRenderPass();
        pCubeMapGenerationPass->RecordCommandBuffers();
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
