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
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE,
                                             m_buffer, m_allocation,
                                             "Uniform Buffer");
        m_nSize = size;
    }
    void SetData(const T& buffer)
    {
        BufferResource::SetData(&buffer, sizeof(T));
    }
};
}  // namespace Muyo
