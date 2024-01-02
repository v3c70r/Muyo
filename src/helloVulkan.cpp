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
#include "VkExtFuncsLoader.h"
#include "Camera.h"
#include "Debug.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "ImGuiControl.h"
#include "Material.h"
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderPass.h"
#include "RenderPassGBufferLighting.h"
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
#include "RayTracingSceneManager.h"
#endif

using namespace Muyo;

bool g_bWaylandExt = false;
const int WIDTH = 1920;
const int HEIGHT = 1080;

/// 
// Arcball callbacks
static void clickArcballCallback(int button, int action)
{
    Arcball *pArcball = dynamic_cast<Arcball *>(GetRenderPassManager()->GetCamera());
    if (pArcball)
    {
        if (button == Input::Button::BUTTON_LEFT)
        {
            if (EventState::PRESSED == action)
            {
                pArcball->StartDragging();
            }
            else if (EventState::RELEASED == action)
            {
                pArcball->StopDragging();
            }
        }
    }
}

static void rotateArcballCallback(double xpos, double ypos)
{
    Arcball *pArcball = dynamic_cast<Arcball *>(GetRenderPassManager()->GetCamera());
    if (pArcball)
    {
        pArcball->UpdateDrag(glm::vec2(xpos, ypos));
    }
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

    auto pWheel = EventSystem::sys()->globalEvent<EventType::MOUSEWHEEL, GlobalWheelEvent>();

    pWheel->Watch([](uint32_t timestamp, double xoffset, double yoffset)
                  {

    if (Arcball *pArcball = dynamic_cast<Arcball *>(GetRenderPassManager()->GetCamera()))
    {
        pArcball->AddZoom(yoffset * -0.1f);
    } });

    auto pResize = EventSystem::sys()->globalEvent<EventType::WINDOWRESIZE,
                                                   GlobalResizeEvent>();
}

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
        //VK_KHR_MULTIVIEW_EXTENSION_NAME,
        //VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,

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

    GetRenderPassManager()->Unintialize();
    GetRenderDevice()->DestroyCommandPools();
    GetRenderResourceManager()->Unintialize();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
    Window::Uninitialize();
}

void updateUniformBuffer(UniformBuffer<PerViewData> *ub)
{
    //s_arcball.UpdatePerViewDataUBO(ub);
};

int main(int argc, char** argv)
{
    // Load mesh into memory
    if (!Window::Initialize("hello Vulkan", WIDTH, HEIGHT))
        return -1;
    
    // Create Instace
    std::vector<const char *> vInstanceExtensions = GetRequiredInstanceExtensions();
    GetRenderDevice()->Initialize(vInstanceExtensions);
    VkExt::LoadInstanceFunctions(GetRenderDevice()->GetInstance());

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
    //features.push_back(&bufferDeviceAddressFeatrues);
    if (GetRenderDevice()->IsRayTracingSupported())
    {
        features.push_back(&rayTracingFeature);
        features.push_back(&accStructFeature);
    }

    GetRenderDevice()->CreateDevice(GetRequiredDeviceExtensions(),  // Extensions
                                    std::vector<const char *>(),    // Layers
                                    surface, features);

    GetMemoryAllocator()->Initalize(GetRenderDevice());

    GetRenderDevice()->CreateCommandPools();

    // Initialize managers
    GetDescriptorManager()->createDescriptorPool();
    GetDescriptorManager()->createDescriptorSetLayouts();

    GetSamplerManager()->createSamplers();


    InitEventHandlers();

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
            //GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5_emissive/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5_spotlight/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/balls/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/Cars/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/Emissive/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/Cornell_box_Emissive/untitled.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/Cornell_box/scene.gltf");
            GetSceneManager()->LoadSceneFromFile("assets/Cornell_box_spot/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/sofa_combination/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/StudioSetup/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/shiba/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/catherdral/scene.gltf");
            //GetSceneManager()->LoadSceneFromFile("C:/Users/mcvec/source/repos/v3c70r/Muyo/assets/glTF-Sample-Models/2.0/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf");
            //GetSceneManager()->LoadSceneFromFile("C:/Users/mcvec/Documents/cars/New Folder/cars.gltf");
            //GetSceneManager()->LoadSceneFromFile("assets/TestAssets/EnvironmentTest/glTF/EnvironmentTest.gltf");
        }

        // Finalize material buffer
        GetMaterialManager()->CreateDefaultMaterial();
        GetMaterialManager()->UploadMaterialBuffer();

		GetRenderPassManager()->Initialize(WIDTH, HEIGHT, surface);
        DrawLists dl = GetSceneManager()->GatherDrawLists();
        GetSceneManager()->ConstructLightBufferFromDrawLists(dl);

		UniformBuffer<PerViewData>* pUniformBuffer = GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");

        // Load materials

  
        

        // Test ray tracing
#ifdef FEATURE_RAY_TRACING

        RayTracingSceneManager RTSceneManager;
        RTSceneManager.BuildScene(dl.m_aDrawLists[DrawLists::DL_OPAQUE]);
        GetRenderPassManager()->SetRayTracingSceneManager(&RTSceneManager);
        // Allocate ray tracing descriptor layout based on number of textures in scene.
        //GetDescriptorManager()->CreateRayTracingDescriptorLayout(GetTextureResourceManager()->GetTextures().size());

        //RTBuilder rayTracingBuilder;
        //if (GetRenderDevice()->IsRayTracingSupported())
        //{
        //    // Test ray tracing
        //    RTInputs rtInputs;
        //    rtInputs = ConstructRTInputsFromDrawLists(dl);
        //    rayTracingBuilder.BuildBLAS(
        //        rtInputs.BLASs,
        //        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
        //    rayTracingBuilder.BuildTLAS(
        //        rtInputs.vInstances,
        //        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

        //    // Allocate primitive description buffer
        //    StorageBuffer<PrimitiveDescription>* primDescBuffer = GetRenderResourceManager()->GetStorageBuffer("primitive descs", rtInputs.vPrimitiveDescriptions);

        //    // Allocate output image
        //    ImageResource* rtOutputImage = GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Output", vpExtent, VK_FORMAT_R16G16B16A16_SFLOAT);

        //    rayTracingBuilder.BuildRTPipeline();
        //    rayTracingBuilder.BuildShaderBindingTable();
        //    rayTracingBuilder.RayTrace(vpExtent);
        //}
#endif
        // Create perview constant buffer
        //

        // Record static command buffer
        GetRenderPassManager()->RecordStaticCmdBuffers(dl);
       // Mainloop
        while (!Window::ShouldQuit())
        {
            Window::ProcessEvents();
            //updateUniformBuffer(pUniformBuffer);

            GetRenderPassManager()->BeginFrame();

            
            //uint32_t uFrameIdx = GetRenderDevice()->GetFrameIdx();
            GetRenderPassManager()->RecordDynamicCmdBuffers();

            GetRenderPassManager()->SubmitCommandBuffers();

            GetRenderPassManager()->Present();
            ImGui::Update();

            // Handle resizing
            {
                // TODO: Resizing doesn't work properly, need to investigate
                VK_ASSERT(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()));
                int width, height;
                std::tie(width, height) = Window::GetWindowSize();
                VkExtent2D currentVp = GetRenderPassManager()->GetViewportSize();
                if (width != (int)currentVp.width || height != (int)currentVp.height)
                {
                    //VkExtent2D vp = {(uint32_t)width, (uint32_t)height};
                    GetRenderPassManager()->OnResize(width, height);
                    GetRenderPassManager()->RecordStaticCmdBuffers(dl);
                    //GetRenderDevice()->SetViewportSize(vp);
                    //GetRenderPassManager()->SetSwapchainImageViews(pSwapchian->GetImageViews(), pDepthResource->getView());
                }
            }
        }
        std::cout << "Closing window, wait for device to finish..."
                  << std::endl;
        VK_ASSERT(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()));
        std::cout << "Device finished" << std::endl;

        // Destroy managers
        GetMaterialManager()->DestroyMaterials();
        GetGeometryManager()->Destroy();
        GetSamplerManager()->destroySamplers();
        GetTextureResourceManager()->Destroy();
    }
    ImGui::Shutdown();
    cleanup();
    return 0;
}
