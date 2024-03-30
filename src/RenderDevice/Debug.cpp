#include "Debug.h"

#include <cassert>
#include <iostream>
#include <ostream>
#include "VkRenderDevice.h"

namespace Muyo
{

namespace Color
{
enum Code
{
    FG_RED = 31,
    FG_GREEN = 32,
    FG_BLUE = 34,
    FG_YELLOW = 33,
    FG_DEFAULT = 39,
    BG_RED = 41,
    BG_GREEN = 42,
    BG_BLUE = 44,
    BG_YELLOW = 43,
    BG_DEFAULT = 49
};
class Modifier
{
    Code code;

public:
    Modifier(Code pCode) : code(pCode) {}
    friend std::ostream& operator<<(std::ostream& os, const Modifier& mod)
    {
        return os << "\033[" << mod.code << "m";
    }
};
}  // namespace Color

static std::vector<const char*> s_validationLayers{
    //"VK_LAYER_LUNARG_standard_validation",
    "VK_LAYER_KHRONOS_validation",
    "VK_LAYER_KHRONOS_synchronization2",
    //"VK_LAYER_GOOGLE_unique_objects",
    //"VK_LAYER_LUNARG_api_dump",
    //"VK_LAYER_LUNARG_core_validation",
    //"VK_LAYER_LUNARG_image",
    //"VK_LAYER_LUNARG_object_tracker",
    //"VK_LAYER_LUNARG_parameter_validation",
    //"VK_LAYER_LUNARG_swapchain",
    //"VK_LAYER_GOOGLE_threading"
};

const std::vector<const char*>& GetValidationLayerNames()
{
    return s_validationLayers;
}

static const char* VALIDATE_EXTENSION = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
const char* GetValidationExtensionName()
{
    return VALIDATE_EXTENSION;
}

const char* GetValidationLayerName()
{
    return "VK_LAYER_KHRONOS_validation";
}

static VkBool32 DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
{
    Color::Modifier red(Color::FG_RED);
    Color::Modifier normal(Color::BG_DEFAULT);
    Color::Modifier yellow(Color::FG_YELLOW);

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
        {
            std::cerr << "Queue: " << pCallbackData->pQueueLabels[i].pLabelName << std::endl;
        }

        for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
        {
            std::cerr << "CmdBuffer: " << pCallbackData->pCmdBufLabels[i].pLabelName << std::endl;
        }

        std::cerr << red << "[ERROR]:" << pCallbackData->pMessage << normal
                  << std::endl;
        assert(0 && "Vulkan Error");
    }
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << yellow << "[WARNING]:" << pCallbackData->pMessage << normal
                  << std::endl;
    }

    return VK_FALSE;
}

// Call add callback by query the extension
VkResult CreateDebugUtilsMessenger(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pCallback)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pCallback);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyDebugUtilsMessenger(VkInstance instance,
                                VkDebugUtilsMessengerEXT callback,
                                const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, callback, pAllocator);
    }
}

void DebugUtilsMessenger::Initialize(const VkInstance& instance)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    assert(CreateDebugUtilsMessenger(
               instance, &createInfo, nullptr,
               &m_debugUtilsMessenger) == VK_SUCCESS);
}

void DebugUtilsMessenger::Uninitialize(const VkInstance& instance)
{
    destroyDebugUtilsMessenger(instance, m_debugUtilsMessenger, nullptr);
}

// Debug Utils markers
// // Call add callback by query the extension
VkResult setDebugUtilsObjectName(uint64_t objectHandle, VkObjectType objectType,
                                 const std::string& sName)
{
    // Set debug name for the pipeline
    VkDebugUtilsObjectNameInfoEXT info;
    info.pNext = nullptr;
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.pObjectName = sName.c_str();
    info.objectType = objectType;
    info.objectHandle = objectHandle;

    static auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(
        GetRenderDevice()->GetInstance(), "vkSetDebugUtilsObjectNameEXT");
    if (func != nullptr)
        return func(GetRenderDevice()->GetDevice(), &info);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

// specialization for VkQueue and VkCommandBuffer
void beginMarker(VkQueue queue, std::string&& name, uint64_t)
{
    VkDebugUtilsLabelEXT labelInfo = {};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = name.data();

    static auto func = (PFN_vkQueueBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        GetRenderDevice()->GetDevice(), "vkQueueBeginDebugUtilsLabelEXT");
    func(queue, &labelInfo);
}

void endMarker(VkQueue queue)
{
    static auto func = (PFN_vkQueueEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        GetRenderDevice()->GetDevice(), "vkQueueEndDebugUtilsLabelEXT");
    func(queue);
}

// specialization for VkQueue and VkCommandBuffer
void beginMarker(VkCommandBuffer cmd, std::string&& name, uint64_t)
{
    VkDebugUtilsLabelEXT labelInfo = {};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = name.data();

    static auto func = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        GetRenderDevice()->GetDevice(), "vkCmdBeginDebugUtilsLabelEXT");
    func(cmd, &labelInfo);
}

void endMarker(VkCommandBuffer cmd)
{
    static auto func = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        GetRenderDevice()->GetDevice(), "vkCmdEndDebugUtilsLabelEXT");
    func(cmd);
}

void VK_ASSERT(VkResult result)
{
    assert(result == VK_SUCCESS);
}

}  // namespace Muyo
