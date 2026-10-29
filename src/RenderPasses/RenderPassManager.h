#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ShadowPassManager.h"

namespace Muyo
{
class IRenderPass;
class RayTracingSceneManager;
class Camera;
struct DrawLists;

enum RenderPassNames
{
    // Order matters
    RENDERPASS_IBL,

    // TODO(P3): merge ibl passes with a manager - deleted with src/RenderPasses/.
    RENDERPASS_CUBEMAP_GENERATION,
    RENDERPASS_MESH_SHADER,

    RENDERPASS_GBUFFER,
    RENDERPASS_OPAQUE_LIGHTING,

    RENDERPASS_SKYBOX,
    RENDERPASS_TRANSPARENT,

    RENDERPASS_UI,

    RENDERPASS_RAY_TRACING,

    RENDERPASS_COUNT
};
/// The legacy scene renderer.
///
/// Since #20 increment 1 it no longer owns the swapchain, acquire or present: `FrameSync` does, and the
/// graph presents. This class records the scene passes that have not been ported and submits them,
/// signalling `GetSceneFinishedSemaphore()` when the composite is ready for the graph's `FINAL` node.
/// The bridge and its removal are named on #20.
class RenderPassManager
{
public:
    void Initialize(uint32_t uWidth, uint32_t uHeight);
    void Unintialize();
    void RecordStaticCmdBuffers(const DrawLists& drawLists);
    void RecordDynamicCmdBuffers();
    /// Advance the camera's temporal state and upload `perView`. Call once per frame before recording the
    /// UI (which reads the camera through `DebugUI`) and before `SubmitSceneCommandBuffers`.
    void UpdatePerViewData();
    void ReloadEnvironmentMap(const std::string& sNewEnvMapPath);
    VkExtent2D GetViewportSize() const { return VkExtent2D({.width = m_uWidth, .height = m_uHeight}); }

    /// Record and submit the legacy scene passes for one frame.
    ///
    /// The submit consumes `imageAvailable` (the swapchain acquire) and signals
    /// `GetSceneFinishedSemaphore()`. The graph's `FINAL` node waits on that semaphore before sampling the
    /// imported composite, so the swapchain image's availability is chained through it - the graph itself
    /// only declares the one wait `RenderGraphExecuteInfo` carries.
    /// @param imageAvailable Semaphore the swapchain acquire signalled this frame.
    void SubmitSceneCommandBuffers(VkSemaphore imageAvailable);

    /// @return The semaphore signalled when `SubmitSceneCommandBuffers`' work has completed.
    VkSemaphore GetSceneFinishedSemaphore() const { return m_sceneFinished; }

    Camera* GetCamera() { return m_pCamera.get(); }

#ifdef FEATURE_RAY_TRACING
    void SetRayTracingSceneManager(const RayTracingSceneManager* pSceneManager)
    {
        m_pRayTracingSceneManager = pSceneManager;
    }
#endif

    std::vector<const IRenderPass*> GetRenderPasses() const
    {
        std::vector<const IRenderPass*> vpRenderPasses;
        vpRenderPasses.reserve(m_vpRenderPasses.size());
        for (const auto& pRenderPass : m_vpRenderPasses)
        {
            vpRenderPasses.push_back(pRenderPass.get());
        }
        return vpRenderPasses;
    }

private:
    std::array<std::unique_ptr<IRenderPass>, RENDERPASS_COUNT> m_vpRenderPasses = {nullptr, nullptr, nullptr,
                                                                                   nullptr, nullptr, nullptr};
    uint32_t m_uWidth = 0;
    uint32_t m_uHeight = 0;
    bool m_bIsIrradianceGenerated = false;

    // Synchronization elements required in passes. Acquire and present belong to FrameSync since #20
    // increment 1; `m_sceneFinished` is the named bridge the graph's FINAL node waits on.
    VkSemaphore m_depthReady = VK_NULL_HANDLE;
    VkSemaphore m_sceneFinished = VK_NULL_HANDLE;

    std::unique_ptr<Camera> m_pCamera = nullptr;

#ifdef FEATURE_RAY_TRACING

    const RayTracingSceneManager* m_pRayTracingSceneManager = nullptr;

#endif

    struct TemporalInfo
    {
        uint32_t nFrameId = 0;
        uint32_t nFrameNoCameraMove = 0;
    } m_temporalInfo;

    std::unique_ptr<ShadowPassManager> m_pShadowPassManager;
};

RenderPassManager* GetRenderPassManager();
}  // namespace Muyo
