#pragma once
#include <vulkan/vulkan.h>

class ImmediateContextInfo
{
public:
    static VkCommandPool pool;
    static VkQueue queue;
};
