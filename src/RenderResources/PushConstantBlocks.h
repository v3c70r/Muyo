// Shared push constant blocks
#pragma once
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

namespace Muyo
{
struct UIPushConstBlock
{
    glm::vec2 scale;
    glm::vec2 translate;
};

struct SingleFloatPushConstant
{
    float fValue;
};

template <class T>
VkPushConstantRange GetPushConstantRange(
    VkShaderStageFlagBits stageFlags = VK_SHADER_STAGE_ALL)
{
    VkPushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = stageFlags;
    pushConstantRange.size = sizeof(T);
    pushConstantRange.offset = 0;
    return pushConstantRange;
}
}  // namespace Muyo
