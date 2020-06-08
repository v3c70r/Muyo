// Shared push constant blocks
#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct UIPushConstBlock
{
    glm::vec2 scale;
    glm::vec2 translate;
};
struct IrradianceMapPushConstBlock
{
    glm::mat4 mMvp;
    float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
    float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
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

