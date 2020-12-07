#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>  // for ImDrawVert structure
#include <vector>
using Index = uint32_t;
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 textureCoord;
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
        attribDesc.resize(3);
        attribDesc[0].location = 0;
        attribDesc[0].binding = 0;
        attribDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[0].offset = offsetof(Vertex, pos);

        attribDesc[1].location = 1;
        attribDesc[1].binding = 0;
        attribDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[1].offset = offsetof(Vertex, normal);

        attribDesc[2].location = 2;
        attribDesc[2].binding = 0;
        attribDesc[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attribDesc[2].offset = offsetof(Vertex, textureCoord);
        return attribDesc;
    }
};

struct UIVertex {
    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription desc = {};
        desc.binding = 0;
        desc.stride = sizeof(ImDrawVert);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
    {
        std::vector<VkVertexInputAttributeDescription> attribDesc;
        attribDesc.resize(3);
        attribDesc[0].location = 0;
        attribDesc[0].binding = 0;
        attribDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
        attribDesc[0].offset = offsetof(ImDrawVert, pos);

        attribDesc[1].location = 1;
        attribDesc[1].binding = 0;
        attribDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
        attribDesc[1].offset = offsetof(ImDrawVert, uv);

        attribDesc[2].location = 2;
        attribDesc[2].binding = 0;
        attribDesc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
        attribDesc[2].offset = offsetof(ImDrawVert, col);
        return attribDesc;
    }
};
