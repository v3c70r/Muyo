#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <vector>
#include <string>

class RenderPass;
class Swapchain;
class RayTracingSceneManager;
class Camera;
struct DrawLists;

enum RenderPassNames
{
    // Order matters
    RENDERPASS_IBL,
    RENDERPASS_GBUFFER,
    RENDERPASS_SKYBOX,
    RENDERPASS_TRANSPARENT,

    // Render directpy to swapchain
    RENDERPASS_FINAL,
    RENDERPASS_UI,

    RENDERPASS_AO,

    RENDERPASS_RAY_TRACING,

    RENDERPASS_COUNT
};
class RenderPassManager
{
public:
    void CreateSwapchain(const VkSurfaceKHR& swapchainSurface);
    void BeginFrame();
    void Present();
        
    void Initialize(uint32_t uWidth, uint32_t uHeight, const VkSurfaceKHR& swapchainSurface);
    void SetSwapchainImageViews(const std::vector<VkImageView> &vImageViews, VkImageView depthImageView);
    void OnResize(uint32_t uWidth, uint32_t uHeight);
    void Unintialize();
    void RecordStaticCmdBuffers(const DrawLists& drawLists);
    void RecordDynamicCmdBuffers();
    std::vector<VkCommandBuffer> GetCommandBuffers(uint32_t uImgIdx);
    void ReloadEnvironmentMap(const std::string& sNewEnvMapPath);
    VkExtent2D GetViewportSize() const {return VkExtent2D({m_uWidth, m_uHeight});}

    void SubmitCommandBuffers();

    Camera* GetCamera() {return m_pCamera.get();}

#ifdef FEATURE_RAY_TRACING
    void SetRayTracingSceneManager(const RayTracingSceneManager* pSceneManager)
    {
        m_pRayTracingSceneManager = pSceneManager;
    }
#endif

private:
#ifdef __APPLE__
    static const uint32_t NUM_BUFFERS = 2;
#else
    static const uint32_t NUM_BUFFERS = 3;
#endif
    const VkSurfaceFormatKHR SWAPCHAIN_FORMAT = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    const VkPresentModeKHR PRESENT_MODE = VK_PRESENT_MODE_FIFO_KHR;

private:
    std::array<std::unique_ptr<RenderPass>, RENDERPASS_COUNT> m_vpRenderPasses = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    uint32_t m_uWidth = 0;
    uint32_t m_uHeight = 0;
    bool m_bIsIrradianceGenerated = false;

    // Synchronization elements required in passes
    VkSemaphore m_depthReady = VK_NULL_HANDLE;
    VkSemaphore m_aoReady = VK_NULL_HANDLE;
    VkSemaphore m_imageAvailable= VK_NULL_HANDLE;   // Semaphores to notify the frame when the current image is ready
    VkSemaphore m_renderFinished= VK_NULL_HANDLE;

    std::array<VkFence, NUM_BUFFERS> m_aGPUExecutionFence;
    uint32_t m_uImageIdx2Present = 0;

    std::unique_ptr<Swapchain> m_pSwapchain = nullptr;
    std::unique_ptr<Camera> m_pCamera = nullptr;

#ifdef FEATURE_RAY_TRACING
    const RayTracingSceneManager* m_pRayTracingSceneManager = nullptr;
#endif

    struct TemporalInfo
    {
        uint32_t nFrameId = 0;
        uint32_t nFrameNoCameraMove = 0;
    } m_temporalInfo;
};

RenderPassManager* GetRenderPassManager();
