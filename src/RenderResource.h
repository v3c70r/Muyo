#pragma once
#include <vk_mem_alloc.h>

#include "Debug.h"
#include "VkMemoryAllocator.h"

class IRenderResource
{
public:
    virtual ~IRenderResource(){};
    virtual VkObjectType GetVkObjectType() const = 0;
    virtual void SetDebugName(const std::string& sName) const = 0;
};

class ImageResource : public IRenderResource
{
public:
    VkImageView getView() const { return m_view; }
    VkImage getImage() const { return m_image; }
    virtual ~ImageResource()
    {
        GetMemoryAllocator()->FreeImage(m_image, m_allocation);
    }
    virtual VkObjectType GetVkObjectType() const override
    {
        return VK_OBJECT_TYPE_IMAGE;
    }
    virtual void SetDebugName(const std::string& sName) const override
    {
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_image),
                                GetVkObjectType(), sName.c_str());
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
    {
    }
    virtual VkBuffer buffer() const { return m_buffer; }
    virtual ~BufferResource()
    {
        GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
    }
    virtual VkObjectType GetVkObjectType() const override
    {
        return VK_OBJECT_TYPE_BUFFER;
    }

    virtual void SetDebugName(const std::string& sName) const override
    {
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_buffer),
                                GetVkObjectType(), sName.c_str());
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
