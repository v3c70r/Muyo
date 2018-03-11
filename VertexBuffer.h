#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <iostream>

struct Vertex{
    glm::vec3 pos;
    glm::vec3 color;
    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription desc = {};
        desc.binding = 0;
        desc.stride = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attribDesc = {};
        attribDesc[0].location = 0;
        attribDesc[0].binding = 0;
        attribDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[0].offset = offsetof(Vertex, pos);

        attribDesc[1].location = 1;
        attribDesc[1].binding = 0;
        attribDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[1].offset = offsetof(Vertex, color);
        return attribDesc;
    }
};

uint32_t mFindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                         VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
    // No available type
    assert(false);
}


// RAII, probably not a good idea since it need to be deleted before logical device become invalid
class VertexBuffer {
public:
    VertexBuffer(const VkDevice& device, const std::vector<Vertex>& vertices)
        : mBuffer(VK_NULL_HANDLE), mDevice(device), mBufferInfo({})
    {
        mBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        mBufferInfo.size = sizeof(vertices[0]) * vertices.size();
        mBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        mBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        assert(vkCreateBuffer(mDevice, &mBufferInfo, nullptr, &mBuffer) ==
               VK_SUCCESS);
        vkGetBufferMemoryRequirements(mDevice, mBuffer, &mMemRequirements);
    }
    void allocate(VkPhysicalDevice physicalDevice)
    {
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = mMemRequirements.size;
        allocInfo.memoryTypeIndex =
            mFindMemoryType(physicalDevice, mMemRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(vkAllocateMemory(mDevice, &allocInfo, nullptr,
                                &mVertexBufferMemory) == VK_SUCCESS);
        vkBindBufferMemory(mDevice, mBuffer, mVertexBufferMemory, 0);
    }
    void setData(const std::vector<Vertex>& vertices)
    {
        void *data;
        vkMapMemory(mDevice, mVertexBufferMemory, 0, mBufferInfo.size, 0, &data);
        memcpy(data, vertices.data(), (size_t) mBufferInfo.size);
        vkUnmapMemory(mDevice, mVertexBufferMemory);
    }
    VkBuffer getBuffer() const
    {
        return mBuffer;
    }
    void release()
    {
        vkFreeMemory(mDevice, mVertexBufferMemory, nullptr);
    }
    ~VertexBuffer()
    {
        std::cout<<"Destroy vertex buffer";
        vkDestroyBuffer(mDevice, mBuffer, nullptr);
    }
private:
    VkBuffer mBuffer;   //!< Internal buffer object (handle)
    VkDeviceMemory mVertexBufferMemory; //!< the real data memory
    VkDevice mDevice;   //!< The device on which the buffer is created
    VkMemoryRequirements mMemRequirements;
    VkBufferCreateInfo mBufferInfo;
};

