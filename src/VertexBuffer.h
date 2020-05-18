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
class MemoryBuffer
{
public:
    MemoryBuffer(bool stagedUpload = true);
    void* map();
    void unmap();
    void setData(const void* data, size_t size);
    void flush();
    const VkBuffer& buffer() const { return m_buffer; }
    virtual ~MemoryBuffer();
protected:
    std::string m_sBufferName = "";
    VkBufferUsageFlags m_bufferUsageFlags;
private:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;

    VmaMemoryUsage m_memoryUsage;
    size_t m_nSize = 0;
};

class VertexBuffer : public MemoryBuffer
{
public:
    VertexBuffer(bool stagedUpload = true,
                 std::string sBufferName = "vertex buffer")
        : MemoryBuffer(stagedUpload)
    {
        m_bufferUsageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        m_sBufferName = sBufferName;
    }
private:
};

class IndexBuffer : public MemoryBuffer
{
public:
    IndexBuffer(bool stagedUpload = true,
                 std::string sBufferName = "index buffer")
        : MemoryBuffer(stagedUpload)
    {
        m_bufferUsageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        m_sBufferName = sBufferName;
    }
};
