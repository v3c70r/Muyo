#include <SDL2/SDL_events.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_video.h>
#include <algorithm>
#include <iterator>
#include <ostream>
#include <stdexcept>
#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#define ENABLE_RAY_TRACING

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cassert>  // assert
#include <chrono>
#include <cstring>  // strcmp
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

#include "../thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Camera.h"
#include "Debug.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "ImGuiGlfwControl.h"
#include "Material.h"
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderPass.h"
#include "RenderPassGBuffer.h"
#include "RenderLayerIBL.h"
#include "RenderPassUI.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "VertexBuffer.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
#include "SceneImporter.h"
#include "RenderPassManager.h"
#include "SceneManager.h"
#include "Swapchain.h"

#ifdef ENABLE_RAY_TRACING
#include "RayTracingUtils.h"
#endif

bool g_bWaylandExt = false;
const int WIDTH = 1024;
const int HEIGHT = 768;

static bool s_bResizeWanted = false;
static SDL_Window *s_pWindow = nullptr;
static Arcball s_arcball(glm::perspective(glm::radians(80.0f),
                                          (float)WIDTH / (float)HEIGHT, 0.1f,
                                          10.0f),
                         glm::lookAt(glm::vec3(0.0f, 0.0f, -2.0f),  // Eye
                                     glm::vec3(0.0f, 0.0f, 0.0f),   // Center
                                     glm::vec3(0.0f, 1.0f, 0.0f))); // Up

/// 
// Arcball callbacks
static void clickArcballCallback(int button, int action)
{
    if (button == SDL_BUTTON_LEFT)
    {
        if (SDL_PRESSED == action)
        {
            s_arcball.startDragging();
        }
        else if (SDL_RELEASED == action)
        {
            s_arcball.stopDragging();
        }
    }
}

static void rotateArcballCallback(double xpos, double ypos)
{
    s_arcball.updateDrag(glm::vec2(xpos, ypos));
}

///////////////////////////////////////////////////////////////////////////////
// event callbacks
///////////////////////////////////////////////////////////////////////////////

static void onMousePress(SDL_Window *window, int button, int action)
{
    if (ImGui::GetIO().WantCaptureMouse)
    {
        // ImGui::MouseButtonCallback(window, button, action, 0);
    }
    else
    {
        clickArcballCallback(button, action);
    }
}

static void onMouseMotion(SDL_Window *window, double xpos, double ypos)
{
    rotateArcballCallback(xpos, ypos);
}

void onMouseScroll(SDL_Window* window, double xoffset, double yoffset)
{
    if (ImGui::GetIO().WantCaptureMouse)
    {
        // ImGui::ScrollCallback(window, xoffset, yoffset);
    }
    else
    {
        s_arcball.AddZoom(yoffset * -0.1f);
    }
}
// GLFW key callbacks
static void onKeyStroke(SDL_Window *window, int key, int scancode, int action,
                        int mods)
{
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        // ImGui::KeyCallback(window, key, scancode, action, mods);
    }
    else
    {
        std::cout << "Key captured by engine\n";
    }
}

static void onWindowResize(SDL_Window *window, int width, int height)
{
    s_arcball.resize(glm::vec2((float)width, (float)height));
    s_bResizeWanted = true;
}

static void onWindowEvent(SDL_Window *window, SDL_WindowEvent& event)
{
    //based on SDL wiki, SDL_WINDOWEVENT_RESIZED doesn't handle programed
    //window size change
    if (event.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        onWindowResize(window, event.data1, event.data2);
    }
}

// void charCallback(GLFWwindow* window, unsigned int c)
// {
//     ImGui::CharCallback(window, c);
// }

static void
handleSDLEvent(SDL_Event& event)
{
    switch (event.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        onKeyStroke(s_pWindow,
                    event.key.keysym.sym,
                    event.key.keysym.scancode,
                    event.key.state,
                    event.key.keysym.mod);        
        break; //on keystroke
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        onMousePress(s_pWindow,
                     event.button.button,
                     event.button.state);
        break;
    case SDL_MOUSEMOTION:
        onMouseMotion(s_pWindow, event.motion.x, event.motion.y);
        break;
    case SDL_MOUSEWHEEL:
        onMouseScroll(s_pWindow, event.wheel.x, event.wheel.y);
        break;
    case SDL_WINDOWEVENT:
        onWindowEvent(s_pWindow, event.window);
    default:
        break;
    }
}

#ifdef __APPLE__
static const uint32_t NUM_BUFFERS = 2;
#else
static const uint32_t NUM_BUFFERS = 3;
#endif

static DebugUtilsMessenger s_debugMessenger;

//Geometry* g_pQuadGeometry = nullptr;

///////////////////////////////////////////

void InitSDL()
{
    SDL_Init(SDL_INIT_EVERYTHING);
    s_pWindow = SDL_CreateWindow("HelloVulkan",
                                 SDL_WINDOWPOS_CENTERED,
	                          SDL_WINDOWPOS_CENTERED,
                                 WIDTH, HEIGHT,
                                 SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
}

std::vector<const char *> GetRequiredInstanceExtensions()
{
    std::vector<const char *> vExtensions;
    uint32_t countExtensions = 0;
    bool waylandSurface = false;
    const char *waylandExt = "VK_KHR_wayland_surface";

    if (!SDL_Vulkan_GetInstanceExtensions(s_pWindow, &countExtensions,
                                          nullptr))
        throw std::runtime_error("Failed to query instance extensions");
    vExtensions.resize(countExtensions);
    if (!SDL_Vulkan_GetInstanceExtensions(s_pWindow, &countExtensions,
                                          vExtensions.data()))
        throw std::runtime_error("Failed to query instance extensions");
    //query if we have wayland surface, nvidia GPU will not work with it
    waylandSurface = std::find_if(std::begin(vExtensions),
                                  std::end(vExtensions),
                                  [waylandExt](const char *ext) {
                                      return std::strcmp(ext, waylandExt) == 0;
                                  }) != std::end(vExtensions);

    // This exteinsion is required by logical device's multiview extension
    vExtensions.push_back("VK_KHR_get_physical_device_properties2");
    return vExtensions;
}

std::vector<const char *> GetRequiredDeviceExtensions()
{
    std::vector<const char *> vDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MULTIVIEW_EXTENSION_NAME,

        // Ray tracing extensions
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        // VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        // VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        // VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
    };
    return vDeviceExtensions;
}

void cleanup()
{
    GetDescriptorManager()->destroyDescriptorSetLayouts();
    GetDescriptorManager()->destroyDescriptorPool();

    GetRenderDevice()->DestroySwapchain();
    GetRenderDevice()->DestroyCommandPools();
    GetRenderResourceManager()->Unintialize();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
    SDL_DestroyWindow(s_pWindow);
    SDL_Quit();
}

static bool bIrradianceMapGenerated = false;

void updateUniformBuffer(UniformBuffer<PerViewData> *ub)
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    double time = std::chrono::duration<float, std::chrono::seconds::period>(
                      currentTime - startTime)
                      .count() *
                  0.01;
    PerViewData ubo = {};
    ubo.model = glm::rotate(glm::mat4(1.0f), (float)time * glm::radians(10.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    // ubo.model = glm::rotate(glm::mat4(1.0), glm::degrees(time),
    // glm::vec3(0.0, 1.0, 0.0));
    ubo.model = glm::mat4(1.0);

    ubo.view = s_arcball.getViewMat();
    ubo.proj = s_arcball.getProjMat();

    // Update auxiliary matrices
    ubo.objectToView = ubo.view * ubo.model;
    ubo.viewToObject = glm::inverse(ubo.objectToView);
    ubo.normalObjectToView = glm::transpose(ubo.viewToObject);

    ub->setData(ubo);
};

int main()
{
    // Load mesh into memory
    InitSDL();
    // Create Instace
    std::vector<const char *> vInstanceExtensions = GetRequiredInstanceExtensions();
    GetRenderDevice()->Initialize(vInstanceExtensions);

    // Create swapchain
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    SDL_Vulkan_CreateSurface(s_pWindow, GetRenderDevice()->GetInstance(), &surface);

    // Create device
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatrues = {};
    bufferDeviceAddressFeatrues.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_ADDRESS_FEATURES_EXT;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeature = {};
    rayTracingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accStructFeature = {};
    accStructFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

    std::vector<void*> features;
    features.push_back(&bufferDeviceAddressFeatrues);
    if (GetRenderDevice()->IsRayTracingSupported())
    {
        features.push_back(&rayTracingFeature);
        features.push_back(&accStructFeature);
    }

    GetRenderDevice()->CreateDevice(GetRequiredDeviceExtensions(),  // Extensions
                                    std::vector<const char *>(),    // Layers
                                    surface, features);
    GetRenderDevice()->CreateSwapchain(surface);

    GetMemoryAllocator()->Initalize(GetRenderDevice());
    GetRenderDevice()->SetViewportSize(VkExtent2D{WIDTH, HEIGHT});

    GetRenderDevice()->CreateCommandPools();

    // Initialize managers
    GetDescriptorManager()->createDescriptorPool();
    GetDescriptorManager()->createDescriptorSetLayouts();

    GetSamplerManager()->createSamplers();

    VkExtent2D vpExtent = {WIDTH, HEIGHT};

    // Can it be in gbuffer depth?
    RenderTarget *pDepthResource =
        GetRenderResourceManager()->getDepthTarget(
            "depthTarget",
            vpExtent);

    Swapchain* pSwapchian = GetRenderDevice()->GetSwapchain();
    GetRenderPassManager()->Initialize(vpExtent.width, vpExtent.height, pSwapchian->GetImageFormat());
    GetRenderPassManager()->SetSwapchainImageViews(pSwapchian->GetImageViews(), pDepthResource->getView());

    // ImGui::Init(s_pWindow);

    {
        // Load scene
        //GLTFImporter importer;
        //g_vScenes = importer.ImportScene("assets/mazda_mx-5/scene.gltf");
        GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5/scene.gltf");

        if (GetRenderDevice()->IsRayTracingSupported())
        {
            RTBuilder rayTracingBuilder;
            // Test ray tracing

            RTInputs rtInputs;
            DrawLists dl = GetSceneManager()->GatherDrawLists();
            rtInputs = ConstructRTInputsFromDrawLists(dl);
            rayTracingBuilder.BuildBLAS(
                rtInputs.BLASs,
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
            rayTracingBuilder.BuildTLAS(
                rtInputs.TLASs,
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

            rayTracingBuilder.Cleanup();
        }
        // Create perview constant buffer
        //

        UniformBuffer<PerViewData> *pUniformBuffer =
            GetRenderResourceManager()->getUniformBuffer<PerViewData>(
                "perView");

        // Load materials
        GetMaterialManager()->CreateDefaultMaterial();

        // Record static command buffer
        GetRenderPassManager()->RecordStaticCmdBuffers(GetSceneManager()->GatherDrawLists());
        // Mainloop
        while (true)
        {
            SDL_Event event;

            SDL_PollEvent(&event);
            if (event.type == SDL_QUIT)
                break;
            handleSDLEvent(event);

            updateUniformBuffer(pUniformBuffer);

            GetRenderDevice()->BeginFrame();

            // Handle resizing
            {
                // TODO: Resizing doesn't work properly, need to investigate
                int width, height;
                SDL_GetWindowSize(s_pWindow, &width, &height);
                VkExtent2D currentVp = GetRenderDevice()->GetViewportSize();
                if (width != (int)currentVp.width || height != (int)currentVp.height)
                {
                    VkExtent2D vp = {(uint32_t)width, (uint32_t)height};
                    GetRenderDevice()->SetViewportSize(vp);
                    //GetRenderPassManager()->SetSwapchainImageViews(pSwapchian->GetImageViews(), pDepthResource->getView());
                    s_bResizeWanted = false;
                    continue;
                }
            }
            uint32_t uFrameIdx = GetRenderDevice()->GetFrameIdx();
            VkExtent2D vpExt = {WIDTH, HEIGHT};
            GetRenderPassManager()->RecordDynamicCmdBuffers(uFrameIdx, vpExt);

            std::vector<VkCommandBuffer> vCmdBufs = GetRenderPassManager()->GetCommandBuffers(uFrameIdx);
            GetRenderDevice()->SubmitCommandBuffers(vCmdBufs);

            GetRenderDevice()->Present();
            ImGui::UpdateMousePosAndButtons();
            ImGui::UpdateMouseCursor();
            ImGui::UpdateGamepads();
        }
        std::cout << "Closing window, wait for device to finish..."
                  << std::endl;
        assert(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()) == VK_SUCCESS);
        std::cout << "Device finished" << std::endl;

        // Destroy managers
        GetMaterialManager()->destroyMaterials();
        GetGeometryManager()->Destroy();
        GetSamplerManager()->destroySamplers();
        GetTextureManager()->Destroy();
    }
    ImGui::Shutdown();
    GetRenderPassManager()->Unintialize();
    cleanup();
    return 0;
}
