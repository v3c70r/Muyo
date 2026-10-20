#pragma once
#include <vulkan/vulkan.h>

#include <cstring>
#include <glm/glm.hpp>

#include "RenderResource.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{
template <class T>
class UniformBuffer : public BufferResource
{
public:
    UniformBuffer()
        : BufferResource(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
#ifdef FEATURE_RAY_TRACING

                             | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT

#endif
                         ,
                         VMA_MEMORY_USAGE_CPU_TO_GPU)

    {
        const size_t size = sizeof(T);
        AllocateOwnedBuffer(size, "Uniform Buffer");
        m_nSize = size;
    }
    // Keep the virtual `SetData(const void*, size_t)` visible. Without this line the typed overload below
    // hides it, which is `-Woverloaded-virtual` once per translation unit that instantiates the template
    // (#25). Calls keep resolving to the typed overload; the base one is simply no longer invisible.
    using BufferResource::SetData;
    void SetData(const T& buffer) { BufferResource::SetData(&buffer, sizeof(T)); }
};
}  // namespace Muyo
