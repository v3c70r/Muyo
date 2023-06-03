#pragma once
#include <vulkan/vulkan_core.h>

#include "RenderResource.h"

namespace Muyo
{
class StorageImageResource : public ImageResource
{
public:
    StorageImageResource(VkFormat format, uint32_t nWidth, uint32_t nHeight);
};

}  // namespace Muyo
