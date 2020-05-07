#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_beta.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>  // strcmp
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <set>
#include <vector>
#include <cassert> // assert
#include "RenderResourceManager.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "VertexBuffer.h"
#include "Camera.h"
#include "PipelineStateBuilder.h"
#include "MeshVertex.h"
//#include "UIOverlay.h"
#include "VkRenderDevice.h"
#include "VkMemoryAllocator.h"
#include "../thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "RenderPass.h"
#include "PipelineManager.h"
#include "RenderPassGBuffer.h"
#include "GLFWSwapchain.h"
#include "Debug.h"

// TODO: Move them to renderpass manager
std::unique_ptr<RenderPassFinal> pFinalPass = nullptr;
std::unique_ptr<RenderPassGBuffer> pGBufferPass = nullptr;

const int WIDTH = 800;
const int HEIGHT = 600;

thread_local size_t s_currentContext;


static bool s_resizeWanted = true;



static bool s_isValidationEnabled = true;

static GLFWwindow* s_pWindow = nullptr;

PipelineManager gPipelineManager;

static Arcball s_arcball(glm::perspective(glm::radians(80.0f),
                                          (float)WIDTH / (float)HEIGHT, 0.1f,
                                          10.0f),
                         glm::lookAt(glm::vec3(0.0f, 0.0f, -2.0f),   // Eye
                                     glm::vec3(0.0f, 0.0f, 0.0f),    // Center
                                     glm::vec3(0.0f, 1.0f, 0.0f)));  // Up
// GLFW mouse callback
static void mouseCallback(GLFWwindow* window, int button, int action, int mods)
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
static void mouseCursorCallback(GLFWwindow* window, double xpos, double ypos)
{
    s_arcball.updateDrag(glm::vec2(xpos, ypos));
}
// GLFW key callbacks
static void onKeyStroke(GLFWwindow* window, int key, int scancode, int action,
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
}


#ifdef __APPLE__
static const uint32_t NUM_BUFFERS = 2;
#else
static const uint32_t NUM_BUFFERS = 3;
#endif

static DebugUtilsMessenger s_debugMessenger;


GLFWSwapchain* s_pSwapchain = nullptr;

static VertexBuffer* s_pQuadVB = nullptr;
static IndexBuffer* s_pQuadIB = nullptr;

static VertexBuffer* s_pCubeVB = nullptr;
static IndexBuffer* s_pCubeIB = nullptr;
static UniformBuffer<PerViewData>* s_pUniformBuffer = nullptr;
static Texture* s_pTexture = nullptr;
//static std::unique_ptr<UIOverlay> s_UIOverlay= nullptr;

// DescriptorLayout, which is part of the pipeline layout
static VkDescriptorSetLayout s_descriptorSetLayout;
// Pipeline

// Descriptor pool
static VkDescriptorPool s_descriptorPool;

static VkDescriptorSet s_descriptorSet = VK_NULL_HANDLE;

// sync
static std::vector<VkSemaphore> s_imageAvailableSemaphores;
static std::vector<VkSemaphore> s_renderFinishedSemaphores;
static std::vector<VkFence> s_waitFences;

// PHYSICAL Device extensions
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    //"VK_KHR_ray_tracing"
    };

struct TinyObjInfo
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
};

void LoadMesh(const std::string path, TinyObjInfo& objInfo)
{
    std::string err;
    bool ret = tinyobj::LoadObj(&(objInfo.attrib), &(objInfo.shapes),
                                &(objInfo.materials), &err, path.c_str());
    if (!ret)
    {
        std::cerr << err << std::endl;
    }
    else
    {
        std::cout << "Num verts :" << objInfo.attrib.vertices.size() / 3
                  << std::endl;
        std::cout << "Num Normals :" << objInfo.attrib.normals.size() / 3
                  << std::endl;
        std::cout << "Num TexCoords :" << objInfo.attrib.texcoords.size() / 2
                  << std::endl;

        // Print out mesh summery
        for (size_t i = 0; i < objInfo.shapes.size(); i++)
        {
            std::cout << "Shape " << i << ": " << std::endl;
            tinyobj::shape_t& shape = objInfo.shapes[i];
            std::cout << "\t" << shape.mesh.indices.size() << " indices\n";
            std::cout << "\t" << shape.mesh.indices.size() << " indices\n";
            // std::cout<<"\t"<<shape.mesh.vert
        }
    }
}

static TinyObjInfo s_objInfo;
// Hardcoded vertices and indices
std::vector<Vertex> getCubeVertices()
{
    std::vector<Vertex> res;

    assert(s_objInfo.shapes.size() == 1 && "Supports only one obj");

    const auto &vIndices = s_objInfo.shapes[0].mesh.indices;

    size_t numVert = vIndices.size();
    res.reserve(numVert);

    for (const auto& meshIdx : vIndices)
    {
        res.emplace_back(Vertex(
            {{s_objInfo.attrib.vertices[3 * meshIdx.vertex_index],
              s_objInfo.attrib.vertices[3 * meshIdx.vertex_index + 1],
              s_objInfo.attrib.vertices[3 * meshIdx.vertex_index + 2]},
             {s_objInfo.attrib.texcoords[2 * meshIdx.texcoord_index],
              s_objInfo.attrib.texcoords[2 * meshIdx.texcoord_index + 1], 0}}));
    }
    return res;
}

std::vector<uint32_t> getCubeIndices()
{
    std::vector<uint32_t> indices;
    indices.reserve(s_objInfo.shapes[0].mesh.indices.size());

    for (size_t i = 0; i< s_objInfo.shapes[0].mesh.indices.size(); i++)
    {
        indices.push_back(i);
    }
    return indices;
}

std::vector<Vertex> getQuadVertices()
{
    const std::vector<Vertex> vertices = {
        {{-1.0f, -1.0f, 0.0}, {0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0}, {1.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0}, {1.0f, 1.0f, 1.0f}},
        {{-1.0f, 1.0f, 0.0}, {0.0f, 1.0f, 1.0f}}};
    return vertices;
}

std::vector<uint32_t> getQuadIndices()
{
    const std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
    return indices;
}

void recreateSwapChain();  // fwd declaration
static void onWindowResize(GLFWwindow* pWindow, int width, int height)
{
    s_arcball.resize(glm::vec2((float)width, (float)height));
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
    const std::vector<const char*> extensionNames, VkPhysicalDevice phyDevice)
{
    uint32_t extCount;
    vkEnumerateDeviceExtensionProperties(phyDevice, nullptr, &extCount,
                                         nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(phyDevice, nullptr, &extCount,
                                         exts.data());
    std::set<std::string> requiredExts(deviceExtensions.begin(),
                                       deviceExtensions.end());
    for (const auto& ext : exts) requiredExts.erase(ext.extensionName);
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
    std::cout<<"Supported Layers ==== \n";

    for (auto& supportedLayer : supportedLayers)
    {
        std::cout<<supportedLayer.description<<std::endl;
    }
    std::cout<<"Supported Layers ^^^ \n";

    return supportedLayers;
}

bool isLayerSupported(const char* layerName)
{
    std::vector<VkLayerProperties> supportedLayers = getSupportedLayers();
    for (const auto& layer : supportedLayers)
    {
        if (strcmp(layer.layerName, layerName) == 0) return true;
    }
    return false;
}

bool areLayersSupported(const std::vector<const char*> layerNames)
{
    for (const auto& layerName : layerNames)
    {
        if (!isLayerSupported(layerName)) 
        {
            std::cout<<layerName<<std::endl;
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
}

std::vector<const char*> getRequiredExtensions()
{
    // get glfw extensiIons

    std::vector<const char*> extensions;
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;

    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    for (uint32_t i = 0; i < glfwExtensionCount; i++)
        extensions.push_back(glfwExtensions[i]);

    // Add validation layer extensions to required list
    if (s_isValidationEnabled)
        extensions.push_back(getValidationExtensionName());

    return extensions;
}

void createInstance()
{
    if (s_isValidationEnabled && !areLayersSupported(getValidationLayerNames()))
        throw std::runtime_error("Validation layer is not supported");

    std::vector<const char*> extensions = getRequiredExtensions();
    std::cout << "Required extensions" << std::endl;
    for (const auto& ext : extensions) std::cout << ext << std::endl;

    std::cout << "Supported exts" << std::endl;
    std::vector<VkExtensionProperties> supportedExts = getSupportedExtensions();
    for (const auto& ext : supportedExts)
        std::cout << ext.extensionName << std::endl;

    if (s_isValidationEnabled)
    {
        GetRenderDevice()->Initialize(getValidationLayerNames(), extensions);
    }
    else
    {
        std::vector<const char*> dummy;
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
    for (const auto& queueFamily : queueFamilies)
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

        if (indices.isComplete()) break;
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


std::vector<char> m_readSpv(const std::string& fileName)
{
    std::ifstream file(fileName, std::ios::ate | std::ios::binary);
    assert(file.is_open());
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}
VkShaderModule m_createShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shdrModule = VK_NULL_HANDLE;
    assert(vkCreateShaderModule(GetRenderDevice()->GetDevice(), &createInfo, nullptr, &shdrModule) ==
           VK_SUCCESS);
    return shdrModule;
}


void createGraphicsPipeline()
{
    // Create descriptor set layout
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
    descriptorSetLayoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
        PerViewData::getDescriptorSetLayoutBinding(),
        Texture::getSamplerLayoutBinding()};

    descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
    descriptorSetLayoutInfo.pBindings = bindings.data();

    assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                       &descriptorSetLayoutInfo, nullptr,
                                       &s_descriptorSetLayout) == VK_SUCCESS);

    gPipelineManager.CreateStaticObjectPipeline(
        s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height,
        s_descriptorSetLayout, *pFinalPass);

    gPipelineManager.CreateGBufferPipeline(
        s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height,
        s_descriptorSetLayout, *pGBufferPass);
}

void createCommandBuffers()
{

    pGBufferPass->recordCommandBuffer(
        s_pCubeVB->buffer(), s_pCubeIB->buffer(), static_cast<uint32_t>(getCubeIndices().size()),
        gPipelineManager.GetGBufferPipeline(),
        gPipelineManager.GetStaticObjectPipelineLayout(), s_descriptorSet);

    pFinalPass->RecordOnce(
        s_pQuadVB->buffer(), s_pQuadIB->buffer(), static_cast<uint32_t>(getQuadIndices().size()),
        gPipelineManager.GetStaticObjectPipeline(),
        gPipelineManager.GetStaticObjectPipelineLayout(), s_descriptorSet);

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
    for (auto& fence : s_waitFences)
    {
        assert(vkCreateFence(GetRenderDevice()->GetDevice(), &fenceInfo, nullptr, &fence) ==
               VK_SUCCESS);
    }
}

void createDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size());
    assert(vkCreateDescriptorPool(GetRenderDevice()->GetDevice(), &poolInfo, nullptr,
                                  &s_descriptorPool) == VK_SUCCESS);
};

void createDescriptorSet()
{
    // Create descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = s_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &s_descriptorSetLayout;

    assert(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &s_descriptorSet) == VK_SUCCESS);

    // Prepare buffer descriptor
    {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = s_pUniformBuffer->buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(PerViewData);

        // prepare image descriptor
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = s_pTexture->getImageView();
        imageInfo.sampler = s_pTexture->getSamper();

        std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = s_descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = s_descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(),
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
}

void cleanupSwapChain()
{


    gPipelineManager.DestroyStaticObjectPipeline();
    gPipelineManager.DestroyGBufferPipeline();
    
    s_pSwapchain->destroySwapchain();

    vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(), s_descriptorSetLayout, nullptr);
}

void recreateSwapChain()
{
    int width, height;
    glfwGetWindowSize(s_pWindow, &width, &height);
    if (width == 0 || height == 0) return;

    vkDeviceWaitIdle(GetRenderDevice()->GetDevice());

    cleanupSwapChain();

    s_pSwapchain->createSwapchain(
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VK_PRESENT_MODE_FIFO_KHR, NUM_BUFFERS);

    GetRenderResourceManager()->removeResource("depthTarget");

    RenderTarget* pDepthResource = GetRenderResourceManager()->getDepthTarget(
        "depthTarget", VkExtent2D({s_pSwapchain->getSwapchainExtent().width,
                                   s_pSwapchain->getSwapchainExtent().height}));

    pFinalPass->SetSwapchainImageViews(
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
    pGBufferPass = nullptr;
    vkDestroyDescriptorPool(GetRenderDevice()->GetDevice(), s_descriptorPool, nullptr);

    for (auto& somaphore : s_imageAvailableSemaphores)
        vkDestroySemaphore(GetRenderDevice()->GetDevice(), somaphore, nullptr);
    for (auto& somaphore : s_renderFinishedSemaphores)
        vkDestroySemaphore(GetRenderDevice()->GetDevice(), somaphore, nullptr);
    for (auto& fence : s_waitFences) vkDestroyFence(GetRenderDevice()->GetDevice(), fence, nullptr);


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

void present()
{
    uint32_t imageIndex = s_pSwapchain->getNextImage(s_imageAvailableSemaphores[0]);

    s_currentContext = imageIndex;

    vkWaitForFences(GetRenderDevice()->GetDevice(), 1, &s_waitFences[imageIndex], VK_TRUE,
                    std::numeric_limits<uint64_t>::max());
    vkResetFences(GetRenderDevice()->GetDevice(), 1, &s_waitFences[imageIndex]);

    // submit command buffer
    VkPipelineStageFlags stageFlag =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // only color attachment
                                                        // waits for the
                                                        // semaphore

    std::array<VkCommandBuffer, 2> cmdBuffers = 
    {
        pGBufferPass->GetCommandBuffer(),
        pFinalPass->GetCommandBuffer(imageIndex)
    };
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &s_imageAvailableSemaphores[0];
    submitInfo.pWaitDstStageMask = &stageFlag;

    submitInfo.commandBufferCount = static_cast<uint32_t>(cmdBuffers.size());
    submitInfo.pCommandBuffers = cmdBuffers.data();

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &s_renderFinishedSemaphores[0];

    assert(vkQueueSubmit(GetRenderDevice()->GetGraphicsQueue(), 1, &submitInfo,
                         s_waitFences[imageIndex]) == VK_SUCCESS);

    // Present swap chain
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &s_renderFinishedSemaphores[0];

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &(s_pSwapchain->getSwapChain());
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(GetRenderDevice()->GetPresentQueue(), &presentInfo);
}

void updateUniformBuffer(UniformBuffer<PerViewData>* ub)
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
        //"VK_KHR_ray_tracing"
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
        std::vector<const char*> dummy;
        GetRenderDevice()->createLogicalDevice(dummy,
                                               vLogicalDeviceExtensions,
                                               s_pSwapchain->getSurface());
    }

    GetMemoryAllocator()->Initalize(GetRenderDevice());
    s_pSwapchain->createSwapchain(
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VK_PRESENT_MODE_FIFO_KHR, NUM_BUFFERS);

    GetRenderDevice()->createCommandPools();

    RenderTarget* pDepthResource =
        GetRenderResourceManager()->getDepthTarget(
            "depthTarget",
            VkExtent2D({s_pSwapchain->getSwapchainExtent().width,
                        s_pSwapchain->getSwapchainExtent().height}));

    pFinalPass = std::make_unique<RenderPassFinal>(s_pSwapchain->getImageFormat());
    pFinalPass->SetSwapchainImageViews(s_pSwapchain->getImageViews(), pDepthResource->getView(),s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height);

    pGBufferPass = std::make_unique<RenderPassGBuffer>();
    pGBufferPass->createGBufferViews(s_pSwapchain->getSwapchainExtent());

    createGraphicsPipeline();

    // Create memory allocator

    // A bunch of news and deletes happend in the following block
    // They have to be created and destroyed in a certain order
    // Looking for a way to convert them to smart pointers, otherwise a major
    // refactorying is required.
    {

        // Create the cube
        s_pCubeVB = new VertexBuffer();

        LoadMesh("assets/cube.obj", s_objInfo);
        s_pCubeVB->setData(reinterpret_cast<void*>(getCubeVertices().data()),
                                 sizeof(Vertex) * getCubeVertices().size());

        s_pCubeIB = new IndexBuffer();

        s_pCubeIB->setData(reinterpret_cast<void*>(getCubeIndices().data()),
                                sizeof(uint32_t) * getCubeIndices().size());

        // Create the quad
        s_pQuadVB = new VertexBuffer();
        s_pQuadVB->setData(getQuadVertices().data(),
                           sizeof(Vertex) * getQuadVertices().size());

        s_pQuadIB = new IndexBuffer();
        s_pQuadIB->setData(getQuadIndices().data(),
                           sizeof(uint32_t) * getQuadIndices().size());

        s_pUniformBuffer = new UniformBuffer<PerViewData>();

        s_pTexture = new Texture();
        s_pTexture->LoadImage("assets/default.png");
        createDescriptorPool();
        createDescriptorSet();
        createCommandBuffers();
        createSemaphores();
        createFences();
        //s_UIOverlay =
        //    std::make_unique<UIOverlay>(GetRenderDevice()->GetDevice());

        //s_UIOverlay->initialize(dynamic_cast<RenderContext&>(
        //                            *(s_contextManager.getContext(CONTEXT_UI))),
        //                        NUM_BUFFERS,
        //                        GetRenderDevice()->GetPhysicalDevice());
        //s_UIOverlay->initializeFontTexture(
        //    GetRenderDevice()->GetPhysicalDevice(), s_commandPool,
        //    GetRenderDevice()->GetGraphicsQueue());

        //ImGui::GetIO().DisplaySize = ImVec2(s_pSwapchain->getSwapchainExtent().width, s_pSwapchain->getSwapchainExtent().height);

        // Mainloop
        while (!glfwWindowShouldClose(s_pWindow))
        {
            glfwPollEvents();

            if (s_resizeWanted)
            {
                s_resizeWanted = false;
            }
            //ImGui::NewFrame();
            //ImGui::Text("test");
            updateUniformBuffer(s_pUniformBuffer);
            //ImGui::EndFrame();
            // wait on device to make sure it has been drawn
            // assert(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()) == VK_SUCCESS);
            //ImGui::Render();
            //s_UIOverlay->renderDrawData(ImGui::GetDrawData(), *(static_cast<RenderContext*>(s_contextManager.getContext(CONTEXT_UI))), s_commandPool, GetRenderDevice()->GetGraphicsQueue());
            present();
        }
        std::cout << "Closing window, wait for device to finish..."
                  << std::endl;
        assert(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()) == VK_SUCCESS);
        std::cout << "Device finished" << std::endl;

        //s_UIOverlay->finalize();
        //s_UIOverlay = nullptr;
        delete s_pCubeVB;
        delete s_pCubeIB;
        delete s_pQuadVB;
        delete s_pQuadIB;
        delete s_pUniformBuffer;
        delete s_pTexture;
    }
    cleanup();
    return 0;
}
