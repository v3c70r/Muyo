#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstring>  // strcmp
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <vector>
#include <array>
#include <glm/glm.hpp>

#include "VertexBuffer.h"

const int WIDTH = 800;
const int HEIGHT = 600;

static VkInstance s_instance;

static std::vector<const char*> s_validationLayers{
    "VK_LAYER_LUNARG_standard_validation"};

static bool s_isValidationEnabled = false;

static GLFWwindow* s_pWindow = nullptr;

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

VkRenderPass s_renderPass;

// Pipeline
static VkPipelineLayout s_pipelineLayout;

static VkPipeline s_graphicsPipeline;

// Command
static VkCommandPool s_commandPool;
static std::vector<VkCommandBuffer> s_commandBuffers;

// sync
static VkSemaphore s_imageAvailableSemaphore;
static VkSemaphore s_renderFinishedSemaphore;

// PHYSICAL Device extensions
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

std::vector<Vertex> getVertices()
{
    std::vector<Vertex> res = {{{0.0f, -0.5f, 0.0}, {1.0f, 0.0f, 0.0f}},
                               {{0.5f, 0.5f, 0.0}, {0.0f, 1.0f, 0.0f}},
                               {{-0.5f, 0.5f, 0.0}, {0.0f, 0.0f, 1.0f}}};
    return res;
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

void recreateSwapChain();       // fwd declaration
static void onWindowResize( GLFWwindow* pWindow, int width, int height)
{
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
    //   createInfo.flags =
    //       VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
    //       VK_DEBUG_REPORT_WARNING_BIT_EXT |
    //       VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
    //       VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
    createInfo.flags = VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                       VK_DEBUG_REPORT_ERROR_BIT_EXT;
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
    //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    s_pWindow = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowSizeCallback(s_pWindow, onWindowResize);
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

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = s_surface;
    createInfo.minImageCount = imageCount;
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
    vkGetSwapchainImagesKHR(s_device, s_swapChain, &imageCount, nullptr);
    s_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(s_device, s_swapChain, &imageCount,
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

    // attachment reference
    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment =
        0;  // indicates the attachemnt index in the attachments array
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;  // the index is the output
                                                      // from the fragment
                                                      // shader

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
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
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
    VkShaderModule vertShdr = m_createShaderModule(m_readSpv("./vert.spv"));
    VkShaderModule fragShdr = m_createShaderModule(m_readSpv("./frag.spv"));

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
    VkVertexInputBindingDescription bindingDesc = Vertex::getBindingDescription();
    std::array<VkVertexInputAttributeDescription, 2> attribDescs = Vertex::getAttributeDescriptions();
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDescs.size());
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

    // 8. Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;
    assert(vkCreatePipelineLayout(s_device, &pipelineLayoutInfo, nullptr,
                                  &s_pipelineLayout) == VK_SUCCESS);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;

    pipelineInfo.pViewportState = &viewportState;

    pipelineInfo.pRasterizationState = &rasterizerInfo;

    pipelineInfo.pMultisampleState = &multisamplingInfo;

    pipelineInfo.pDepthStencilState = nullptr;

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
        VkImageView attachments[] = {s_swapChainImageViews[i]};
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = s_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
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

void createCommandBuffers(const VertexBuffer& vertexBuffer)
{
    s_commandBuffers.resize(s_swapChainFramebuffers.size());
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = s_commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = (uint32_t)s_commandBuffers.size();

    assert(vkAllocateCommandBuffers(s_device, &cmdAllocInfo,
                                    s_commandBuffers.data()) == VK_SUCCESS);

    for (size_t i = 0; i < s_commandBuffers.size(); i++) {
        // Begin cmd buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        vkBeginCommandBuffer(s_commandBuffers[i], &beginInfo);

        // Begin renderpass
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = s_renderPass;
        renderPassBeginInfo.framebuffer = s_swapChainFramebuffers[i];

        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = s_swapChainExtent;

        VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;
        vkCmdBeginRenderPass(s_commandBuffers[i], &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkBuffer vb = vertexBuffer.getBuffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(s_commandBuffers[i], 0, 1, &vb, &offset);
        vkCmdBindPipeline(s_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          s_graphicsPipeline);

        vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);

        vkCmdEndRenderPass(s_commandBuffers[i]);

        assert(vkEndCommandBuffer(s_commandBuffers[i]) == VK_SUCCESS);
    }
}

void createSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    assert(vkCreateSemaphore(s_device, &semaphoreInfo, nullptr,
                             &s_imageAvailableSemaphore) == VK_SUCCESS);
    assert(vkCreateSemaphore(s_device, &semaphoreInfo, nullptr,
                             &s_renderFinishedSemaphore) == VK_SUCCESS);
}

void cleanupSwapChain()
{
    for (auto framebuffer : s_swapChainFramebuffers)
        vkDestroyFramebuffer(s_device, framebuffer, nullptr);
    vkFreeCommandBuffers(s_device, s_commandPool, static_cast<uint32_t>(s_commandBuffers.size()), s_commandBuffers.data());
    vkDestroyPipeline(s_device, s_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(s_device, s_pipelineLayout, nullptr);
    vkDestroyRenderPass(s_device, s_renderPass, nullptr);
    for (auto& imageView : s_swapChainImageViews)
        vkDestroyImageView(s_device, imageView, nullptr);
    vkDestroySwapchainKHR(s_device, s_swapChain, nullptr);
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
    createFramebuffers();
    // todo: fix this
    //createCommandBuffers();
}



void cleanup()
{
    cleanupSwapChain();
    vkDestroySemaphore(s_device, s_imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(s_device, s_renderFinishedSemaphore, nullptr);

    vkDestroyCommandPool(s_device, s_commandPool, nullptr);
    vkDestroySurfaceKHR(s_instance, s_surface, nullptr);
    vkDestroyDevice(s_device, nullptr);
    DestroyDebugReportCallbackEXT(s_instance, s_debugCallback, nullptr);
    vkDestroyInstance(s_instance, nullptr);
    glfwDestroyWindow(s_pWindow);
    glfwTerminate();
}

void drawFrame()
{
    uint32_t imageIndex;
    vkAcquireNextImageKHR(
        s_device, s_swapChain, std::numeric_limits<uint64_t>::max(),
        s_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    // submit command buffer
    VkPipelineStageFlags stageFlag =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // only color attachment
                                                        // waits for the
                                                        // semaphore
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &s_imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &stageFlag;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &s_commandBuffers[imageIndex];

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &s_renderFinishedSemaphore;

    assert(vkQueueSubmit(s_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) ==
           VK_SUCCESS);

    // Present swap chain
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &s_renderFinishedSemaphore;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &s_swapChain;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(s_presentQueue, &presentInfo);
}

int main()
{
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
    createFramebuffers();
    createCommandPool();

    VertexBuffer vb(s_device, getVertices());
    vb.allocate(s_physicalDevice);
    vb.setData(getVertices());
    createCommandBuffers(vb);
    createSemaphores();

    // Mainloop
    while (!glfwWindowShouldClose(s_pWindow)) {
        glfwPollEvents();
        drawFrame();
    }
    vb.release();
    vb.destroy();
    vkDeviceWaitIdle(s_device);
    cleanup();
    return 0;
}
