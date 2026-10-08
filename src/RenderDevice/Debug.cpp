#include "Debug.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <ostream>

#include "VkRenderDevice.h"

namespace Muyo
{
namespace
{
/// @param result A VkResult known to be a failure.
/// @return A readable name for the results this engine treats as fatal.
const char* VkResultName(VkResult result)
{
    switch (result)
    {
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        // This header only carries the extension spelling of the 1.3 core name.
        case VK_ERROR_PIPELINE_COMPILE_REQUIRED_EXT:
            return "VK_ERROR_PIPELINE_COMPILE_REQUIRED";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_NOT_PERMITTED_KHR:
            return "VK_ERROR_NOT_PERMITTED_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        default:
            return "unrecognised VkResult";
    }
}
}  // namespace

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
    friend std::ostream& operator<<(std::ostream& os, const Modifier& mod) { return os << "\033[" << mod.code << "m"; }
};
}  // namespace Color

static const char* VALIDATE_EXTENSION = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
const char* GetValidationExtensionName() { return VALIDATE_EXTENSION; }

const char* GetValidationLayerName() { return "VK_LAYER_KHRONOS_validation"; }

static VkBool32 DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT,
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

        std::cerr << red << "[ERROR]:" << pCallbackData->pMessage << normal << std::endl;
        assert(0 && "Vulkan Error");
    }
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << yellow << "[WARNING]:" << pCallbackData->pMessage << normal << std::endl;
    }

    return VK_FALSE;
}

// Call add callback by query the extension
VkResult CreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pCallback)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pCallback);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT callback,
                                const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, callback, pAllocator);
    }
}

void DebugUtilsMessenger::Initialize(const VkInstance& instance)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    assert(CreateDebugUtilsMessenger(instance, &createInfo, nullptr, &m_debugUtilsMessenger) == VK_SUCCESS);
}

void DebugUtilsMessenger::Uninitialize(const VkInstance& instance)
{
    destroyDebugUtilsMessenger(instance, m_debugUtilsMessenger, nullptr);
}

// Debug Utils markers
// // Call add callback by query the extension
VkResult setDebugUtilsObjectName(uint64_t objectHandle, VkObjectType objectType, const std::string& sName)
{
    // Set debug name for the pipeline
    VkDebugUtilsObjectNameInfoEXT info;
    info.pNext = nullptr;
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.pObjectName = sName.c_str();
    info.objectType = objectType;
    info.objectHandle = objectHandle;

    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(GetRenderDevice()->GetInstance(),
                                                                        "vkSetDebugUtilsObjectNameEXT");
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

    // Note: the function pointer is re-resolved on every call. Caching it in a static binds it to
    // the device that happened to be current the first time and goes stale if the device is
    // recreated (which tests do per case).
    auto func = (PFN_vkQueueBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(GetRenderDevice()->GetDevice(),
                                                                        "vkQueueBeginDebugUtilsLabelEXT");
    if (func != nullptr) func(queue, &labelInfo);
}

void endMarker(VkQueue queue)
{
    auto func = (PFN_vkQueueEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(GetRenderDevice()->GetDevice(),
                                                                      "vkQueueEndDebugUtilsLabelEXT");
    if (func != nullptr) func(queue);
}

// specialization for VkQueue and VkCommandBuffer
void beginMarker(VkCommandBuffer cmd, std::string&& name, uint64_t)
{
    VkDebugUtilsLabelEXT labelInfo = {};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = name.data();

    auto func = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(GetRenderDevice()->GetDevice(),
                                                                      "vkCmdBeginDebugUtilsLabelEXT");
    if (func != nullptr) func(cmd, &labelInfo);
}

void endMarker(VkCommandBuffer cmd)
{
    auto func = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(GetRenderDevice()->GetDevice(),
                                                                    "vkCmdEndDebugUtilsLabelEXT");
    if (func != nullptr) func(cmd);
}

void VKAssertFailed(VkResult result, const std::source_location& location)
{
    std::cerr << "[FATAL]: Vulkan call failed with " << VkResultName(result) << " (" << static_cast<int>(result)
              << ")\n"
              << "  at " << location.file_name() << ":" << location.line() << " in " << location.function_name()
              << std::endl;
    std::abort();
}

}  // namespace Muyo
