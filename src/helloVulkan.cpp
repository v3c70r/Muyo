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
#include "PipelineManager.h"
#include "PipelineStateBuilder.h"
#include "RenderPass.h"
#include "RenderPassGBuffer.h"
#include "RenderPassIBL.h"
#include "RenderPassUI.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "VertexBuffer.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

// TODO: Move them to renderpass manager
std::unique_ptr<RenderPassFinal> pFinalPass = nullptr;
std::unique_ptr<RenderPassGBuffer> pGBufferPass = nullptr;
std::unique_ptr<RenderPassUI> pUIPass = nullptr;
std::unique_ptr<RenderPassIBL> pIBLPass = nullptr;

const int WIDTH = 800;
const int HEIGHT = 600;

static bool s_resizeWanted = true;

static bool s_isValidationEnabled = true;

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
// GLFW key callbacks
static void onKeyStroke(GLFWwindow *window, int key, int scancode, int action,
                        int mods)
{
    std::cout << "Key pressed\n";
    // OpenGLRenderer* renderer =
    //    static_cast<OpenGLRenderer*>(glfwGetWindowUserPointer(window));
    // FPScamera& camera = renderer->world_.camera();
    // if (action == GLFW_PRESS) switch (key) {
    //        case GLFW_KEY_ESCAPE:
    //            glfwSetWindowShouldClose(window, GL_TRUE);
    //            break;
    //        case GLFW_KEY_W:
    //            camera.cameraMatrix() = glm::translate(
    //                camera.cameraMatrix(), glm::vec3(0.0, 0.0, -1.0));
    //            break;
    //        case GLFW_KEY_S:
    //            camera.cameraMatrix() = glm::translate(
    //                camera.cameraMatrix(), glm::vec3(0.0, 0.0, 1.0));
    //            break;
    //        case GLFW_KEY_A:
    //            camera.cameraMatrix() = glm::translate(
    //                camera.cameraMatrix(), glm::vec3(-1.0, 0.0, 0.0));
    //            break;
    //        case GLFW_KEY_D:
    //            camera.cameraMatrix() = glm::translate(
    //                camera.cameraMatrix(), glm::vec3(1.0, 0.0, 0.0));
    //            break;
    //        case GLFW_KEY_I:
    //            camera.pitch(0.1);
    //            break;
    //    }
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

static UniformBuffer<PerViewData> *s_pUniformBuffer = nullptr;

// sync
static std::vector<VkSemaphore> s_imageAvailableSemaphores;
static std::vector<VkSemaphore> s_renderFinishedSemaphores;
static std::vector<VkFence> s_waitFences;

// PHYSICAL Device extensions
const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    //"VK_KHR_ray_tracing"
};

static std::unique_ptr<Geometry> s_pQuadGeometry = nullptr;
static std::unique_ptr<Geometry> s_pObjGeometry = nullptr;
///////////////////////////////////////////

void recreateSwapChain(); // fwd declaration
static void onWindowResize(GLFWwindow *pWindow, int width, int height)
{
    s_arcball.resize(glm::vec2((float)width, (float)height));
    GetRenderDevice()->setViewportSize(VkExtent2D(
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}));
    recreateSwapChain();
}

// Query extensions
std::vector<VkExtensionProperties> getSupportedExtensions()
{
    uint32_t extensionCount = 0;
    std::vector<VkExtensionProperties> supportedExtensions;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    supportedExtensions.resize(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                           supportedExtensions.data());
    return supportedExtensions;
}

bool areExtensionsSupportedByPhysicalDevice(
    const std::vector<const char *> extensionNames, VkPhysicalDevice phyDevice)
{
    uint32_t extCount;
    vkEnumerateDeviceExtensionProperties(phyDevice, nullptr, &extCount,
                                         nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(phyDevice, nullptr, &extCount,
                                         exts.data());
    std::set<std::string> requiredExts(deviceExtensions.begin(),
                                       deviceExtensions.end());
    for (const auto &ext : exts)
        requiredExts.erase(ext.extensionName);
    return requiredExts.empty();
}

// Query Layers
std::vector<VkLayerProperties> getSupportedLayers()
{
    uint32_t layerCount = 0;
    std::vector<VkLayerProperties> supportedLayers;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    supportedLayers.resize(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, supportedLayers.data());
    std::cout << "Supported Layers ==== \n";

    for (auto &supportedLayer : supportedLayers)
    {
        std::cout << supportedLayer.description << std::endl;
    }
    std::cout << "Supported Layers ^^^ \n";

    return supportedLayers;
}

bool isLayerSupported(const char *layerName)
{
    std::vector<VkLayerProperties> supportedLayers = getSupportedLayers();
    for (const auto &layer : supportedLayers)
    {
        if (strcmp(layer.layerName, layerName) == 0)
            return true;
    }
    return false;
}

bool areLayersSupported(const std::vector<const char *> layerNames)
{
    for (const auto &layerName : layerNames)
    {
        if (!isLayerSupported(layerName))
        {
            std::cout << layerName << std::endl;
            return false;
        }
    }
    return true;
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
    glfwSetScrollCallback(s_pWindow, ImGui::ScrollCallback);
    glfwSetKeyCallback(s_pWindow, ImGui::KeyCallback);
    glfwSetCharCallback(s_pWindow, ImGui::CharCallback);
}

std::vector<const char *> getRequiredExtensions()
{
    // get glfw extensiIons

    std::vector<const char *> extensions;
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;

    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    for (uint32_t i = 0; i < glfwExtensionCount; i++)
        extensions.push_back(glfwExtensions[i]);

    // Add validation layer extensions to required list
    if (s_isValidationEnabled)
        extensions.push_back(getValidationExtensionName());

    // This exteinsion is required by logical device's multiview extension
    extensions.push_back("VK_KHR_get_physical_device_properties2");

    return extensions;
}

void createInstance()
{
    if (s_isValidationEnabled && !areLayersSupported(getValidationLayerNames()))
        throw std::runtime_error("Validation layer is not supported");

    std::vector<const char *> extensions = getRequiredExtensions();
    std::cout << "Required extensions" << std::endl;
    for (const auto &ext : extensions)
        std::cout << ext << std::endl;

    std::cout << "Supported exts" << std::endl;
    std::vector<VkExtensionProperties> supportedExts = getSupportedExtensions();
    for (const auto &ext : supportedExts)
        std::cout << ext.extensionName << std::endl;

    if (s_isValidationEnabled)
    {
        GetRenderDevice()->Initialize(getValidationLayerNames(), extensions);
    }
    else
    {
        std::vector<const char *> dummy;
        GetRenderDevice()->Initialize(dummy, extensions);
    }

    if (!areLayersSupported(getValidationLayerNames()))
    {
        throw std::runtime_error("Layers are not fully supported");
    }
}
// swap chain details
struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice phyDevice)
{
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phyDevice, s_pSwapchain->getSurface(),
                                              &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice, s_pSwapchain->getSurface(), &formatCount,
                                         nullptr);
    assert(formatCount > 0);
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice, s_pSwapchain->getSurface(), &formatCount,
                                         details.formats.data());

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phyDevice, s_pSwapchain->getSurface(),
                                              &presentCount, nullptr);
    assert(presentCount > 0);
    details.presentModes.resize(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        phyDevice, s_pSwapchain->getSurface(), &presentCount, details.presentModes.data());

    return details;
}

// Hardware devices
struct QueueFamilyIndice
{
    int graphicsFamily = -1;
    int presentFamily = -1;
    bool isComplete() { return graphicsFamily >= 0 && presentFamily >= 0; }
};

QueueFamilyIndice mFindQueueFamily(VkPhysicalDevice device)
{
    QueueFamilyIndice indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                             queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily : queueFamilies)
    {
        // Check for graphics support
        if (queueFamily.queueCount > 0 &&
            queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;

        // Check for presentation support ( they can be in the same queeu
        // family)
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, s_pSwapchain->getSurface(),
                                             &presentSupport);

        if (queueFamily.queueCount > 0 && presentSupport)
            indices.presentFamily = i;

        if (indices.isComplete())
            break;
        i++;
    }
    return indices;
}

bool mIsDeviceSuitable(VkPhysicalDevice device)
{
    // A pysical device has properties and features

    // check for device properties
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    // #todo: add checkers

    // Check for device features
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
    // #todo: add checkers

    // check for extension support
    bool extensionsSupported =
        areExtensionsSupportedByPhysicalDevice(deviceExtensions, device);

    // Check for swap chain adequate
    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport =
            querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() &&
                            !swapChainSupport.presentModes.empty();
    }

    // Check for queue family capabilities
    QueueFamilyIndice indices = mFindQueueFamily(device);

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

void createGraphicsPipeline()
{

    GetPipelineManager()->CreateGBufferPipeline(
        s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height,
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_GBUFFER), pGBufferPass->GetPass());

    GetPipelineManager()->CreateStaticObjectPipeline(
        s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height,
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_LIGHTING), pFinalPass->GetPass());
}

void createCommandBuffers()
{
    const Material* pMaterial = GetMaterialManager()->m_mMaterials["plasticpattern"].get();
    Material::PBRViews views = 
    {
        pMaterial->getImageView(Material::TEX_ALBEDO),
        pMaterial->getImageView(Material::TEX_NORMAL),
        pMaterial->getImageView(Material::TEX_METALNESS),
        pMaterial->getImageView(Material::TEX_ROUGHNESS),
        pMaterial->getImageView(Material::TEX_AO),
    };
    pGBufferPass->recordCommandBuffer(
        s_pObjGeometry->getPrimitives(),
        GetPipelineManager()->GetGBufferPipeline(),
        GetPipelineManager()->GetGBufferPipelineLayout(),
        GetDescriptorManager()->allocateGBufferDescriptorSet(*s_pUniformBuffer,
                                                             views));

    pFinalPass->RecordOnce(
        *s_pQuadGeometry, 
        GetPipelineManager()->GetStaticObjectPipeline(),
        GetPipelineManager()->GetStaticObjectPipelineLayout(),
        GetDescriptorManager()->allocateLightingDescriptorSet(
            *s_pUniformBuffer,
            GetRenderResourceManager()
                ->getColorTarget("GBUFFER_POSITION", VkExtent2D({0, 0}))
                ->getView(),
            GetRenderResourceManager()
                ->getColorTarget("GBUFFER_ALBEDO", VkExtent2D({0, 0}))
                ->getView(),
            GetRenderResourceManager()
                ->getColorTarget("GBUFFER_NORMAL", VkExtent2D({0, 0}))
                ->getView(),
            GetRenderResourceManager()
                ->getColorTarget("GBUFFER_UV", VkExtent2D({0, 0}))
                ->getView()));

    // Load env view
    //pIBLPass->setEnvMap("assets/hdr/Walk_Of_Fame/Mans_Outside_2k.hdr");
    //Texture* pTexture = new Texture;
    //pTexture->LoadImage("assets/hdr/Walk_Of_Fame/Mans_Outside_2k.hdr");
    //pIBLPass->createEquirectangularMapToCubeMapPipeline();
    //pIBLPass->recordEquirectangularMapToCubeMapCmd(pTexture->getImageView());
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

    GetPipelineManager()->DestroyStaticObjectPipeline();
    GetPipelineManager()->DestroyGBufferPipeline();

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

    createGraphicsPipeline();
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

    GetRenderDevice()->destroyCommandPools();
    GetRenderResourceManager()->Unintialize();
    GetMemoryAllocator()->Unintialize();
    s_pSwapchain->destroySurface();
    s_pSwapchain = nullptr;
    delete s_pSwapchain;
    GetRenderDevice()->destroyLogicalDevice();
    if (s_isValidationEnabled)
    {
        s_debugMessenger.uninitialize();
    }
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

void present(uint32_t nIamgeIndex)
{
    // Record command buffers
    pUIPass->recordCommandBuffer(s_pSwapchain->getSwapchainExtent(), nIamgeIndex);

    // submit command buffer
    VkPipelineStageFlags stageFlag =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // only color attachment
                                                       // waits for the
                                                       // semaphore

    std::array<VkCommandBuffer, 4> cmdBuffers = {
        //pIBLPass->GetCommandBuffer(),
        pIBLPass->GetCommandBuffer(),
        pGBufferPass->GetCommandBuffer(),
        pFinalPass->GetCommandBuffer(nIamgeIndex),
        pUIPass->GetCommandBuffer(nIamgeIndex)
    };

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

    ub->setData(ubo);
};

int main()
{
    // Load mesh into memory
    initWindow();
    createInstance();
    s_pSwapchain = new GLFWSwapchain(s_pWindow);
    s_pSwapchain->createSurface();

    std::vector<const char *> vLogicalDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MULTIVIEW_EXTENSION_NAME,
    };
    if (s_isValidationEnabled)
    {
        s_debugMessenger.initialize();
        GetRenderDevice()->createLogicalDevice(getValidationLayerNames(),
                                               vLogicalDeviceExtensions,
                                               s_pSwapchain->getSurface());
    }
    else
    {
        std::vector<const char *> dummy;
        GetRenderDevice()->createLogicalDevice(dummy,
                                               vLogicalDeviceExtensions,
                                               s_pSwapchain->getSurface());
    }

    GetMemoryAllocator()->Initalize(GetRenderDevice());
    s_pSwapchain->createSwapchain(
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VK_PRESENT_MODE_FIFO_KHR, NUM_BUFFERS);

    GetRenderDevice()->setViewportSize(s_pSwapchain->getSwapchainExtent());

    GetRenderDevice()->createCommandPools();
    GetDescriptorManager()->createDescriptorPool();
    GetDescriptorManager()->createDescriptorSetLayouts();
    GetSamplerManager()->createSamplers();

    RenderTarget *pDepthResource =
        GetRenderResourceManager()->getDepthTarget(
            "depthTarget",
            VkExtent2D({s_pSwapchain->getSwapchainExtent().width,
                        s_pSwapchain->getSwapchainExtent().height}));

    pFinalPass = std::make_unique<RenderPassFinal>(s_pSwapchain->getImageFormat());
    pFinalPass->setSwapchainImageViews(s_pSwapchain->getImageViews(), pDepthResource->getView(), s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height);

    pUIPass = std::make_unique<RenderPassUI>(
        s_pSwapchain->getImageFormat(), s_pSwapchain->getImageViews().size());
    pUIPass->setSwapchainImageViews(s_pSwapchain->getImageViews(), pDepthResource->getView(), s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height);

    pIBLPass = std::make_unique<RenderPassIBL>();
    //pIBLPass->initializeIBLResources();


    pGBufferPass = std::make_unique<RenderPassGBuffer>();
    pGBufferPass->createGBufferViews(s_pSwapchain->getSwapchainExtent());


    createGraphicsPipeline();

    ImGui::Init(s_pWindow);

    // Create memory allocator

    // A bunch of news and deletes happend in the following block
    // They have to be created and destroyed in a certain order
    // Looking for a way to convert them to smart pointers, otherwise a major
    // refactorying is required.
    {
        // Create the quad
        s_pQuadGeometry = getQuad();
        //s_pObjGeometry = loadObj("assets/sphere.obj", glm::scale(glm::vec3(0.05)));
        s_pObjGeometry = loadObj("assets/cube.obj");

        s_pUniformBuffer = new UniformBuffer<PerViewData>();

        // Load materials
        GetMaterialManager()->m_mMaterials["plasticpattern"] = std::make_unique<Material>();
        GetMaterialManager()->m_mMaterials["plasticpattern"]->loadTexture(
            Material::TEX_ALBEDO,
            "assets/Materials/plasticpattern1-ue/"
            "plasticpattern1-albedo.png");

        GetMaterialManager()->m_mMaterials["plasticpattern"]->loadTexture(
            Material::TEX_METALNESS,
            "assets/Materials/plasticpattern1-ue/"
            "plasticpattern1-metalness.png");

        GetMaterialManager()->m_mMaterials["plasticpattern"]->loadTexture(
            Material::TEX_NORMAL,
            "assets/Materials/plasticpattern1-ue/"
            "plasticpattern1-normal2b.png");

        GetMaterialManager()->m_mMaterials["plasticpattern"]->loadTexture(
            Material::TEX_ROUGHNESS,
            "assets/Materials/plasticpattern1-ue/"
            "plasticpattern1-roughness2.png");

        GetMaterialManager()->m_mMaterials["plasticpattern"]->loadTexture(
            Material::TEX_AO,
            "assets/Materials/white5x5.png");

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
            updateUniformBuffer(s_pUniformBuffer);

            uint32_t nImageIndex = beginFrame();
            pUIPass->newFrame(s_pSwapchain->getSwapchainExtent());
            pUIPass->updateBuffers(nImageIndex);
            present(nImageIndex);
            ImGui::UpdateMousePosAndButtons();
            ImGui::UpdateMouseCursor();
            ImGui::UpdateGamepads();
        }
        std::cout << "Closing window, wait for device to finish..."
                  << std::endl;
        assert(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()) == VK_SUCCESS);
        std::cout << "Device finished" << std::endl;

        delete s_pUniformBuffer;
        GetMaterialManager()->destroyMaterials();
        s_pObjGeometry = nullptr;
        s_pQuadGeometry = nullptr;
        GetSamplerManager()->destroySamplers();
    }
    ImGui::Shutdown();
    cleanup();
    return 0;
}
