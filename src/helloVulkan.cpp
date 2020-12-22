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

#include "../thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Camera.h"
#include "Debug.h"
#include "DescriptorManager.h"
#include "GLFWSwapchain.h"
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

// TODO: Move them to renderpass manager
std::unique_ptr<RenderPassFinal> pFinalPass = nullptr;
std::unique_ptr<RenderPassGBuffer> pGBufferPass = nullptr;
std::unique_ptr<RenderPassUI> pUIPass = nullptr;
std::unique_ptr<RenderLayerIBL> pIBLPass = nullptr;

const int WIDTH = 800;
const int HEIGHT = 600;

static bool s_resizeWanted = true;

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
    clickArcballCallback(window, button, action, mods);
    ImGui::MouseButtonCallback(window, button, action, mods);
}
static void mouseCursorCallback(GLFWwindow *window, double xpos, double ypos)
{
    rotateArcballCallback(window, xpos, ypos);
}

void scrollCallback(GLFWwindow* window, double xoffset,
                                   double yoffset)
{
    s_arcball.AddZoom(yoffset * -0.1f);
    ImGui::ScrollCallback(window, xoffset, yoffset);
}
// GLFW key callbacks
static void onKeyStroke(GLFWwindow *window, int key, int scancode, int action,
                        int mods)
{
    std::cout << "Key pressed\n";
    ImGui::KeyCallback(window, key, scancode, action, mods);
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

GLFWSwapchain *s_pSwapchain = nullptr;


// sync
static std::vector<VkSemaphore> s_imageAvailableSemaphores;
static std::vector<VkSemaphore> s_renderFinishedSemaphores;
static std::vector<VkFence> s_waitFences;

// PHYSICAL Device extensions
const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    //"VK_KHR_ray_tracing"
};

Geometry* g_pQuadGeometry = nullptr;
std::vector<Scene> g_vScenes;

///////////////////////////////////////////

void recreateSwapChain(); // fwd declaration
static void onWindowResize(GLFWwindow *pWindow, int width, int height)
{
    s_arcball.resize(glm::vec2((float)width, (float)height));
    GetRenderDevice()->SetViewportSize(VkExtent2D(
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}));
    recreateSwapChain();
}


void initWindow()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    s_pWindow = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowSizeCallback(s_pWindow, onWindowResize);
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
        //#ifndef __APPLE__
        VK_KHR_MULTIVIEW_EXTENSION_NAME,
        //#endif
    };
    return vDeviceExtensions;
}

// swap chain details
void createCommandBuffers()
{
    const std::vector<const SceneNode *> vpSceneNodes =
        g_vScenes[0].FlattenTree();
    std::vector<const Geometry *> vpGeometries;
    vpGeometries.reserve(vpSceneNodes.size());
    for (const SceneNode *pNode : vpSceneNodes)
    {
        vpGeometries.push_back(
            static_cast<const GeometrySceneNode *>(pNode)->GetGeometry());
    }
    pGBufferPass->recordCommandBuffer(vpGeometries);

    pFinalPass->RecordOnce(
        *g_pQuadGeometry,
        GetRenderResourceManager()
            ->getColorTarget("LIGHTING_OUTPUT", VkExtent2D({0, 0}))
            ->getView());
}

void createSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    s_imageAvailableSemaphores.resize(NUM_BUFFERS);
    s_renderFinishedSemaphores.resize(NUM_BUFFERS);
    for (size_t i = 0; i < NUM_BUFFERS; i++)
    {
        assert(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr,
                                 &s_imageAvailableSemaphores[i]) == VK_SUCCESS);
        assert(vkCreateSemaphore(GetRenderDevice()->GetDevice(), &semaphoreInfo, nullptr,
                                 &s_renderFinishedSemaphores[i]) == VK_SUCCESS);
    }
}

void createFences()
{
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    s_waitFences.resize(NUM_BUFFERS);
    for (auto &fence : s_waitFences)
    {
        assert(vkCreateFence(GetRenderDevice()->GetDevice(), &fenceInfo, nullptr, &fence) ==
               VK_SUCCESS);
    }
}


void cleanupSwapChain()
{
    s_pSwapchain->destroySwapchain();
}

void recreateSwapChain()
{
    int width, height;
    glfwGetWindowSize(s_pWindow, &width, &height);
    if (width == 0 || height == 0)
        return;

    vkDeviceWaitIdle(GetRenderDevice()->GetDevice());

    cleanupSwapChain();

    s_pSwapchain->createSwapchain(
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VK_PRESENT_MODE_FIFO_KHR, NUM_BUFFERS);

    GetRenderResourceManager()->removeResource("depthTarget");

    RenderTarget *pDepthResource = GetRenderResourceManager()->getDepthTarget(
        "depthTarget", VkExtent2D({s_pSwapchain->getSwapchainExtent().width,
                                   s_pSwapchain->getSwapchainExtent().height}));

    pFinalPass->setSwapchainImageViews(
        s_pSwapchain->getImageViews(), pDepthResource->getView(),
        s_pSwapchain->getSwapchainExtent().width,
        s_pSwapchain->getSwapchainExtent().height);

    pUIPass->setSwapchainImageViews(
        s_pSwapchain->getImageViews(), pDepthResource->getView(),
        s_pSwapchain->getSwapchainExtent().width,
        s_pSwapchain->getSwapchainExtent().height);

    pGBufferPass->removeGBufferViews();
    pGBufferPass->createGBufferViews(s_pSwapchain->getSwapchainExtent());

    createCommandBuffers();
}

void cleanup()
{
    cleanupSwapChain();

    pFinalPass = nullptr;
    pUIPass = nullptr;
    pGBufferPass = nullptr;
    pIBLPass = nullptr;

    GetDescriptorManager()->destroyDescriptorSetLayouts();
    GetDescriptorManager()->destroyDescriptorPool();

    for (auto &somaphore : s_imageAvailableSemaphores)
        vkDestroySemaphore(GetRenderDevice()->GetDevice(), somaphore, nullptr);
    for (auto &somaphore : s_renderFinishedSemaphores)
        vkDestroySemaphore(GetRenderDevice()->GetDevice(), somaphore, nullptr);
    for (auto &fence : s_waitFences)
        vkDestroyFence(GetRenderDevice()->GetDevice(), fence, nullptr);

    GetRenderDevice()->DestroyCommandPools();
    GetRenderResourceManager()->Unintialize();
    GetMemoryAllocator()->Unintialize();
    s_pSwapchain->destroySurface();
    s_pSwapchain = nullptr;
    delete s_pSwapchain;
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
    glfwDestroyWindow(s_pWindow);
    glfwTerminate();
}

uint32_t beginFrame()
{
    // Begin new frame and get the frame id
    uint32_t nIamgeIndex = s_pSwapchain->getNextImage(s_imageAvailableSemaphores[0]);


    vkWaitForFences(GetRenderDevice()->GetDevice(), 1, &s_waitFences[nIamgeIndex], VK_TRUE,
                    std::numeric_limits<uint64_t>::max());
    vkResetFences(GetRenderDevice()->GetDevice(), 1, &s_waitFences[nIamgeIndex]);
    return nIamgeIndex;
}

static bool bIrradianceMapGenerated = false;
void present(uint32_t nIamgeIndex)
{
    // Record command buffers
    pUIPass->recordCommandBuffer(s_pSwapchain->getSwapchainExtent(), nIamgeIndex);

    // submit command buffer
    VkPipelineStageFlags stageFlag =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // only color attachment
                                                        // waits for the
                                                        // semaphore

    std::vector<VkCommandBuffer> cmdBuffers;
    if (bIrradianceMapGenerated)
    {
        cmdBuffers = {// TODO: Disable IBL submitting for every frame
                      // pIBLPass->GetCommandBuffer(),
                      pGBufferPass->GetCommandBuffer(),
                      pFinalPass->GetCommandBuffer(nIamgeIndex),
                      pUIPass->GetCommandBuffer(nIamgeIndex)};
    }
    else
    {
        cmdBuffers = {
            // TODO: Disable IBL submitting for every frame
            pIBLPass->GetCommandBuffer(), pGBufferPass->GetCommandBuffer(),
            pFinalPass->GetCommandBuffer(nIamgeIndex),
            pUIPass->GetCommandBuffer(nIamgeIndex)};
        bIrradianceMapGenerated = true;
    }

    // Filter out null cmd buffers
    std::vector<VkCommandBuffer> vCmdBuffers;
    for (auto& cmdBuf : cmdBuffers)
    {
        if (cmdBuf != VK_NULL_HANDLE)
        {
            vCmdBuffers.push_back(cmdBuf);
        }
    }
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &s_imageAvailableSemaphores[0];
    submitInfo.pWaitDstStageMask = &stageFlag;

    submitInfo.commandBufferCount = static_cast<uint32_t>(vCmdBuffers.size());
    submitInfo.pCommandBuffers = vCmdBuffers.data();

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &s_renderFinishedSemaphores[0];

    assert(vkQueueSubmit(GetRenderDevice()->GetGraphicsQueue(), 1, &submitInfo,
                         s_waitFences[nIamgeIndex]) == VK_SUCCESS);

    // Present swap chain
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &s_renderFinishedSemaphores[0];

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &(s_pSwapchain->getSwapChain());
    presentInfo.pImageIndices = &nIamgeIndex;

    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(GetRenderDevice()->GetPresentQueue(), &presentInfo);
}

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
    initWindow();
    // Create Instace
    std::vector<const char *> extensions = GetRequiredInstanceExtensions();
    GetRenderDevice()->Initialize(extensions);

    // Create swapchain
    s_pSwapchain = new GLFWSwapchain(s_pWindow);
    s_pSwapchain->createSurface();

    GetRenderDevice()->CreateDevice(GetRequiredDeviceExtensions(),  // Extensions
                                    std::vector<const char *>(),    // Layers
                                    s_pSwapchain->getSurface());    // Swapchain surface

    GetMemoryAllocator()->Initalize(GetRenderDevice());
    s_pSwapchain->createSwapchain(
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VK_PRESENT_MODE_FIFO_KHR, NUM_BUFFERS);

    GetRenderDevice()->SetViewportSize(s_pSwapchain->getSwapchainExtent());

    GetRenderDevice()->CreateCommandPools();

    // Initialize managers
    GetDescriptorManager()->createDescriptorPool();
    GetDescriptorManager()->createDescriptorSetLayouts();
    GetSamplerManager()->createSamplers();

    RenderTarget *pDepthResource =
        GetRenderResourceManager()->getDepthTarget(
            "depthTarget",
            VkExtent2D({s_pSwapchain->getSwapchainExtent().width,
                        s_pSwapchain->getSwapchainExtent().height}));

    GetRenderPassManager()->Initialize(s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height, s_pSwapchain->getImageFormat());
    GetRenderPassManager()->SetSwapchainImageViews(s_pSwapchain->getImageViews(), pDepthResource->getView());

    // arrange render passes
    pFinalPass = std::make_unique<RenderPassFinal>(s_pSwapchain->getImageFormat());
    pFinalPass->setSwapchainImageViews(s_pSwapchain->getImageViews(), pDepthResource->getView(), s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height);

    pUIPass = std::make_unique<RenderPassUI>(s_pSwapchain->getImageFormat());
    pUIPass->setSwapchainImageViews(s_pSwapchain->getImageViews(), pDepthResource->getView(), s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height);

    pIBLPass = std::make_unique<RenderLayerIBL>();

    pGBufferPass = std::make_unique<RenderPassGBuffer>();
    pGBufferPass->createGBufferViews(s_pSwapchain->getSwapchainExtent());
    pGBufferPass->createPipelines();

    ImGui::Init(s_pWindow);

    // Create memory allocator

    // A bunch of news and deletes happend in the following block
    // They have to be created and destroyed in a certain order
    // Looking for a way to convert them to smart pointers, otherwise a major
    // refactorying is required.
    {
        // Create the quad
        g_pQuadGeometry = GetQuad();
        GLTFImporter importer;
        g_vScenes = importer.ImportScene("assets/mazda_mx-5/scene.gltf");
        for (const auto &scene : g_vScenes)
        {
            std::cout << scene.ConstructDebugString() << std::endl;
        }

        UniformBuffer<PerViewData> *pUniformBuffer =
            GetRenderResourceManager()->getUniformBuffer<PerViewData>(
                "perView");

        // Load materials
        GetMaterialManager()->CreateDefaultMaterial();

        createCommandBuffers();
        createSemaphores();
        createFences();
        // Mainloop
        while (!glfwWindowShouldClose(s_pWindow))
        {
            glfwPollEvents();

            if (s_resizeWanted)
            {
                s_resizeWanted = false;
            }
            updateUniformBuffer(pUniformBuffer);

            uint32_t nImageIndex = beginFrame();

            pUIPass->newFrame(s_pSwapchain->getSwapchainExtent());
            pUIPass->updateBuffers(nImageIndex);

            ImGui::Render();
            present(nImageIndex);
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
