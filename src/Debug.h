#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

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

VkResult setDebugUtilsObjectName(uint64_t objectHandle, VkObjectType objectType,
                                 const char* sName);

void beginMarker(VkCommandBuffer cmd, std::string&& marker, uint64_t color);

void endMarker(VkCommandBuffer cmd);

class ScopedGPUMarker
{
public:
    ScopedGPUMarker(VkCommandBuffer cmd, std::string&& marker)
    {
        beginMarker(cmd, std::forward<std::string>(marker), (uint16_t)16);
    }
    ~ScopedGPUMarker()
    {
        endMarker(m_cmd);
    }
private:
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;

};

    
