#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>

#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
#include "RenderResource.h"

template <class VertexType>
class VertexBuffer : public BufferResource
{
public:
    VertexBuffer(bool bStagedUpoload = true)
        : BufferResource(
              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | (bStagedUpoload ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0)  // Staged upload needs to be transfer dist bit
#ifdef FEATURE_RAY_TRACING
                  | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
#endif
              ,
              bStagedUpoload ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU)
    {
    }
    VertexBuffer(const std::vector<VertexType>& vVertexData, bool bStagedUpoload = true) : VertexBuffer(bStagedUpoload)
    {
        size_t nSizeInByte = vVertexData.size() * sizeof(VertexType);
        GetMemoryAllocator()->AllocateBuffer(nSizeInByte, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation, "VertexBuffer");
        SetData(vVertexData.data(), nSizeInByte);
    }
};

class IndexBuffer : public BufferResource
{
public:
    IndexBuffer(bool bStagedUpoload = true)
        : BufferResource(
              VK_BUFFER_USAGE_INDEX_BUFFER_BIT 
              | (bStagedUpoload ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0)   // Staged upload needs to be transfer dist bit
#ifdef FEATURE_RAY_TRACING
              | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
#endif
              ,
              bStagedUpoload ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU
          ) 
        {}
		template<class IndexType>
        IndexBuffer(const std::vector<IndexType>& vIndexData, bool bStagedUpoload = true) : IndexBuffer(bStagedUpoload)
        {
			size_t nSizeInByte = vIndexData.size() * sizeof(IndexType);
            GetMemoryAllocator()->AllocateBuffer(nSizeInByte, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation, "IndexBuffer");
            SetData(vIndexData.data(), nSizeInByte);
        }
};

