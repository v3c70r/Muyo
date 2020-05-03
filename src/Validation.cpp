#include "Validation.h"
#include "VkRenderDevice.h"
#include <iostream>
#include <cassert>

static std::vector<const char*> s_validationLayers{
    "VK_LAYER_LUNARG_standard_validation",
    //"VK_LAYER_GOOGLE_unique_objects",
    //"VK_LAYER_LUNARG_api_dump",
    //"VK_LAYER_LUNARG_core_validation",
    //"VK_LAYER_LUNARG_image",
    //"VK_LAYER_LUNARG_object_tracker",
    //"VK_LAYER_LUNARG_parameter_validation",
    //"VK_LAYER_LUNARG_swapchain",
    //"VK_LAYER_GOOGLE_threading"
};

const std::vector<const char*>& getValidationLayerNames()
{
    return s_validationLayers;
}

static const char* VALIDATE_EXTENSION = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
const char* getValidationExtensionName()
{
    return VALIDATE_EXTENSION;
}

static VKAPI_PTR VkBool32 debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        std::cerr << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

// Call add callback by query the extension
VkResult createDebugUtilsMessenger(
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

void DebugUtilsMessenger::initialize()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    createInfo.messageType = 
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT 
        | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
        ;
    createInfo.messageSeverity = 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
        ;
    createInfo.pfnUserCallback = debugCallback;

    assert(createDebugUtilsMessenger(
               GetRenderDevice()->GetInstance(), &createInfo, nullptr,
               &m_debugUtilsMessenger) == VK_SUCCESS);
}

void DebugUtilsMessenger::uninitialize()
{

    destroyDebugUtilsMessenger(GetRenderDevice()->GetInstance(), m_debugUtilsMessenger, nullptr);
}
