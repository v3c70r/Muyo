#pragma once
#include <vulkan/vulkan.h>
#include <cassert>

// A raii buffer creation class
// This is a bad practice 
// https://developer.nvidia.com/vulkan-memory-management
// Allocate once and bond multiple times, using the offset

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
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        assert(vkCreateBuffer(mDevice, &bufferInfo, nullptr, &mBuffer) ==
               VK_SUCCESS);
        vkGetBufferMemoryRequirements(device, mBuffer, &mMemRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = mMemRequirements.size;
        allocInfo.memoryTypeIndex = mFindMemoryType(
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
    size_t size() { return mMemRequirements.size; }
    VkDevice device() const { return mDevice; }
    VkPhysicalDevice physicalDevice() const { return mPhysicalDevice; }
    VkBuffer buffer() const { return mBuffer; }
protected:
    uint32_t mFindMemoryType(VkPhysicalDevice physicalDevice,
                             uint32_t typeFilter,
                             VkMemoryPropertyFlags properties)
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

    VkDevice mDevice;
    VkPhysicalDevice mPhysicalDevice;
    VkBuffer mBuffer;
    VkDeviceMemory mDeviceMemory;
    VkMemoryRequirements mMemRequirements;
};
