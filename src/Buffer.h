#pragma once
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstring> // memcpy

// A raii buffer creation class
// TODO: fix the bad practice
// This is a bad practice 
// https://developer.nvidia.com/vulkan-memory-management
// Allocate once and bond multiple times, using the offset

static size_t alignUp(size_t size, size_t alignment) {
    return (size + alignment - 1) / alignment * alignment;
}

class Buffer {
public:
    Buffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
           size_t size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties)
        : mDevice(device),
          mPhysicalDevice(physicalDevice),
          mBuffer(VK_NULL_HANDLE),
          mDeviceMemory(VK_NULL_HANDLE),
          mMemRequirements({})
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size == 0 ? 1 : size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        assert(vkCreateBuffer(mDevice, &bufferInfo, nullptr, &mBuffer) ==
               VK_SUCCESS);
        vkGetBufferMemoryRequirements(device, mBuffer, &mMemRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = mMemRequirements.size;

        allocInfo.memoryTypeIndex = findMemoryType(
            mPhysicalDevice, mMemRequirements.memoryTypeBits, properties);

        assert(vkAllocateMemory(mDevice, &allocInfo, nullptr, &mDeviceMemory) ==
               VK_SUCCESS);
        vkBindBufferMemory(mDevice, mBuffer, mDeviceMemory, 0);
    }
    virtual ~Buffer()
    {
        vkDestroyBuffer(mDevice, mBuffer, nullptr);
        vkFreeMemory(mDevice, mDeviceMemory, nullptr);
    }
    void setData(void* pData)
    {
        void* pMappedData = nullptr;
        vkMapMemory(mDevice, mDeviceMemory, 0, mMemRequirements.size, 0,
                    &pMappedData);
        memcpy(pMappedData, pData, (size_t)mMemRequirements.size);
        vkUnmapMemory(mDevice, mDeviceMemory);
    }
    size_t size() const { return mMemRequirements.size; }
    size_t alignment() const {return mMemRequirements.alignment;}
    VkDevice device() const { return mDevice; }
    VkPhysicalDevice physicalDevice() const { return mPhysicalDevice; }
    VkBuffer buffer() const { return mBuffer; }

    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                   uint32_t typeFilter,                 // bitfield of supported types
                                   VkMemoryPropertyFlags properties)    // wanted bitfield 
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
            if (typeFilter & (1 << i) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) ==
                    properties)
                return i;
        // No available type
        assert(false);
    }

protected:
    

    VkDevice mDevice;
    VkPhysicalDevice mPhysicalDevice;
    VkBuffer mBuffer;
    VkDeviceMemory mDeviceMemory;
    VkMemoryRequirements mMemRequirements;
};
