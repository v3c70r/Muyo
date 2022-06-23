#pragma once
#include "RenderResource.h"
#include <vulkan/vulkan_core.h>

class StorageImageResource : public ImageResource
{
public:
    StorageImageResource(VkFormat format,uint32_t nWidth, uint32_t nHeight);
};

