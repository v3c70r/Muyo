#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 textureCoord;
    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription desc = {};
        desc.binding = 0;
        desc.stride = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions()
    {
        std::vector<VkVertexInputAttributeDescription> attribDesc;
        attribDesc.resize(2);
        attribDesc[0].location = 0;
        attribDesc[0].binding = 0;
        attribDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[0].offset = offsetof(Vertex, pos);

        attribDesc[1].location = 1;
        attribDesc[1].binding = 0;
        attribDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[1].offset = offsetof(Vertex, textureCoord);
        return attribDesc;
    }
};

struct UIVertex {
    static VkVertexInputBindingDescription getBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
};
