#pragma once
#include "RenderPass.h"

struct ImGuiResource
{
};
class RenderPassUI : public RenderPass
{
public:
    RenderPassUI();
    void SetRenderTargetImageView(VkImageView targetView, uint32_t nWidth,
                                  uint32_t nHeight);
    ~RenderPassUI();

private:
    VkFramebuffer m_framebuffer;
};
