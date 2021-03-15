#pragma once
#include <vk_mem_alloc.h>

#include "Debug.h"
#include "VkMemoryAllocator.h"

class IRenderResource
{
public:
    virtual ~IRenderResource(){};
};

class ImageResource : public IRenderResource
{
public:
    VkImageView getView() const { return m_view; }
    VkImage getImage() const { return m_image; }
    void setName(const std::string& sName)
    {
        if (m_image != VK_NULL_HANDLE)
        {
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_image),
                                    VK_OBJECT_TYPE_IMAGE, sName.data());
        }
    }

protected:
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
};

class BufferResource : public IRenderResource
{
public:
    BufferResource(VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage)
        : BUFFER_USAGE(bufferUsage), MEMORY_USAGE(memoryUsage)
    {}
    virtual VkBuffer buffer() const { return m_buffer; }
    virtual ~BufferResource()
    {
        GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
    }

protected:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    const VkBufferUsageFlags BUFFER_USAGE = 0x0;
    const VmaMemoryUsage MEMORY_USAGE = VMA_MEMORY_USAGE_UNKNOWN;
};

class AccelerationStructureBuffer : public BufferResource
{
public:
    AccelerationStructureBuffer(uint32_t size)
        : BufferResource(
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT |
                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
              VMA_MEMORY_USAGE_CPU_ONLY)
    {
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE,
                                             m_buffer, m_allocation,
                                             "AccelerationStrucutre");
    }
};
