#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

struct PerViewData {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    static VkDescriptorSetLayoutBinding getDescriptorSetLayoutBinding()
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.pImmutableSamplers = nullptr;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        return uboLayoutBinding;
    }
};

template<class T>
class UniformBuffer
{
public:
    UniformBuffer()
    {
        constexpr size_t size = sizeof(T);
        if (size != 0)
        {
            GetMemoryAllocator()->AllocateBuffer(
                size, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation, "Uniform Buffer");
        }
    }
    void setData(const T& buffer, VkCommandPool commandPool, VkQueue queue)
    {
        void* pMappedMemory = nullptr;
        GetMemoryAllocator()->MapBuffer(m_allocation, &pMappedMemory);
        memcpy(pMappedMemory, (void*)&buffer, sizeof(T));
        GetMemoryAllocator()->UnmapBuffer(m_allocation);
    }
    ~UniformBuffer()
    {
        GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
    }

    VkBuffer buffer() const { return m_buffer; }
private:
    VkBuffer m_buffer = VK_NULL_HANDLE;

    VmaAllocation m_allocation = VK_NULL_HANDLE;
    const VkBufferUsageFlags BUFFER_USAGE = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    const VmaMemoryUsage MEMORY_USAGE = VMA_MEMORY_USAGE_CPU_TO_GPU;
};
