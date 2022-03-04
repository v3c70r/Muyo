#include <algorithm>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <tuple>
#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE


#include <GLFW/glfw3.h>
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
#include <filesystem>

#include "../thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Camera.h"
#include "Debug.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "ImGuiControl.h"
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
#include "EventSystem.h"

#if defined (USE_SDL)
#include "WindowSDL.h"
#else
#include "WindowGLFW.h"
#endif


#ifdef FEATURE_RAY_TRACING
#include "RayTracingUtils.h"
#endif

bool g_bWaylandExt = false;
const int WIDTH = 1920;
const int HEIGHT = 1080;

static bool s_bResizeWanted = false;
const float FAR = 100.0f;
static Arcball s_arcball(glm::perspective(glm::radians(80.0f),
                                          (float)WIDTH / (float)HEIGHT, 0.1f,
                                          FAR),
                         glm::lookAt(glm::vec3(0.0f, 0.0f, -2.0f),  // Eye
                                     glm::vec3(0.0f, 0.0f, 0.0f),   // Center
                                     glm::vec3(0.0f, 1.0f, 0.0f)),  // Up
                         0.1f,                                      // near
                         FAR,                                     // far
                         (float)WIDTH,
                         (float)HEIGHT

);

/// 
// Arcball callbacks
static void clickArcballCallback(int button, int action)
{
    if (button == Input::Button::BUTTON_LEFT)
    {
        if (EventState::PRESSED == action)
        {
            s_arcball.StartDragging();
        }
        else if (EventState::RELEASED == action)
        {
            s_arcball.StopDragging();
        }
    }
}

static void rotateArcballCallback(double xpos, double ypos)
{
    s_arcball.UpdateDrag(glm::vec2(xpos, ypos));
}

///////////////////////////////////////////////////////////////////////////////
// event callbacks
///////////////////////////////////////////////////////////////////////////////

static void InitEventHandlers()
{
    auto pMove = EventSystem::sys()->globalEvent<EventType::MOUSEMOTION,
                                                 GlobalMotionEvent>();
    pMove->Watch([](uint32_t timestamp, float sx, float sy) {
        rotateArcballCallback(sx, sy);
    });

    auto pBtn = EventSystem::sys()->globalEvent<EventType::MOUSEBUTTON,
                                                GlobalButtonEvent>();
    pBtn->Watch([](uint32_t timestamp, Input::Button btn, EventState state) {
        clickArcballCallback(btn, state);
    });

    auto pWheel = EventSystem::sys()->globalEvent<EventType::MOUSEWHEEL,
                                                  GlobalWheelEvent>();
    pWheel->Watch([](uint32_t timestamp, double xoffset, double yoffset) {
        s_arcball.AddZoom(yoffset * -0.1f);
    });

    auto pResize = EventSystem::sys()->globalEvent<EventType::WINDOWRESIZE,
                                                   GlobalResizeEvent>();
    pResize->Watch([](uint32_t timestamp, size_t w, size_t h) {
        s_arcball.Resize(glm::vec2((float)w, (float)h));
        s_bResizeWanted = true;        
    });
}

#ifdef __APPLE__
static const uint32_t NUM_BUFFERS = 2;
#else
static const uint32_t NUM_BUFFERS = 3;
#endif

static DebugUtilsMessenger s_debugMessenger;

//Geometry* g_pQuadGeometry = nullptr;

///////////////////////////////////////////

std::vector<const char *> GetRequiredInstanceExtensions()
{
    std::vector<const char *> vExtensions;
    uint32_t countExtensions = 0;

    countExtensions = Window::GetVulkanInstanceExtensions(vExtensions);
    if (countExtensions == 0 || vExtensions.size() == 0) 
        throw std::runtime_error("Failed to query instance extensions");    
    //const char *waylandExt = "VK_KHR_wayland_surface";
    //query if we have wayland surface, nvidia GPU will not work with it
    //bool waylandSurface = std::find_if(std::begin(vExtensions),
    //                              std::end(vExtensions),
    //                              [waylandExt](const char *ext) {
    //                                  return std::strcmp(ext, waylandExt) == 0;
    //                              }) != std::end(vExtensions);

    // This exteinsion is required by logical device's multiview extension
    vExtensions.push_back("VK_KHR_get_physical_device_properties2");
    return vExtensions;
}

std::vector<const char *> GetRequiredDeviceExtensions()
{
    std::vector<const char *> vDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MULTIVIEW_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,

#ifdef FEATURE_SYNCHRONIZATION2
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
#endif

#ifdef FEATURE_RAY_TRACING
        // Ray tracing extensions
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
#endif
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
    Window::Uninitialize();
}

static bool bIrradianceMapGenerated = false;

void updateUniformBuffer(UniformBuffer<PerViewData> *ub)
{
    s_arcball.UpdatePerViewDataUBO(ub);
};

int main(int argc, char** argv)
{
    // Load mesh into memory
    if (!Window::Initialize("hello Vulkan", WIDTH, HEIGHT))
        return -1;
    
    // Create Instace
    std::vector<const char *> vInstanceExtensions = GetRequiredInstanceExtensions();
    GetRenderDevice()->Initialize(vInstanceExtensions);

    // Create swapchain
    VkSurfaceKHR surface = Window::GetVulkanSurface(GetRenderDevice()->GetInstance());

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
#ifdef FEATURE_SYNCHRONIZATION2
    VkPhysicalDeviceAccelerationStructureFeaturesKHR sync2Feature = {};
    sync2Feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
	features.push_back(&sync2Feature);
#endif

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
        GetRenderResourceManager()->GetDepthTarget(
            "depthTarget",
            vpExtent);

    Swapchain* pSwapchian = GetRenderDevice()->GetSwapchain();
    GetRenderPassManager()->Initialize(vpExtent.width, vpExtent.height);
    GetRenderPassManager()->SetSwapchainImageViews(pSwapchian->GetImageViews(), pDepthResource->getView());

    InitEventHandlers();
    ImGui::Init();

    {
        bool bFileFromArg = false;
        if (argc == 2)
        {
            namespace fs = std::filesystem;
            fs::path path(argv[1]);
            if (fs::exists(path))
            {
                std::string sPath{path.relative_path().string()};
                GetSceneManager()->LoadSceneFromFile(sPath);
                bFileFromArg = true;
            }
        }

        if (!bFileFromArg)
        {
            // Load scene
            //GetSceneManager()->LoadSceneFromFile("assets/triangle/scene.gltf");
            GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/sofa_combination/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/StudioSetup/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/shiba/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/catherdral/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("C:/Users/mcvec/source/repos/v3c70r/Muyo/assets/glTF-Sample-Models/2.0/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf");
            //GetSceneManager()->LoadSceneFromFile("C:/Users/mcvec/Documents/cars/New Folder/cars.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/TestAssets/EnvironmentTest/glTF/EnvironmentTest.gltf");
        }

        DrawLists dl = GetSceneManager()->GatherDrawLists();
        StorageBuffer<LightData>* lightData = GetSceneManager()->ConstructLightBufferFromDrawLists(dl);

#ifdef FEATURE_RAY_TRACING
        // Allocate ray tracing descriptor layout based on number of textures in scene.
        GetDescriptorManager()->CreateRayTracingDescriptorLayout(GetTextureManager()->GetTextures().size());

        RTBuilder rayTracingBuilder;
        if (GetRenderDevice()->IsRayTracingSupported())
        {
            // Test ray tracing
            RTInputs rtInputs;
            rtInputs = ConstructRTInputsFromDrawLists(dl);
            rayTracingBuilder.BuildBLAS(
                rtInputs.BLASs,
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
            rayTracingBuilder.BuildTLAS(
                rtInputs.vInstances,
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

            // Allocate primitive description buffer
            StorageBuffer<PrimitiveDescription>* primDescBuffer = GetRenderResourceManager()->GetStorageBuffer("primitive descs", rtInputs.vPrimitiveDescriptions);

            // Allocate output image
            ImageResource* rtOutputImage = GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Output", vpExtent, VK_FORMAT_R16G16B16A16_SFLOAT);

            rayTracingBuilder.BuildRTPipeline();
            rayTracingBuilder.BuildShaderBindingTable();
            rayTracingBuilder.RayTrace(vpExtent);
        }
#endif
        // Create perview constant buffer
        //

        UniformBuffer<PerViewData> *pUniformBuffer =
            GetRenderResourceManager()->getUniformBuffer<PerViewData>(
                "perView");

        // Load materials
        GetMaterialManager()->CreateDefaultMaterial();

        // Record static command buffer
        GetRenderPassManager()->RecordStaticCmdBuffers(dl);
        // Mainloop
        while (!Window::ShouldQuit())
        {
            Window::ProcessEvents();
            updateUniformBuffer(pUniformBuffer);

            GetRenderDevice()->BeginFrame();

            
            uint32_t uFrameIdx = GetRenderDevice()->GetFrameIdx();
            GetRenderPassManager()->RecordDynamicCmdBuffers(uFrameIdx, GetRenderDevice()->GetViewportSize());

            std::vector<VkCommandBuffer> vCmdBufs = GetRenderPassManager()->GetCommandBuffers(uFrameIdx);
#ifdef FEATURE_RAY_TRACING
            // Hack to patch the ray tracing command buffer
            // Insert it after the first cmd buffer because the first cmd buffer can be the one to generate IBL images.
            // IBL images are required by ray tracing pass.
            if (GetRenderDevice()->IsRayTracingSupported())
            {
                vCmdBufs.insert(vCmdBufs.begin()+1, rayTracingBuilder.GetCommandBuffer());
            }
#endif
            GetRenderDevice()->SubmitCommandBuffers(vCmdBufs);

            GetRenderDevice()->Present();
            ImGui::Update();

            // Handle resizing
            {
                // TODO: Resizing doesn't work properly, need to investigate
                assert(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()) == VK_SUCCESS);
                int width, height;
                std::tie(width, height) = Window::GetWindowSize();
                VkExtent2D currentVp = GetRenderDevice()->GetViewportSize();
                if (width != (int)currentVp.width || height != (int)currentVp.height)
                {
                    //VkExtent2D vp = {(uint32_t)width, (uint32_t)height};
                    GetRenderPassManager()->OnResize(width, height);
                    s_arcball.Resize(glm::vec2((float)width, (float)height));
                    GetRenderPassManager()->RecordStaticCmdBuffers(dl);
                    //GetRenderDevice()->SetViewportSize(vp);
                    //GetRenderPassManager()->SetSwapchainImageViews(pSwapchian->GetImageViews(), pDepthResource->getView());
                    s_bResizeWanted = false;
                }
            }
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
#ifdef FEATURE_RAY_TRACING
        rayTracingBuilder.Cleanup();
#endif
    }
    ImGui::Shutdown();
    GetRenderPassManager()->Unintialize();
    cleanup();
    return 0;
}
