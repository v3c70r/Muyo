#pragma once
#include "RenderResource.h"
#include <vulkan/vulkan_core.h>

class ImageStorageResource : public ImageResource
{
public:
    ImageStorageResource(VkFormat format,uint32_t nWidth, uint32_t nHeight);
};

