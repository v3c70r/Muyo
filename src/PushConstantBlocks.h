// Shared push constant blocks
#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#ifdef _MSC_VER
static constexpr float M_PI = 3.141592653f;
#endif

struct UIPushConstBlock
{
    glm::vec2 scale;
    glm::vec2 translate;
};

struct PrefilteredPushConstantBlock
{
    float fRoughness;
};

template <class T>
VkPushConstantRange getPushConstantRange(
    VkShaderStageFlagBits stageFlags = VK_SHADER_STAGE_ALL)
{
    VkPushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = stageFlags;
    pushConstantRange.size = sizeof(T);
    pushConstantRange.offset = 0;
    return pushConstantRange;
}

