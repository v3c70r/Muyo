#pragma once

#include "RenderResource.h"

namespace Muyo
{
class RenderTarget : public ImageResource
{
public:
    RenderTarget(VkFormat format, VkImageUsageFlags usage, uint32_t width,
                 uint32_t height, uint32_t numMips, uint32_t numLayers);
};
}  // namespace Muyo
