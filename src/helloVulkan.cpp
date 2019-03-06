#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
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

#include "ContextManager.h"
#include "DepthResource.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "VertexBuffer.h"
#include "Camera.h"
#include "RenderContext.h"

#include "../thirdparty/tiny_obj_loader.h"
#include "../thirdparty/imgui/imgui.h"
#include "../thirdparty/imgui/imgui_impl_vulkan.h"

const int WIDTH = 800;
const int HEIGHT = 600;

static VkInstance s_instance;
thread_local size_t s_currentContext;


static ContextManager s_contextManager;
static bool s_resizeWanted = true;

static std::vector<const char*> s_validationLayers{
    "VK_LAYER_LUNARG_standard_validation"};

static bool s_isValidationEnabled = true;

static GLFWwindow* s_pWindow = nullptr;

static Arcball s_arcball(glm::perspective(glm::radians(80.0f),
                                          (float)WIDTH / (float)HEIGHT, 0.1f,
                                          10.0f),
                         glm::lookAt(glm::vec3(0.0f, 0.0f, -2.0f),   // Eye
                                     glm::vec3(0.0f, 0.0f, 0.0f),    // Center
                                     glm::vec3(0.0f, 1.0f, 0.0f)));  // Up
// GLFW mouse callback
static void mouseCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
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
static void mouseCursorCallback( GLFWwindow * window, double xpos, double ypos)  
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

static uint32_t s_numBuffers = 2;
static VkDebugReportCallbackEXT s_debugCallback;

static VkPhysicalDevice s_physicalDevice = VK_NULL_HANDLE;

static VkDevice s_device;

static VkQueue s_graphicsQueue;
static VkQueue s_presentQueue;

static VkSurfaceKHR s_surface;

static VkSwapchainKHR s_swapChain = VK_NULL_HANDLE;
static VkFormat s_swapChainImageFormat;
static VkExtent2D s_swapChainExtent;

static std::vector<VkImage> s_swapChainImages;
static std::vector<VkImageView> s_swapChainImageViews;
static std::vector<VkFramebuffer> s_swapChainFramebuffers;

static VertexBuffer* s_pVertexBuffer = nullptr;
static IndexBuffer* s_pIndexBuffer = nullptr;
static UniformBuffer* s_pUniformBuffer = nullptr;
static Texture* s_pTexture = nullptr;
static DepthResource* s_pDepthResource = nullptr;

VkRenderPass s_renderPass;

// DescriptorLayout, which is part of the pipeline layout
static VkDescriptorSetLayout s_descriptorSetLayout;
// Pipeline
static VkPipelineLayout s_pipelineLayout;

// Descriptor pool
static VkDescriptorPool s_descriptorPool;

static VkDescriptorSet s_descriptorSet;

static VkPipeline s_graphicsPipeline;

// Command
static VkCommandPool s_commandPool;
//static std::vector<VkCommandBuffer> s_commandBuffers;

// sync
static std::vector<VkSemaphore> s_imageAvailableSemaphores;
static std::vector<VkSemaphore> s_renderFinishedSemaphores;
static std::vector<VkFence> s_waitFences;

// PHYSICAL Device extensions
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

struct TinyObjInfo {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
};

void LoadMesh(const std::string path, TinyObjInfo& objInfo)
{
    std::string err;
    bool ret = tinyobj::LoadObj(&(objInfo.attrib), &(objInfo.shapes),
                                &(objInfo.materials), &err, path.c_str());
    if (!ret) {
        std::cerr << err << std::endl;
    }
    else {
        std::cout << "Num verts :" << objInfo.attrib.vertices.size() / 3
                  << std::endl;
        std::cout << "Num Normals :" << objInfo.attrib.normals.size() / 3
                  << std::endl;
        std::cout << "Num TexCoords :" << objInfo.attrib.texcoords.size() / 2
                  << std::endl;

        // Print out mesh summery
        for (size_t i = 0; i < objInfo.shapes.size(); i++) {
            std::cout << "Shape " << i << ": " << std::endl;
            tinyobj::shape_t& shape = objInfo.shapes[i];
            std::cout << "\t" << shape.mesh.indices.size() << " indices\n";
            // std::cout<<"\t"<<shape.mesh.vert
        }
    }
}

static TinyObjInfo s_objInfo;
// Hardcoded vertices and indices
std::vector<Vertex> getVertices()
{
    std::vector<Vertex> res;
    size_t numVert = s_objInfo.attrib.vertices.size() / 3;
    res.reserve(numVert);
    for (size_t i = 0; i < numVert; i++) {
        res.emplace_back(Vertex({{s_objInfo.attrib.vertices[3 * i],
                                  s_objInfo.attrib.vertices[3 * i + 1],
                                  s_objInfo.attrib.vertices[3 * i + 2]},
                                 {0.0, 0.0, 0.0}}));
    }
    for (const auto& index : s_objInfo.shapes[0].mesh.indices) {
        res[index.vertex_index].textureCoord = glm::vec3(
            s_objInfo.attrib.texcoords[2 * index.texcoord_index],
            1.0 - s_objInfo.attrib.texcoords[2 * index.texcoord_index + 1],
            0.0);
    }
    return res;
}

std::vector<uint32_t> getIndices()
{
    // std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0, 4,
    //                                 5, 6, 6, 7, 4

    //};
    // return indices;
    std::vector<uint32_t> indices;
    indices.reserve(s_objInfo.shapes[0].mesh.indices.size());
    for (const auto& index : s_objInfo.shapes[0].mesh.indices)
        indices.push_back(index.vertex_index);
    return indices;
}

// Callback function
static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
              uint64_t obj, size_t location, int32_t code,
              const char* layerPrefix, const char* msg, void* userData)
{
    switch (flags) {
        case VK_DEBUG_REPORT_INFORMATION_BIT_EXT:
            std::cerr << "[INFO] :";
            break;
        case VK_DEBUG_REPORT_WARNING_BIT_EXT:
            std::cerr << "*[WARNING] :";
            break;
        case VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT:
            std::cerr << "[PERF] :";
            break;
        case VK_DEBUG_REPORT_ERROR_BIT_EXT:
            std::cerr << "**[ERROR] :";
            break;
        case VK_DEBUG_REPORT_DEBUG_BIT_EXT:
            std::cerr << "[DEBUG] :";
            break;
        default:
            std::cerr << "[UNKNOWN]";
            break;
    }
    std::cerr << msg << std::endl;

    return VK_FALSE;
}

void recreateSwapChain();  // fwd declaration
static void onWindowResize(GLFWwindow* pWindow, int width, int height)
{
    s_arcball.resize(glm::vec2((float)width, (float)height));
    recreateSwapChain();
}

// Call add callback by query the extension
VkResult createDebugReportCallbackEXT(
    VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugReportCallbackEXT* pCallback)
{
    auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugReportCallbackEXT");
    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pCallback);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugReportCallbackEXT(VkInstance instance,
                                   VkDebugReportCallbackEXT callback,
                                   const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugReportCallbackEXT");
    if (func != nullptr) {
        func(instance, callback, pAllocator);
    }
}

void setupDebugCallback()
{
    if (!s_isValidationEnabled) return;
    VkDebugReportCallbackCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    // createInfo.flags =
    //    VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT
    //    | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
    //    VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
    createInfo.flags = VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                       VK_DEBUG_REPORT_ERROR_BIT_EXT |
                       VK_DEBUG_REPORT_WARNING_BIT_EXT;
    createInfo.pfnCallback = debugCallback;

    assert(createDebugReportCallbackEXT(s_instance, &createInfo, nullptr,
                                        &s_debugCallback) == VK_SUCCESS);
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
    return supportedLayers;
}

bool isLayerSupported(const char* layerName)
{
    std::vector<VkLayerProperties> supportedLayers = getSupportedLayers();
    for (const auto& layer : supportedLayers) {
        if (strcmp(layer.layerName, layerName) == 0) return true;
    }
    return false;
}

bool areLayersSupported(const std::vector<const char*> layerNames)
{
    for (const auto& layerName : layerNames)
        if (!isLayerSupported(layerName)) return false;
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
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

    return extensions;
}

void createInstance()
{
    if (s_isValidationEnabled && !areLayersSupported(s_validationLayers))
        throw std::runtime_error("Validation layer is not supported");
    // Populate application info structure
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    std::vector<const char*> extensions = getRequiredExtensions();
    std::cout << "Required extensions" << std::endl;
    for (const auto& ext : extensions) std::cout << ext << std::endl;

    std::cout << "Supported exts" << std::endl;
    std::vector<VkExtensionProperties> supportedExts = getSupportedExtensions();
    for (const auto& ext : supportedExts)
        std::cout << ext.extensionName << std::endl;

    createInfo.enabledExtensionCount = extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (s_isValidationEnabled) {
        createInfo.enabledLayerCount = s_validationLayers.size();
        createInfo.ppEnabledLayerNames = s_validationLayers.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }

    if (!areLayersSupported(s_validationLayers)) {
        throw std::runtime_error("Layers are not fully supported");
    }

    VkResult res = vkCreateInstance(&createInfo, nullptr, &s_instance);
    if (res != VK_SUCCESS) {
        std::cerr << res << std::endl;
        throw std::runtime_error("failed inst");
    }
}
// swap chain details
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Chose one of the formats and presentModes
VkSurfaceFormatKHR chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> formats)
{
    // #todo: Manually choose the format we want
    return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
}

VkPresentModeKHR chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> presentModes)
{
    // #todo: Manually choose the presentMode we want
    return VK_PRESENT_MODE_FIFO_KHR;
    // return VK_PRESENT_MODE_MAILBOX_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    // Choose swapchain resolution
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    else {
        VkExtent2D actualExtent = {WIDTH, HEIGHT};

        actualExtent.width = std::max(
            capabilities.minImageExtent.width,
            std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(
            capabilities.minImageExtent.height,
            std::min(capabilities.maxImageExtent.height, actualExtent.height));

        return actualExtent;
    }
}

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice phyDevice)
{
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phyDevice, s_surface,
                                              &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice, s_surface, &formatCount,
                                         nullptr);
    assert(formatCount > 0);
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice, s_surface, &formatCount,
                                         details.formats.data());

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phyDevice, s_surface,
                                              &presentCount, nullptr);
    assert(presentCount > 0);
    details.presentModes.resize(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        phyDevice, s_surface, &presentCount, details.presentModes.data());

    return details;
}

// Hardware devices

struct QueueFamilyIndice {
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
    for (const auto& queueFamily : queueFamilies) {
        // Check for graphics support
        if (queueFamily.queueCount > 0 &&
            queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;

        // Check for presentation support ( they can be in the same queeu
        // family)
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, s_surface,
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
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport =
            querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() &&
                            !swapChainSupport.presentModes.empty();
    }

    // Check for queue family capabilities
    QueueFamilyIndice indices = mFindQueueFamily(device);

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

void pickPysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(s_instance, &deviceCount, nullptr);
    assert(deviceCount != 0);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(s_instance, &deviceCount, devices.data());
    for (const auto& device : devices)
        if (mIsDeviceSuitable(device)) {
            s_physicalDevice = device;
            break;
        }
    assert(s_physicalDevice != VK_NULL_HANDLE);
}

// Logical device

void createLogicalDevice()
{
    // INFO: A queue is bound to logical device
    QueueFamilyIndice indices = mFindQueueFamily(s_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<int> uniqueQueueFamilise = {indices.graphicsFamily,
                                         indices.presentFamily};

    float queuePriority = 1.0f;
    // Create a queue for each of the family
    for (int queueFamily : uniqueQueueFamilise) {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        // Queue are stored in the orders
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures = {};

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount =
        static_cast<uint32_t>(queueCreateInfos.size());

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Enable validation layer on device
    if (s_isValidationEnabled) {
        createInfo.enabledLayerCount =
            static_cast<uint32_t>(s_validationLayers.size());
        createInfo.ppEnabledLayerNames = s_validationLayers.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }

    assert(vkCreateDevice(s_physicalDevice, &createInfo, nullptr, &s_device) ==
           VK_SUCCESS);

    vkGetDeviceQueue(s_device, indices.graphicsFamily, 0, &s_graphicsQueue);
    vkGetDeviceQueue(s_device, indices.presentFamily, 0, &s_presentQueue);

    assert(
        s_graphicsQueue ==
        s_presentQueue);  // Graphics and present queues should be the same one

    // Create a presentation queue
}

void createSurface()
{
    // Create a platform specific window surface to present rendered image
    // The platform specific code has been handled by glfw
    assert(glfwCreateWindowSurface(s_instance, s_pWindow, nullptr,
                                   &s_surface) == VK_SUCCESS);
}

void createSwapChain()
{
    SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(s_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat =
        chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode =
        chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    s_swapChainImageFormat = surfaceFormat.format;
    s_swapChainExtent = extent;


    //uint32_t s_numBuffers = swapChainSupport.capabilities.minImageCount + 1;
    //if (swapChainSupport.capabilities.maxImageCount > 0 &&
    //    s_numBuffers > swapChainSupport.capabilities.maxImageCount) {
    //    s_numBuffers = swapChainSupport.capabilities.maxImageCount;
    //}

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = s_surface;
    createInfo.minImageCount = s_numBuffers;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndice indices = mFindQueueFamily(s_physicalDevice);

    // Assume present and graphics queues are in the same family
    // see
    // https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain
    assert(indices.graphicsFamily == indices.presentFamily);
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    // Blending with other windows
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    createInfo.presentMode = presentMode;
    // Don't care about the window above us
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    assert(vkCreateSwapchainKHR(s_device, &createInfo, nullptr, &s_swapChain) ==
           VK_SUCCESS);

    // Get swap chain images;
    vkGetSwapchainImagesKHR(s_device, s_swapChain, &s_numBuffers, nullptr);
    s_swapChainImages.resize(s_numBuffers);
    vkGetSwapchainImagesKHR(s_device, s_swapChain, &s_numBuffers,
                            s_swapChainImages.data());

}

// Create image views for images on the swap chain
void createImageViews()
{
    s_swapChainImageViews.resize(s_swapChainImages.size());
    for (size_t i = 0; i < s_swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = s_swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = s_swapChainImageFormat;

        // Swizzles

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // subresource
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        assert(vkCreateImageView(s_device, &createInfo, nullptr,
                                 &s_swapChainImageViews[i]) == VK_SUCCESS);
    }
}

// Create Pipeline

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
    VkShaderModule shdrModule;
    assert(vkCreateShaderModule(s_device, &createInfo, nullptr, &shdrModule) ==
           VK_SUCCESS);
    return shdrModule;
}

void createRenderPass()
{
    // Attachments
    //
    // Color Attachements
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = s_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear on load
    colorAttachment.storeOp =
        VK_ATTACHMENT_STORE_OP_STORE;  // Store in the memory to read back later

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachments
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // attachment reference
    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment =
        0;  // indicates the attachemnt index in the attachments array
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;  // the index is the output
                                                      // from the fragment
                                                      // shader
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // subpass deps
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDep.dstSubpass = 0;
    subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.srcAccessMask = 0;
    subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Render pass
    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
                                                          depthAttachment};
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    assert(vkCreateRenderPass(s_device, &renderPassInfo, nullptr,
                              &s_renderPass) == VK_SUCCESS);
}

void createGraphicsPipeline()
{
    // 1. Shaders
    VkShaderModule vertShdr = m_createShaderModule(m_readSpv("shaders/triangle.vert.spv"));
    VkShaderModule fragShdr = m_createShaderModule(m_readSpv("shaders/triangle.frag.spv"));

    // Create shader stages

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShdr;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShdr;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                      fragShaderStageInfo};

    // 2. Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    VkVertexInputBindingDescription bindingDesc =
        Vertex::getBindingDescription();
    std::array<VkVertexInputAttributeDescription, 2> attribDescs =
        Vertex::getAttributeDescriptions();
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attribDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attribDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
    inputAssemblyInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

    // 3. Viewports and scissors
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)s_swapChainExtent.width;
    viewport.height = (float)s_swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = s_swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 4. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizerInfo = {};
    rasterizerInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerInfo.depthClampEnable = VK_FALSE;
    rasterizerInfo.rasterizerDiscardEnable =
        VK_FALSE;  // Discard any output to framebuffer
    rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizerInfo.lineWidth = 1.0f;
    rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;

    // Depth bias, disabled for now
    rasterizerInfo.depthBiasEnable = VK_FALSE;
    rasterizerInfo.depthBiasConstantFactor = 0.0f;  // Optional
    rasterizerInfo.depthBiasClamp = 0.0f;           // Optional
    rasterizerInfo.depthBiasSlopeFactor = 0.0f;     // Optional

    // 5. Multisampling
    VkPipelineMultisampleStateCreateInfo multisamplingInfo = {};
    multisamplingInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisamplingInfo.sampleShadingEnable = VK_FALSE;
    multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisamplingInfo.minSampleShading = 1.0f;
    multisamplingInfo.pSampleMask = nullptr;
    multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
    multisamplingInfo.alphaToOneEnable = VK_FALSE;

    // 6. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ZERO;                                        // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ZERO;                             // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;  // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;  // Optional
    colorBlending.blendConstants[1] = 0.0f;  // Optional
    colorBlending.blendConstants[2] = 0.0f;  // Optional
    colorBlending.blendConstants[3] = 0.0f;  // Optional

    // 7. Dynamic state
    // VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
    //                                  VK_DYNAMIC_STATE_LINE_WIDTH};
    // VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
    // dynamicStateInfo.sType =
    //    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    // dynamicStateInfo.dynamicStateCount = 2;
    // dynamicStateInfo.pDynamicStates = dynamicStates;

    // 7.5. DescriptorSetLayout
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
    descriptorSetLayoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
        UnifromBufferObject::getDescriptorSetLayoutBinding(),
        Texture::getSamplerLayoutBinding()};
    descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
    descriptorSetLayoutInfo.pBindings = bindings.data();

    assert(vkCreateDescriptorSetLayout(s_device, &descriptorSetLayoutInfo,
                                       nullptr,
                                       &s_descriptorSetLayout) == VK_SUCCESS);

    // 8. Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &s_descriptorSetLayout;
    // pipelineLayoutInfo.pushConstantRangeCount = 0;
    // pipelineLayoutInfo.pPushConstantRanges = 0;
    assert(vkCreatePipelineLayout(s_device, &pipelineLayoutInfo, nullptr,
                                  &s_pipelineLayout) == VK_SUCCESS);

    // 9. Dpeth stencile state create info
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    // depth clamp?
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;

    pipelineInfo.pViewportState = &viewportState;

    pipelineInfo.pRasterizationState = &rasterizerInfo;

    pipelineInfo.pMultisampleState = &multisamplingInfo;

    pipelineInfo.pDepthStencilState = &depthStencil;

    pipelineInfo.pColorBlendState = &colorBlending;

    pipelineInfo.pDynamicState = nullptr;

    pipelineInfo.layout = s_pipelineLayout;

    pipelineInfo.renderPass = s_renderPass;
    pipelineInfo.subpass = 0;

    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    assert(vkCreateGraphicsPipelines(s_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                     nullptr,
                                     &s_graphicsPipeline) == VK_SUCCESS);

    vkDestroyShaderModule(s_device, vertShdr, nullptr);
    vkDestroyShaderModule(s_device, fragShdr, nullptr);
}

void createFramebuffers()
{
    s_swapChainFramebuffers.resize(s_swapChainImageViews.size());
    for (size_t i = 0; i < s_swapChainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachmentViews = {
            s_swapChainImageViews[i], s_pDepthResource->getView()};

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = s_renderPass;
        framebufferInfo.attachmentCount =
            static_cast<uint32_t>(attachmentViews.size());
        framebufferInfo.pAttachments = attachmentViews.data();
        framebufferInfo.width = s_swapChainExtent.width;
        framebufferInfo.height = s_swapChainExtent.height;
        framebufferInfo.layers = 1;
        assert(vkCreateFramebuffer(s_device, &framebufferInfo, nullptr,
                                   &s_swapChainFramebuffers[i]) == VK_SUCCESS);
    }
}

void createCommandPool()
{
    QueueFamilyIndice queueFamilyIndice = mFindQueueFamily(s_physicalDevice);

    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.queueFamilyIndex = queueFamilyIndice.graphicsFamily;
    commandPoolInfo.flags = 0;
    assert(vkCreateCommandPool(s_device, &commandPoolInfo, nullptr,
                               &s_commandPool) == VK_SUCCESS);
}

void createCommandBuffers(const VertexBuffer& vertexBuffer,
                          const IndexBuffer& indexBuffer)
{
    s_contextManager.Initalize();
    s_contextManager.getContext(CONTEXT_SCENE)->initialize(s_numBuffers, &s_device, &s_commandPool);
    s_contextManager.getContext(CONTEXT_UI)->initialize(s_numBuffers, &s_device, &s_commandPool);

    // Record command buffer to draw static objects
    for (s_currentContext = 0; s_currentContext < s_numBuffers;
         s_currentContext++) {
        RenderContext* renderContext = static_cast<RenderContext*>(s_contextManager.getContext(CONTEXT_SCENE));
        VkCommandBuffer& currentCmdBuffer = renderContext->getCommandBuffer();
        renderContext->startRecording();
        renderContext->beginPass(
            s_renderPass, s_swapChainFramebuffers[s_currentContext],
            s_swapChainExtent);

        VkBuffer vb = vertexBuffer.buffer();
        VkBuffer ib = indexBuffer.buffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(currentCmdBuffer, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(currentCmdBuffer, ib, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(currentCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          s_graphicsPipeline);
        vkCmdBindDescriptorSets(
            currentCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            s_pipelineLayout, 0, 1, &s_descriptorSet, 0, nullptr);

        // vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);
        vkCmdDrawIndexed(currentCmdBuffer,
                         static_cast<uint32_t>(getIndices().size()), 1, 0, 0,
                         0);

        renderContext->endPass();
        renderContext->endRecording();
    }
}

void createSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    s_imageAvailableSemaphores.resize(s_numBuffers);
    s_renderFinishedSemaphores.resize(s_numBuffers);
    for (size_t i = 0; i < s_numBuffers; i++) {
        assert(vkCreateSemaphore(s_device, &semaphoreInfo, nullptr,
                                 &s_imageAvailableSemaphores[i]) == VK_SUCCESS);
        assert(vkCreateSemaphore(s_device, &semaphoreInfo, nullptr,
                                 &s_renderFinishedSemaphores[i]) == VK_SUCCESS);
    }
}

void createFences()
{
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    s_waitFences.resize(s_numBuffers);
    for (auto& fence : s_waitFences) {
        assert(vkCreateFence(s_device, &fenceInfo, nullptr, &fence) ==
               VK_SUCCESS);
    }
}

void createDescriptorPool()
{
    VkDescriptorPoolSize poolSizes[] = {
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
    poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
    assert(vkCreateDescriptorPool(s_device, &poolInfo, nullptr,
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
    assert(vkAllocateDescriptorSets(s_device, &allocInfo, &s_descriptorSet) ==
           VK_SUCCESS);

    // Prepare buffer descriptor
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = s_pUniformBuffer->buffer();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBuffer);

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

    vkUpdateDescriptorSets(s_device,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
}

void cleanupSwapChain()
{
    for (auto& framebuffer : s_swapChainFramebuffers)
        vkDestroyFramebuffer(s_device, framebuffer, nullptr);

    s_contextManager.getContext(CONTEXT_SCENE)->finalize();
    s_contextManager.getContext(CONTEXT_UI)->finalize();

    vkDestroyPipeline(s_device, s_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(s_device, s_pipelineLayout, nullptr);
    vkDestroyRenderPass(s_device, s_renderPass, nullptr);
    for (auto& imageView : s_swapChainImageViews)
        vkDestroyImageView(s_device, imageView, nullptr);
    vkDestroySwapchainKHR(s_device, s_swapChain, nullptr);
    vkDestroyDescriptorSetLayout(s_device, s_descriptorSetLayout, nullptr);
}

void recreateSwapChain()
{
    int width, height;
    glfwGetWindowSize(s_pWindow, &width, &height);
    if (width == 0 || height == 0) return;

    vkDeviceWaitIdle(s_device);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();

    // Need to recreate depth resource before recreating framebuffer
    delete s_pDepthResource;
    s_pDepthResource = new DepthResource(
        s_device, s_physicalDevice, s_commandPool, s_graphicsQueue,
        s_swapChainExtent.width, s_swapChainExtent.height);

    createFramebuffers();
    createCommandBuffers(*s_pVertexBuffer, *s_pIndexBuffer);
}

void initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplVulkan_InitInfo info;
    memset(&info, 0, sizeof(ImGui_ImplVulkan_InitInfo));
    info.Instance = s_instance;
    info.PhysicalDevice = s_physicalDevice;
    info.Device = s_device;;
    info.QueueFamily = 0;
    info.Queue = s_graphicsQueue;
    info.PipelineCache = 0;
    info.DescriptorPool = s_descriptorPool;
    info.Allocator = nullptr;
    info.CheckVkResultFn = nullptr;

    ImGui_ImplVulkan_Init(&info, s_renderPass);

    // Upload font
    VkCommandBuffer cmdBuf = beginSingleTimeCommands(s_device, s_commandPool);
    ImGui_ImplVulkan_CreateFontsTexture(cmdBuf);
    endSingleTimeCommands(cmdBuf, s_device, s_commandPool, s_graphicsQueue);
    VkResult err = vkDeviceWaitIdle(s_device);
    assert(err == VK_SUCCESS);
    ImGui_ImplVulkan_InvalidateFontUploadObjects();

}

void cleanup()
{
    cleanupSwapChain();
    vkDestroyDescriptorPool(s_device, s_descriptorPool, nullptr);

    for (auto& somaphore : s_imageAvailableSemaphores)
        vkDestroySemaphore(s_device, somaphore, nullptr);
    for (auto& somaphore : s_renderFinishedSemaphores)
        vkDestroySemaphore(s_device, somaphore, nullptr);
    for (auto& fence : s_waitFences) vkDestroyFence(s_device, fence, nullptr);

    ImGui::DestroyContext();
    ImGui_ImplVulkan_Shutdown();

    vkDestroyCommandPool(s_device, s_commandPool, nullptr);
    vkDestroySurfaceKHR(s_instance, s_surface, nullptr);
    vkDestroyDevice(s_device, nullptr);
    DestroyDebugReportCallbackEXT(s_instance, s_debugCallback, nullptr);
    vkDestroyInstance(s_instance, nullptr);
    glfwDestroyWindow(s_pWindow);
    glfwTerminate();
}

void present()
{
    uint32_t imageIndex;
    vkAcquireNextImageKHR(
        s_device, s_swapChain, std::numeric_limits<uint64_t>::max(),
        s_imageAvailableSemaphores[0], VK_NULL_HANDLE, &imageIndex);

    s_currentContext = imageIndex;

    vkWaitForFences(s_device, 1, &s_waitFences[imageIndex], VK_TRUE,
                    std::numeric_limits<uint64_t>::max());
    vkResetFences(s_device, 1, &s_waitFences[imageIndex]);

    // submit command buffer
    VkPipelineStageFlags stageFlag =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // only color attachment
                                                        // waits for the
                                                        // semaphore
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &s_imageAvailableSemaphores[0];
    submitInfo.pWaitDstStageMask = &stageFlag;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &s_contextManager.getContext(CONTEXT_SCENE)->getCommandBuffer();

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &s_renderFinishedSemaphores[0];

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), s_contextManager.getContext(CONTEXT_UI)->getCommandBuffer());

    assert(vkQueueSubmit(s_graphicsQueue, 1, &submitInfo,
                         s_waitFences[imageIndex]) == VK_SUCCESS);

    // Present swap chain
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &s_renderFinishedSemaphores[0];

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &s_swapChain;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(s_presentQueue, &presentInfo);
}

void updateUniformBuffer(UniformBuffer* ub)
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(
                     currentTime - startTime)
                     .count() * 0.01;
    UnifromBufferObject ubo = {};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(10.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    //ubo.model = glm::rotate(glm::mat4(1.0), glm::degrees(time), glm::vec3(0.0, 1.0, 0.0));
    ubo.model = glm::mat4(1.0);

    ubo.view = s_arcball.getViewMat();
    ubo.proj = s_arcball.getProjMat();

    ub->setData(ubo);
};

int main()
{
    // Load mesh into memory
    LoadMesh("assets/chalet.obj", s_objInfo);
    initWindow();
    createInstance();
    setupDebugCallback();
    createSurface();  // This needed to be called before mFindQueueFamily() as
                      // it needs s_surface
    pickPysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();

    createRenderPass();
    createGraphicsPipeline();
    createCommandPool();


    // A bunch of news and deletes happend in the following block
    // They have to be created and destroyed in a certain order
    // Looking for a way to convert them to smart pointers, otherwise a major refactorying
    // is required.
    {
        s_pDepthResource = new DepthResource(
            s_device, s_physicalDevice, s_commandPool, s_graphicsQueue,
            s_swapChainExtent.width, s_swapChainExtent.height);

        createFramebuffers();

        s_pVertexBuffer = new VertexBuffer(
            s_device, s_physicalDevice, sizeof(Vertex) * getVertices().size());

        s_pVertexBuffer->setData(getVertices(), s_commandPool, s_graphicsQueue);

        s_pIndexBuffer = new IndexBuffer(
            s_device, s_physicalDevice, sizeof(uint32_t) * getIndices().size());

        s_pIndexBuffer->setData(getIndices(), s_commandPool, s_graphicsQueue);

        s_pUniformBuffer = new UniformBuffer(s_device, s_physicalDevice,
                                             sizeof(UnifromBufferObject));

        s_pTexture = new Texture();
        s_pTexture->LoadImage("assets/chalet.jpg", s_device, s_physicalDevice,
                              s_commandPool, s_graphicsQueue);
        createDescriptorPool();
        createDescriptorSet();
        createCommandBuffers(*s_pVertexBuffer, *s_pIndexBuffer);
        createSemaphores();
        createFences();
        initImGui();

        // Mainloop
        while (!glfwWindowShouldClose(s_pWindow)) {
            glfwPollEvents();

            // TODO: Do we need multiple swapchains to render the GUI
            if (s_resizeWanted)
            {
                //ImGui_ImplVulkanH_CreateWindowDataSwapChainAndFramebuffer(s_physicalDevice, s_device, s_pWindow, nullptr, s_swapChainExtent.width, s_swapChainExtent.height);
                s_resizeWanted = false;
            }
            ImGui_ImplVulkan_NewFrame();
//            ImGui::NewFrame();
//            ImGui::Text("Hello");
//
            updateUniformBuffer(s_pUniformBuffer);
            // wait on device to make sure it has been drawn
            // assert(vkDeviceWaitIdle(s_device) == VK_SUCCESS);
            present();
        }
        std::cout << "Closing window, wait for device to finish..."
                  << std::endl;
        assert(vkDeviceWaitIdle(s_device) == VK_SUCCESS);
        std::cout << "Device finished" << std::endl;

        delete s_pDepthResource;
        delete s_pVertexBuffer;
        delete s_pIndexBuffer;
        delete s_pUniformBuffer;
        delete s_pTexture;
    }
    cleanup();
    return 0;
}
