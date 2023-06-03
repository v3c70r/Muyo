#pragma once
#include <vulkan/vulkan.h>

#include <cassert>
#include <string>
#include <vector>

namespace Muyo
{
class DebugUtilsMessenger
{
public:
    void Initialize(const VkInstance& instance);
    void Uninitialize(const VkInstance& instance);

private:
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger = VK_NULL_HANDLE;
};

const std::vector<const char*>& GetValidationLayerNames();

const char* GetValidationExtensionName();
const char* GetValidationLayerName();

VkResult setDebugUtilsObjectName(uint64_t objectHandle, VkObjectType objectType,
                                 const char* sName);

// Scoped markers
void beginMarker(VkQueue queue, std::string&& name, uint64_t color);
void endMarker(VkQueue queue);

void beginMarker(VkCommandBuffer cmd, std::string&& name, uint64_t color);
void endMarker(VkCommandBuffer cmd);

// Using template to handle command buffer and queue markers
template <class T>
class ScopedMarker
{
public:
    ScopedMarker(T vkObj, std::string&& marker)
    {
        m_markedVkObject = vkObj;
        beginMarker(vkObj, std::forward<std::string>(marker), uint64_t(0));
    }
    ~ScopedMarker()
    {
        endMarker(m_markedVkObject);
    }

private:
    T m_markedVkObject = VK_NULL_HANDLE;
};

// Assertion for vulkan function calls
void VK_ASSERT(VkResult result);
}  // namespace Muyo

// Create a macro to generate local variables
#define TOKENPASTE(x, y) x##y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define SCOPED_MARKER(OBJ, MESSAGE) \
    auto TOKENPASTE2(scoped_marker_, __LINE__) = ScopedMarker(OBJ, MESSAGE)


