#pragma once
#include <vulkan/vulkan.h>
#include <vector>

class DebugUtilsMessenger
{
public:
    void initialize();
    void uninitialize();
private:
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger = VK_NULL_HANDLE;
};

const std::vector<const char*>& getValidationLayerNames();

const char* getValidationExtensionName();
