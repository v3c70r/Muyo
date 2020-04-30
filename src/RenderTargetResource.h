#pragma once
#include "RenderResource.h"

class RenderTarget: public ImageResource
{
public:
    RenderTarget(VkFormat format, VkImageUsageFlagBits usage, uint32_t width,
                 uint32_t height);
    ~RenderTarget() override;
};
