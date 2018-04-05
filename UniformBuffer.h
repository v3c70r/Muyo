#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "Buffer.h"

struct UnifromBufferObject {
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

class UniformBuffer
{
public:
    UniformBuffer(const VkDevice& device,
                  const VkPhysicalDevice& physicalDevice, size_t size)
        : mBuffer(device, physicalDevice, size,
                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    {
    }
    void setData(const UnifromBufferObject& buffer)
    {
        mBuffer.setData((void*)&buffer);
    }

    VkBuffer buffer() const { return mBuffer.buffer(); }
private:
    Buffer mBuffer;
};
