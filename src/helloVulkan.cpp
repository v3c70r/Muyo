#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#define ENABLE_RAY_TRACING

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

const int WIDTH = 1024;
const int HEIGHT = 768;

static bool s_bResizeWanted = false;
static GLFWwindow *s_pWindow = nullptr;
static Arcball s_arcball(glm::perspective(glm::radians(80.0f),
                                          (float)WIDTH / (float)HEIGHT, 0.1f,
                                          10.0f),
                         glm::lookAt(glm::vec3(0.0f, 0.0f, -2.0f),  // Eye
                                     glm::vec3(0.0f, 0.0f, 0.0f),   // Center
                                     glm::vec3(0.0f, 1.0f, 0.0f))); // Up

// Arcball callbacks
static void clickArcballCallback(GLFWwindow *window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (GLFW_PRESS == action)
        {
            s_arcball.startDragging();
        }
        else if (GLFW_RELEASE == action)
        {
            s_arcball.stopDragging();
        }
    }
}

static void rotateArcballCallback(GLFWwindow *window, double xpos, double ypos)
{
    s_arcball.updateDrag(glm::vec2(xpos, ypos));
}

// GLFW mouse callback
static void mouseCallback(GLFWwindow *window, int button, int action, int mods)
{
    if (ImGui::GetIO().WantCaptureMouse)
    {
        ImGui::MouseButtonCallback(window, button, action, mods);
    }
    else
    {
        clickArcballCallback(window, button, action, mods);
    }
}
static void mouseCursorCallback(GLFWwindow *window, double xpos, double ypos)
{
    rotateArcballCallback(window, xpos, ypos);
}

void scrollCallback(GLFWwindow* window, double xoffset,
                                   double yoffset)
{
    if (ImGui::GetIO().WantCaptureMouse)
    {
        ImGui::ScrollCallback(window, xoffset, yoffset);
    }
    else
    {
        s_arcball.AddZoom(yoffset * -0.1f);
    }
}
// GLFW key callbacks
static void onKeyStroke(GLFWwindow *window, int key, int scancode, int action,
                        int mods)
{
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        ImGui::KeyCallback(window, key, scancode, action, mods);
    }
    else
    {
        std::cout << "Key captured by engine\n";
    }
}
void charCallback(GLFWwindow* window, unsigned int c)
{
    ImGui::CharCallback(window, c);
}

#ifdef __APPLE__
static const uint32_t NUM_BUFFERS = 2;
#else
static const uint32_t NUM_BUFFERS = 3;
#endif

static DebugUtilsMessenger s_debugMessenger;

//Geometry* g_pQuadGeometry = nullptr;

///////////////////////////////////////////

static void onWindowResize(GLFWwindow *pWindow, int width, int height)
{
    s_arcball.resize(glm::vec2((float)width, (float)height));
    s_bResizeWanted = true;
}


void InitGLFW()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    s_pWindow = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    //glfwSetWindowSizeCallback(s_pWindow, onWindowResize);
    glfwSetKeyCallback(s_pWindow, onKeyStroke);
    glfwSetMouseButtonCallback(s_pWindow, mouseCallback);
    glfwSetCursorPosCallback(s_pWindow, mouseCursorCallback);
    glfwSetScrollCallback(s_pWindow, scrollCallback);
    glfwSetKeyCallback(s_pWindow, ImGui::KeyCallback);
    glfwSetCharCallback(s_pWindow, ImGui::CharCallback);
}

std::vector<const char *> GetRequiredInstanceExtensions()
{
    std::vector<const char *> vExtensions;
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;

    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    for (uint32_t i = 0; i < glfwExtensionCount; i++)
    {
        vExtensions.push_back(glfwExtensions[i]);
    }
    // This exteinsion is required by logical device's multiview extension
    vExtensions.push_back("VK_KHR_get_physical_device_properties2");
    return vExtensions;
}

std::vector<const char *> GetRequiredDeviceExtensions()
{
    std::vector<const char *> vDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MULTIVIEW_EXTENSION_NAME,

#ifdef ENABLE_RAY_TRACING
        // Ray tracing extensions
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        // VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        // VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        // VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
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
    glfwDestroyWindow(s_pWindow);
    glfwTerminate();
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
    InitGLFW();
    // Create Instace
    std::vector<const char *> vInstanceExtensions = GetRequiredInstanceExtensions();
    GetRenderDevice()->Initialize(vInstanceExtensions);

    // Create swapchain
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    glfwCreateWindowSurface(GetRenderDevice()->GetInstance(), s_pWindow, NULL, &surface);

    // Create device
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatrues = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_ADDRESS_FEATURES_EXT};
    std::vector<void*> features;
    features.push_back(&bufferDeviceAddressFeatrues);

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

    ImGui::Init(s_pWindow);

    {
        // Load scene
        //GLTFImporter importer;
        //g_vScenes = importer.ImportScene("assets/mazda_mx-5/scene.gltf");
        GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5/scene.gltf");

#ifdef ENABLE_RAY_TRACING
        {
            // Test ray tracing
            for (const auto &scenePair : GetSceneManager()->GetAllScenes())
            {
                BLASInput blas = ConstructBLASInput(scenePair.second);
            }
        }
#endif

        // Create perview constant buffer
        UniformBuffer<PerViewData> *pUniformBuffer =
            GetRenderResourceManager()->getUniformBuffer<PerViewData>(
                "perView");

        // Load materials
        GetMaterialManager()->CreateDefaultMaterial();

        // Record static command buffer
        GetRenderPassManager()->RecordStaticCmdBuffers(GetSceneManager()->GatherDrawLists());
        // Mainloop
        while (!glfwWindowShouldClose(s_pWindow))
        {
            glfwPollEvents();

            updateUniformBuffer(pUniformBuffer);

            GetRenderDevice()->BeginFrame();

            // Handle resizing
            {
                // TODO: Resizing doesn't work properly, need to investigate
                int width, height;
                glfwGetWindowSize(s_pWindow, &width, &height);
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
