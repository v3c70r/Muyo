#pragma once
#include "RenderResource.h"

namespace Muyo
{
class SwapchainImageResource : public ImageResource
{
  public:
    SwapchainImageResource(VkImageView view, VkImage image)
    {
        m_view  = view;
        m_image = image;
    }
    virtual ~SwapchainImageResource() override;
};
}    // namespace Muyo
